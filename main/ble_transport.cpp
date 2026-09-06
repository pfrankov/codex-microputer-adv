#include "ble_transport.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>

#include "adaptive_ble_power.h"
#include "codex_micro_protocol.h"
#include "model.h"
#include "ui.h"
#include "usb_transport.h"
#include "esp_hid_gap.h"
#include "esp_hidd.h"
#include "esp_bt.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

extern "C" void companion_ble_store_init(void);
extern "C" esp_err_t companion_ble_store_forget_all(void);

namespace {
constexpr uint16_t kVid = 0x303a;
constexpr uint16_t kPid = 0x8360;
constexpr uint8_t kRpcReport = 6;
// Match Codex Micro's complete HID map. macOS protects keyboard-capable HID
// devices with an authenticated passkey; the Cardputer displays that code.
constexpr auto kReportMap = std::to_array<uint8_t>(
    {0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
     0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x95, 0x06,
     0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0, 0x06,
     0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95,
     0x3F, 0x09, 0x01, 0x81, 0x02, 0x95, 0x3F, 0x09, 0x02, 0x91, 0x02, 0xC0});
esp_hidd_dev_t* device = nullptr;
esp_hid_raw_report_map_t report_maps[1] = {{kReportMap.data(), kReportMap.size()}};
std::atomic<bool> connected{false};
std::atomic<bool> pairing_active{false};
constexpr uint32_t kPairingPasskey = 123456;
std::atomic<int> pending_profile{-1};
std::atomic<uint16_t> connection_handle{BLE_HS_CONN_HANDLE_NONE};
ble_gap_event_listener gap_listener = {};
bool gap_listener_registered = false;
constexpr uint8_t kNativeAddresses[3][6] = {
    {0x39, 0x20, 0x77, 0x20, 0xde, 0xc8},
    {0x3a, 0x20, 0x77, 0x20, 0xde, 0xc8},
    {0x3b, 0x20, 0x77, 0x20, 0xde, 0xc8},
};

enum class PendingType : uint8_t { Key, Joystick };
struct PendingAction {
    PendingType type = PendingType::Key;
    codex_micro::Transport target = codex_micro::Transport::None;
    char key[8] = {};
    int action = 0;
    int agent = -1;
    float angle = 0.f;
    float distance = 0.f;
    uint32_t queued_ms = 0;
};
constexpr int kPendingCapacity = 12;
constexpr uint32_t kPendingLifeMs = 750;
PendingAction pending[kPendingCapacity] = {};
int pending_count = 0;

struct RpcPacket {
    uint32_t generation = 0;
    uint8_t body[63] = {};
};
constexpr int kRpcQueueCapacity = 24;
RpcPacket rpc_queue[kRpcQueueCapacity] = {};
int rpc_head = 0;
int rpc_count = 0;
uint32_t rpc_last_send_ms = 0;
std::atomic<uint32_t> rpc_generation{1};
std::atomic<bool> rpc_reset_requested{false};
std::atomic<bool> pending_reset_requested{false};
uint32_t radio_last_service_ms = 0;
uint32_t connected_at_ms = 0;
float filtered_rssi = -90.f;
uint8_t power_tier = 4;
bool coded_phy = false;
constexpr uint32_t kRadioServiceMs = 2000;
constexpr uint32_t kAdvertisingWatchdogMs = 5000;
uint32_t advertising_last_check_ms = 0;

esp_power_level_t power_level_for_tier(uint8_t tier)
{
    static constexpr esp_power_level_t levels[5] = {
        ESP_PWR_LVL_N0, ESP_PWR_LVL_P3, ESP_PWR_LVL_P6, ESP_PWR_LVL_P12, ESP_PWR_LVL_P20,
    };
    return levels[std::min<uint8_t>(tier, 4)];
}

void apply_connection_power(uint16_t handle, uint8_t tier, int rssi)
{
    const esp_err_t err = esp_ble_tx_power_set_enhanced(ESP_BLE_ENHANCED_PWR_TYPE_CONN, handle,
                                                        power_level_for_tier(tier));
    std::printf("CCP_NATIVE|ble|power|rssi=%d|tier=%u|dbm=%d|result=%s\n", rssi,
                static_cast<unsigned>(tier), adaptive_ble::kDbm[tier], esp_err_to_name(err));
}

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

int stored_bond_count()
{
    int count = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &count);
    return count;
}

int observe_gap_event(ble_gap_event* event, void*)
{
    if (!event)
        return 0;
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0) {
        const uint16_t handle = event->connect.conn_handle;
        connection_handle.store(handle, std::memory_order_release);
        connected_at_ms = now_ms();
        power_tier = 4;
        filtered_rssi = -90.f;
        coded_phy = false;
        apply_connection_power(handle, power_tier, -90);
        // macOS can open a new HID connection before NimBLE exposes the
        // display action. A stored bond means this is only a reconnect and
        // must never cover the deck with a pairing screen.
        const bool needs_pairing = stored_bond_count() == 0;
        pairing_active.store(needs_pairing, std::memory_order_release);
        std::printf("CCP_NATIVE|ble|pairing_window|%s|pin=%06lu\n",
                    needs_pairing ? "open" : "bonded_reconnect",
                    static_cast<unsigned long>(kPairingPasskey));
        // Overlapping LL procedures in the GAP connect callback can assert in
        // the ESP32-S3 controller. Adaptive PHY tuning starts after settling.
        std::printf("CCP_NATIVE|ble|link_tune|deferred\n");
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT)
        connection_handle.store(BLE_HS_CONN_HANDLE_NONE, std::memory_order_release);
    if (event->type == BLE_GAP_EVENT_PASSKEY_ACTION) {
        std::printf("CCP_NATIVE|ble|passkey_action|action=%u\n",
                    static_cast<unsigned>(event->passkey.params.action));
    }
    if (event->type == BLE_GAP_EVENT_PASSKEY_ACTION &&
        event->passkey.params.action == BLE_SM_IOACT_DISP)
        pairing_active.store(true, std::memory_order_release);
    if (event->type == BLE_GAP_EVENT_ENC_CHANGE && event->enc_change.status == 0) {
        pairing_active.store(false, std::memory_order_release);
        std::printf("CCP_NATIVE|ble|pairing_window|secure\n");
    }
    if (event->type == BLE_GAP_EVENT_DISCONNECT)
        pairing_active.store(false, std::memory_order_release);
    return 0;
}

bool advertise_profile(uint8_t profile)
{
    if (profile >= 3)
        return false;
    const int identity = ble_hs_id_set_rnd(kNativeAddresses[profile]);
    const int advertising = identity == 0 ? esp_hid_ble_gap_adv_start() : identity;
    std::printf("CCP_NATIVE|ble|advertising|profile=%u|identity=%d|result=%d\n",
                static_cast<unsigned>(profile + 1), identity, advertising);
    return identity == 0 && advertising == 0;
}

bool queue_action(const PendingAction& action)
{
    if (pending_count >= kPendingCapacity) {
        std::printf("CCP_NATIVE|retry|full\n");
        return false;
    }
    pending[pending_count++] = action;
    std::printf("CCP_NATIVE|retry|queued|type=%s|depth=%d\n",
                action.type == PendingType::Key ? "key" : "joystick", pending_count);
    return true;
}

void pop_action()
{
    if (pending_count <= 0)
        return;
    if (pending_count > 1)
        std::memmove(pending, pending + 1,
                     sizeof(PendingAction) * static_cast<size_t>(pending_count - 1));
    --pending_count;
}

void request_rpc_reset()
{
    rpc_generation.fetch_add(1, std::memory_order_acq_rel);
    rpc_reset_requested.store(true, std::memory_order_release);
    pending_reset_requested.store(true, std::memory_order_release);
    codex_micro::reset_transport(codex_micro::Transport::Ble);
}
void clear_rpc_queue()
{
    rpc_head = 0;
    rpc_count = 0;
}
void service_rpc_reset()
{
    if (rpc_reset_requested.exchange(false, std::memory_order_acq_rel))
        clear_rpc_queue();
    if (pending_reset_requested.exchange(false, std::memory_order_acq_rel))
        pending_count = 0;
}
bool send_ble_rpc_body(const uint8_t* body, size_t length)
{
    service_rpc_reset();
    if (!connected.load(std::memory_order_acquire) || !device || length != 63)
        return false;
    if (rpc_count >= kRpcQueueCapacity)
        return false;
    const int tail = (rpc_head + rpc_count) % kRpcQueueCapacity;
    rpc_queue[tail].generation = rpc_generation.load(std::memory_order_acquire);
    std::memcpy(rpc_queue[tail].body, body, sizeof(rpc_queue[tail].body));
    ++rpc_count;
    return true;
}
bool send_rpc_body(codex_micro::Transport target, const uint8_t* body, size_t length)
{
    if (target == codex_micro::Transport::Usb)
        return companion_usb_send_rpc(body, length);
    if (target == codex_micro::Transport::Ble)
        return send_ble_rpc_body(body, length);
    return false;
}
void service_rpc_queue()
{
    service_rpc_reset();
    if (!connected.load(std::memory_order_acquire) || !device || rpc_count == 0)
        return;
    const uint32_t now = now_ms();
    if (now - rpc_last_send_ms < 4)
        return;
    RpcPacket packet = rpc_queue[rpc_head];
    if (packet.generation != rpc_generation.load(std::memory_order_acquire)) {
        clear_rpc_queue();
        return;
    }
    const esp_err_t err =
        esp_hidd_dev_input_set(device, 0, kRpcReport, packet.body, sizeof(packet.body));
    if (err != ESP_OK) {
        std::printf("CCP_NATIVE|tx|failed|transport=ble|err=%s\n", esp_err_to_name(err));
        return;
    }
    if (packet.generation != rpc_generation.load(std::memory_order_acquire)) {
        rpc_reset_requested.store(true);
        return;
    }
    rpc_head = (rpc_head + 1) % kRpcQueueCapacity;
    --rpc_count;
    rpc_last_send_ms = now;
}

void event_callback(void*, esp_event_base_t, int32_t id, void* event_data)
{
    const auto event = static_cast<esp_hidd_event_t>(id);
    auto* data = static_cast<esp_hidd_event_data_t*>(event_data);
    if (event == ESP_HIDD_START_EVENT) {
        companion_ble_store_init();
        if (!gap_listener_registered) {
            const int listener_result =
                ble_gap_event_listener_register(&gap_listener, observe_gap_event, nullptr);
            gap_listener_registered = listener_result == 0;
            std::printf("CCP_NATIVE|ble|gap_listener|result=%d\n", listener_result);
        }
        ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
        ble_svc_gap_device_name_set("Codex Micro ADV");
        const esp_err_t adv_power = esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20);
        std::printf("CCP_NATIVE|ble|adv_power|dbm=20|result=%s\n", esp_err_to_name(adv_power));
        std::printf("CCP_NATIVE|ble|started|Codex Micro ADV\n");
        const uint8_t profile = model::state.ble_profile < 3 ? model::state.ble_profile : 0;
        advertise_profile(profile);
    } else if (event == ESP_HIDD_CONNECT_EVENT) {
        connected.store(true, std::memory_order_release);
        request_rpc_reset();
        std::printf("CCP_NATIVE|ble|connected\n");
        companion_ble_link_changed(true);
    } else if (event == ESP_HIDD_DISCONNECT_EVENT) {
        connected.store(false, std::memory_order_release);
        request_rpc_reset();
        std::printf("CCP_NATIVE|ble|disconnected|reason=%d\n", data ? data->disconnect.reason : -1);
        companion_ble_link_changed(false);
        const int requested = pending_profile.exchange(-1, std::memory_order_acq_rel);
        if (requested >= 0)
            advertise_profile(static_cast<uint8_t>(requested));
        else
            esp_hid_ble_gap_adv_start();
    } else if (event == ESP_HIDD_OUTPUT_EVENT && data && data->output.report_id == kRpcReport) {
        const auto* payload =
            reinterpret_cast<const uint8_t*>(event_data) + sizeof(esp_hidd_event_data_t);
        pairing_active.store(false, std::memory_order_release);
        if (!codex_micro::enqueue_report(codex_micro::Transport::Ble, payload, data->output.length))
            std::printf("CCP_NATIVE|ble|rx_drop\n");
    }
}

void host_task(void*)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}
} // namespace

void companion_ble_start(void)
{
    ESP_ERROR_CHECK(nimble_port_init());
    codex_micro::init(send_rpc_body);
    esp_hid_device_config_t config = {
        .vendor_id = kVid,
        .product_id = kPid,
        .version = 0x0101,
        .device_name = "Codex Micro ADV",
        .manufacturer_name = "Work Louder",
        .serial_number = "CARDPUTER-ADV",
        .report_maps = report_maps,
        .report_maps_len = 1,
    };
    ESP_ERROR_CHECK(esp_hid_gap_init(HIDD_BLE_MODE));
    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD, "Codex Micro ADV"));
    // esp_hid_ble_gap_adv_init installs convenience defaults, so the real
    // authenticated display-only policy must be applied after it. The bundled
    // GAP handler injects the same fixed passkey on BLE_SM_IOACT_DISP.
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ESP_ERROR_CHECK(esp_hidd_dev_init(&config, ESP_HID_TRANSPORT_BLE, event_callback, &device));
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set("Codex Micro ADV"));
    nimble_port_freertos_init(host_task);
}

bool companion_ble_connected(void)
{
    return connected.load(std::memory_order_acquire);
}
bool companion_ble_pairing_active(void)
{
    return pairing_active.load(std::memory_order_acquire);
}
uint32_t companion_ble_pairing_passkey(void)
{
    return kPairingPasskey;
}
bool companion_ble_send(const char*, size_t)
{
    return false;
}
int companion_ble_bond_count(void)
{
    return stored_bond_count();
}
esp_err_t companion_ble_forget_bonds(void)
{
    const esp_err_t store_error = companion_ble_store_forget_all();
    if (store_error != ESP_OK)
        return store_error;

    pairing_active.store(false, std::memory_order_release);
    pending_profile.store(-1, std::memory_order_release);
    request_rpc_reset();
    const uint16_t handle = connection_handle.load(std::memory_order_acquire);
    if (handle != BLE_HS_CONN_HANDLE_NONE) {
        const int result = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        std::printf("CCP_NATIVE|ble|forget|disconnect=%d\n", result);
        return result == 0 ? ESP_OK : ESP_FAIL;
    }

    ble_gap_adv_stop();
    const bool advertising = advertise_profile(model::state.ble_profile);
    std::printf("CCP_NATIVE|ble|forget|advertising=%d\n", advertising ? 1 : 0);
    return advertising ? ESP_OK : ESP_FAIL;
}
bool companion_ble_select_profile(uint8_t profile)
{
    if (profile >= 3)
        return false;
    pending_profile.store(profile, std::memory_order_release);
    const int stop_result = ble_gap_adv_stop();
    std::printf("CCP_NATIVE|ble|profile_switch|profile=%u|adv_stop=%d|connected=%d\n",
                static_cast<unsigned>(profile + 1), stop_result, companion_ble_connected() ? 1 : 0);
    if (companion_ble_connected()) {
        const uint16_t handle = connection_handle.load(std::memory_order_acquire);
        if (handle == BLE_HS_CONN_HANDLE_NONE) {
            pending_profile.store(-1, std::memory_order_release);
            return false;
        }
        const int result = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        std::printf("CCP_NATIVE|ble|terminate|handle=%u|result=%d\n", static_cast<unsigned>(handle),
                    result);
        if (result == 0)
            return true;
        pending_profile.store(-1, std::memory_order_release);
        return false;
    }
    pending_profile.store(-1, std::memory_order_release);
    return advertise_profile(profile);
}
bool companion_codex_key(const char* key, int action, int agent)
{
    const auto target = codex_micro::active_transport();
    if (target == codex_micro::Transport::None)
        return false;
    if (codex_micro::send_key_to(target, key, action, agent))
        return true;
    PendingAction pending_action;
    pending_action.type = PendingType::Key;
    pending_action.target = target;
    std::snprintf(pending_action.key, sizeof(pending_action.key), "%s", key ? key : "");
    pending_action.action = action;
    pending_action.agent = agent;
    pending_action.queued_ms = now_ms();
    return queue_action(pending_action);
}
bool companion_codex_joystick(float angle, float distance)
{
    const auto target = codex_micro::active_transport();
    if (target == codex_micro::Transport::None)
        return false;
    if (codex_micro::send_joystick_to(target, angle, distance))
        return true;
    PendingAction p;
    p.type = PendingType::Joystick;
    p.target = target;
    p.angle = angle;
    p.distance = distance;
    p.queued_ms = now_ms();
    return queue_action(p);
}

void companion_ble_service(void)
{
    companion_usb_service();
    service_rpc_reset();
    const uint32_t radio_now = now_ms();
    const uint16_t handle = connection_handle.load(std::memory_order_acquire);
    if (handle != BLE_HS_CONN_HANDLE_NONE && pairing_active.load(std::memory_order_acquire)) {
        ble_gap_conn_desc desc = {};
        if (ble_gap_conn_find(handle, &desc) == 0 && desc.sec_state.encrypted) {
            pairing_active.store(false, std::memory_order_release);
            std::printf("CCP_NATIVE|ble|pairing_window|encrypted_poll\n");
        }
    }
    if (handle != BLE_HS_CONN_HANDLE_NONE && radio_now - radio_last_service_ms >= kRadioServiceMs) {
        radio_last_service_ms = radio_now;
        int8_t rssi = 0;
        if (ble_gap_conn_rssi(handle, &rssi) == 0) {
            filtered_rssi = filtered_rssi * 0.7f + static_cast<float>(rssi) * 0.3f;
            const bool was_weak = model::state.ble_signal_weak;
            model::state.ble_rssi = static_cast<int8_t>(filtered_rssi);
            model::state.ble_signal_weak =
                adaptive_ble::weak_signal(static_cast<int>(filtered_rssi), was_weak);
            if (model::state.ble_signal_weak != was_weak)
                ui::invalidate();
            // Hold maximum power for the first six seconds after reconnect so
            // service discovery and Codex subscriptions complete robustly.
            const uint32_t connected_ms = radio_now - connected_at_ms;
            const uint8_t next =
                !adaptive_ble::tuning_ready(connected_ms)
                    ? 4
                    : adaptive_ble::next_tier(power_tier, static_cast<int>(filtered_rssi));
            if (next != power_tier) {
                power_tier = next;
                apply_connection_power(handle, power_tier, static_cast<int>(filtered_rssi));
            }
            const bool next_coded =
                adaptive_ble::wants_coded_phy(static_cast<int>(filtered_rssi), coded_phy);
            if (adaptive_ble::tuning_ready(connected_ms) && next_coded != coded_phy) {
                const uint8_t mask =
                    next_coded ? BLE_GAP_LE_PHY_CODED_MASK : BLE_GAP_LE_PHY_1M_MASK;
                const int result = ble_gap_set_prefered_le_phy(
                    handle, mask, mask,
                    next_coded ? BLE_GAP_LE_PHY_CODED_S2 : BLE_GAP_LE_PHY_CODED_ANY);
                if (result == 0)
                    coded_phy = next_coded;
                std::printf("CCP_NATIVE|ble|phy|coded=%d|rssi=%d|result=%d\n", next_coded ? 1 : 0,
                            static_cast<int>(filtered_rssi), result);
            }
        }
    }
    if (handle == BLE_HS_CONN_HANDLE_NONE && model::state.ble_signal_weak) {
        model::state.ble_signal_weak = false;
        model::state.ble_rssi = 0;
        ui::invalidate();
    }
    if (handle == BLE_HS_CONN_HANDLE_NONE &&
        radio_now - advertising_last_check_ms >= kAdvertisingWatchdogMs) {
        advertising_last_check_ms = radio_now;
        if (!ble_gap_adv_active() && pending_profile.load(std::memory_order_acquire) < 0) {
            const bool restarted = advertise_profile(model::state.ble_profile);
            std::printf("CCP_NATIVE|ble|adv_watchdog|restarted=%d\n", restarted ? 1 : 0);
        }
    }
    service_rpc_queue();
    if (codex_micro::active_transport() == codex_micro::Transport::None)
        return;
    while (pending_count > 0) {
        const PendingAction action = pending[0];
        const uint32_t age = now_ms() - action.queued_ms;
        if (age > kPendingLifeMs) {
            std::printf("CCP_NATIVE|retry|expired|age_ms=%u\n", static_cast<unsigned>(age));
            pop_action();
            continue;
        }
        if (codex_micro::active_transport() != action.target) {
            std::printf("CCP_NATIVE|retry|expired|reason=transport_switch\n");
            pop_action();
            continue;
        }
        const bool sent =
            action.type == PendingType::Key
                ? codex_micro::send_key_to(action.target, action.key, action.action, action.agent)
                : codex_micro::send_joystick_to(action.target, action.angle, action.distance);
        if (!sent)
            return;
        std::printf("CCP_NATIVE|retry|sent|type=%s|age_ms=%u\n",
                    action.type == PendingType::Key ? "key" : "joystick",
                    static_cast<unsigned>(age));
        pop_action();
    }
}

extern "C" void ble_hid_task_start_up(void)
{
    if (connection_handle.load(std::memory_order_acquire) == BLE_HS_CONN_HANDLE_NONE &&
        !connected.load(std::memory_order_acquire))
        esp_hid_ble_gap_adv_start();
}
