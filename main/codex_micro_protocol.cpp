#include "codex_micro_protocol.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host_select.h"
#include "link.h"
#include "model.h"
#include "rpc_framer.h"
#include "session_sync.h"
#include "status_reducer.h"
#include "ui.h"

namespace codex_micro {
namespace {

constexpr uint8_t kRpcChannel = rpc_framer::kChannel;
constexpr size_t kPayload = rpc_framer::kPayload;
constexpr size_t kBody = 63; // BLE characteristic excludes report id 6.
constexpr size_t kInboundCapacity = 32;

struct InboundReport {
    Transport source = Transport::None;
    uint32_t epoch = 0;
    uint8_t length = 0;
    uint8_t body[kBody] = {};
};

SendReport send_report = nullptr;
QueueHandle_t inbound_queue = nullptr;
StaticQueue_t inbound_queue_state = {};
uint8_t inbound_queue_storage[kInboundCapacity * sizeof(InboundReport)] = {};

rpc_framer::Assembler usb_input;
rpc_framer::Assembler ble_input;
session_sync::Tracker usb_session;
session_sync::Tracker ble_session;
std::atomic<uint32_t> usb_epoch{1};
std::atomic<uint32_t> ble_epoch{1};
std::atomic<uint32_t> usb_last_valid_ms{0};
std::atomic<uint32_t> ble_last_valid_ms{0};
std::atomic<bool> usb_reset_pending{false};
std::atomic<bool> ble_reset_pending{false};
Transport presentation_owner = Transport::None;
uint32_t selection_guard_until_ms = 0;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

int transport_index(Transport source)
{
    if (source == Transport::Usb)
        return 0;
    if (source == Transport::Ble)
        return 1;
    return -1;
}

std::atomic<uint32_t>& epoch_for(Transport source)
{
    return source == Transport::Usb ? usb_epoch : ble_epoch;
}

std::atomic<uint32_t>& last_valid_for(Transport source)
{
    return source == Transport::Usb ? usb_last_valid_ms : ble_last_valid_ms;
}

std::atomic<bool>& reset_pending_for(Transport source)
{
    return source == Transport::Usb ? usb_reset_pending : ble_reset_pending;
}

rpc_framer::Assembler& input_for(Transport source)
{
    return source == Transport::Usb ? usb_input : ble_input;
}

session_sync::Tracker& session_for(Transport source)
{
    return source == Transport::Usb ? usb_session : ble_session;
}

void reset_presentation_sync(const char* reason)
{
    usb_session.begin();
    ble_session.begin();
    model::state.announcement_count = 0;
    for (auto& task : model::state.tasks) {
        task.completion_hold = false;
        task.seen = false;
    }
    ui::cancel_status_announcements();
    std::printf("CCP_NATIVE|session_sync|baseline|reason=%s\n", reason ? reason : "unknown");
}

void apply_pending_reset(Transport source)
{
    if (!reset_pending_for(source).exchange(false, std::memory_order_acq_rel))
        return;
    input_for(source).reset();
    session_for(source).begin();
    if (presentation_owner == source) {
        presentation_owner = Transport::None;
        reset_presentation_sync("transport_reset");
    }
    std::printf("CCP_NATIVE|transport|reset|source=%s\n", transport_name(source));
}

void extend_selection_guard(uint32_t duration_ms)
{
    const uint32_t candidate = now_ms() + duration_ms;
    if (static_cast<int32_t>(candidate - selection_guard_until_ms) > 0)
        selection_guard_until_ms = candidate;
}

bool selection_guarded()
{
    return static_cast<int32_t>(selection_guard_until_ms - now_ms()) > 0;
}

host_select::Snapshot snapshot_from_tasks()
{
    host_select::Snapshot snap;
    for (int i = 0; i < 6; ++i) {
        const auto& task = model::state.tasks[i];
        snap.slots[i].color = task.color;
        snap.slots[i].brightness = task.brightness;
        snap.slots[i].effect = task.effect;
        snap.slots[i].present = task.present;
    }
    return snap;
}

uint8_t parse_effect(cJSON* effect, uint8_t fallback)
{
    if (cJSON_IsNumber(effect))
        return static_cast<uint8_t>(effect->valuedouble);
    if (cJSON_IsString(effect))
        return host_select::effect_named(effect->valuestring, fallback);
    return fallback;
}

bool parse_sync_keys(cJSON* sk)
{
    if (cJSON_IsTrue(sk)) return true;
    return cJSON_IsNumber(sk) && sk->valuedouble != 0;
}

void overlay_thstatus(host_select::Snapshot& snap, cJSON* params)
{
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, params)
    {
        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (!cJSON_IsNumber(id) || id->valueint < 0 || id->valueint >= 6)
            continue;
        auto& slot = snap.slots[id->valueint];
        cJSON* color = cJSON_GetObjectItemCaseSensitive(item, "c");
        cJSON* brightness = cJSON_GetObjectItemCaseSensitive(item, "b");
        cJSON* effect = cJSON_GetObjectItemCaseSensitive(item, "e");
        cJSON* sk = cJSON_GetObjectItemCaseSensitive(item, "sk");
        if (cJSON_IsNumber(color))
            slot.color = static_cast<uint32_t>(color->valuedouble) & 0xffffff;
        if (cJSON_IsNumber(brightness))
            slot.brightness = static_cast<float>(brightness->valuedouble);
        slot.effect = parse_effect(effect, slot.effect);
        slot.sync_keys = parse_sync_keys(sk);
        host_select::refresh_present(slot);
    }
}

void commit_host_selection(int slot, host_select::Reason reason, bool animate)
{
    if (slot < 0 || slot >= 6) return;
    if (model::state.selected != slot) {
        ui::select(slot, animate);
        ++model::state.host_activity_serial;
        std::printf("CCP_NATIVE|host_select|%d|%s\n", slot, host_select::reason_name(reason));
    }
    model::mark_done_viewed(slot);
}

bool transmit(Transport target, const char* json)
{
    if (!send_report || !json || target == Transport::None)
        return false;
    std::string wire(json);
    wire.push_back('\n');
    for (size_t offset = 0; offset < wire.size(); offset += kPayload) {
        uint8_t report[kBody] = {};
        const size_t count = std::min(kPayload, wire.size() - offset);
        report[0] = kRpcChannel;
        report[1] = static_cast<uint8_t>(count);
        std::memcpy(report + 2, wire.data() + offset, count);
        if (!send_report(target, report, sizeof(report)))
            return false;
    }
    return true;
}

void response(Transport target, cJSON* id, cJSON* result)
{
    cJSON* root = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "id", id ? cJSON_Duplicate(id, true) : cJSON_CreateNull());
    cJSON_AddItemToObject(root, "result", result ? result : cJSON_CreateObject());
    char* text = cJSON_PrintUnformatted(root);
    if (text) {
        transmit(target, text);
        cJSON_free(text);
    }
    cJSON_Delete(root);
}

bool apply_thread_lights(cJSON* params, session_sync::Tracker& session)
{
    if (!cJSON_IsArray(params))
        return false;

    // Codex publishes a transient all-off frame while rebuilding Micro lights
    // after model/reasoning controls and some focus changes. It is not a task
    // transition. Applying it would turn every slot Idle and make the restored
    // frame look like six fresh status events, replaying the whole deck after
    // an unrelated key such as D. Keep the last truthful task state, while the
    // all-off edge still participates in display power together with rgbcfg.
    const host_select::Snapshot previous = snapshot_from_tasks();
    host_select::Snapshot now = previous;
    overlay_thstatus(now, params);
    if (!host_select::any_lit(now)) {
        // Codex blanks all six when it closes a control surface. That is the
        // host saying the picker is gone, and it is the only honest way for the
        // device to know -- so the control page leaves on it rather than on a
        // timer of ours.
        if (ui::composer_control_active())
            ui::note_composer_control_closed();
        for (auto& task : model::state.tasks)
            task.lighting_interrupted = true;
        if (model::state.host_threads_enabled) {
            model::state.host_threads_enabled = false;
            ++model::state.host_lighting_serial;
        }
        std::printf("CCP_NATIVE|thstatus|all_off_power_only\n");
        return false;
    }
    const float brightest = host_select::brightest(now);
    if (brightest > 0.001f)
        model::state.host_brightness = brightest;

    // Model/reasoning surfaces temporarily repaint Micro's six lamps with UI
    // colours. They are presentation, not task state: feeding (for example) a
    // red picker preview into the status reducer falsely announces Error.
    // A leftover encoder guard must not swallow a real task snapshot: Codex
    // deduplicates lighting, so the chat-switch frame that arrived during the
    // guard would never be resent. Keep the picker path for the open control
    // page and for guarded frames that are not host lamp colours.
    const bool restoring = session.baseline(now_ms());
    const bool picker_preview = ui::composer_control_active()
        || (selection_guarded() && !host_select::looks_like_task_status(now));
    if (picker_preview) {
        if (!model::state.host_threads_enabled) {
            model::state.host_threads_enabled = true;
            ++model::state.host_lighting_serial;
        }
        ++model::state.host_activity_serial;
        // The frame is wrong as task state but it is the only truth the device
        // has about the surface the host is currently showing. Mirror it onto
        // the control page instead of dropping it: when the host repaints its
        // lamps because the dial moved, the screen moves with them.
        uint32_t preview_rgb[6] = {};
        float    preview_level[6] = {};
        for (int i = 0; i < 6; ++i) {
            preview_rgb[i] = now.slots[i].color;
            preview_level[i] = now.slots[i].brightness;
        }
        // The other way Codex signals a closed surface is to flash all six the
        // same white. A picker preview is never uniform -- it is showing a
        // value -- so a uniform lit frame is a close, not a state.
        bool uniform_white = true;
        for (int i = 0; i < 6; ++i) {
            const uint32_t c = preview_rgb[i];
            if (c != preview_rgb[0]
                || ((c >> 16) & 0xff) < 0xc0 || ((c >> 8) & 0xff) < 0xc0
                || (c & 0xff) < 0xc0) {
                uniform_white = false;
                break;
            }
        }
        if (uniform_white) {
            ui::note_composer_control_closed();
            std::printf("CCP_NATIVE|thstatus|picker_closed|uniform\n");
            return false;
        }
        ui::note_composer_control_preview();
        ui::note_composer_control_lamps(preview_rgb, preview_level);
        std::printf("CCP_NATIVE|thstatus|picker_preview_mirrored\n");
        return false;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, params)
    {
        cJSON* id = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (!cJSON_IsNumber(id) || id->valueint < 0 || id->valueint >= 6)
            continue;
        auto& task = model::state.tasks[id->valueint];
        cJSON* color = cJSON_GetObjectItemCaseSensitive(item, "c");
        cJSON* brightness = cJSON_GetObjectItemCaseSensitive(item, "b");
        cJSON* effect = cJSON_GetObjectItemCaseSensitive(item, "e");
        cJSON* speed = cJSON_GetObjectItemCaseSensitive(item, "s");
        status_reducer::LampFrame frame{task.color, task.brightness, task.effect_speed,
                                        task.effect};
        if (cJSON_IsNumber(color))
            frame.color = static_cast<uint32_t>(color->valuedouble) & 0xffffff;
        if (cJSON_IsNumber(brightness))
            frame.brightness = static_cast<float>(brightness->valuedouble);
        frame.effect = parse_effect(effect, frame.effect);
        if (cJSON_IsNumber(speed))
            frame.speed = static_cast<float>(speed->valuedouble);
        const status_reducer::Result reduced =
            status_reducer::apply(task, frame, restoring);

        if (reduced.changed) {
            ++model::state.host_activity_serial;
            // Worth the whole screen. Only a real change raises it: the app
            // republishes the same lighting every couple of seconds, and a panel
            // that flashed on every poll would be unreadable.
            if (task.present) {
                model::queue_announcement(id->valueint, task.status, reduced.before,
                                          reduced.event_green, reduced.target_unseen,
                                          reduced.was_unseen, now_ms());
            }
            // Completion removes the selected Micro lamp's breath (and Codex
            // may shortly turn that lamp off altogether). At that point the
            // light payload no longer carries enough information to rediscover
            // selection. Use the cursor we already synchronized while the task
            // was running: the completion animation remains green because of
            // completion_hold, then settles to viewed grey.
            // The selection guard only suppresses host-to-device cursor
            // inference while a local selection settles. It must never hide a
            // completion of the slot that is already selected locally: fast
            // tasks can finish inside that guard window and would otherwise
            // remain unread green forever.
            if (task.status == model::Status::Done && id->valueint == model::state.selected) {
                model::mark_done_viewed(id->valueint);
                std::printf("CCP_NATIVE|selected_completion_viewed|%d\n", id->valueint);
            }
        }
    }
    model::state.task_count = 6;
    bool any_thread_enabled = false;
    for (int i = 0; i < 6; ++i)
        any_thread_enabled = any_thread_enabled || model::state.tasks[i].present;
    if (model::state.host_threads_enabled != any_thread_enabled) {
        model::state.host_threads_enabled = any_thread_enabled;
        ++model::state.host_lighting_serial;
    }
    // Desktop deduplicates identical light payloads, so receiving thstatus is a
    // meaningful host event rather than a poll heartbeat.
    ++model::state.host_activity_serial;

    const host_select::Result inferred = host_select::infer(now, &previous);
    if (inferred.slot >= 0)
        commit_host_selection(inferred.slot, inferred.reason, !restoring);
    // Read state must be self-healing, not edge-triggered. A completion can
    // arrive as restoration after a transient all-off frame, in which case
    // reduced.changed is deliberately false. Every subsequent authoritative
    // live snapshot still confirms that the locally selected completed task is
    // being viewed, so a missed transition cannot leave it green forever.
    if (!restoring)
        model::mark_done_viewed(model::state.selected);
    return true;
}

void apply_lighting_config(cJSON* params)
{
    if (!cJSON_IsObject(params))
        return;
    float brightest = 0.f;
    bool any_enabled = false;
    for (const char* name : {"ambient", "keys"}) {
        cJSON* side = cJSON_GetObjectItemCaseSensitive(params, name);
        if (!cJSON_IsObject(side))
            continue;
        cJSON* brightness = cJSON_GetObjectItemCaseSensitive(side, "b");
        cJSON* effect = cJSON_GetObjectItemCaseSensitive(side, "e");
        const float value = cJSON_IsNumber(brightness)
                                ? std::clamp(static_cast<float>(brightness->valuedouble), 0.f, 1.f)
                                : 0.f;
        brightest = std::max(brightest, value);
        any_enabled =
            any_enabled || (value > 0.001f && (!cJSON_IsNumber(effect) || effect->valueint != 0));
    }
    model::state.host_lighting_seen = true;
    if (model::state.host_zones_enabled != any_enabled) {
        model::state.host_zones_enabled = any_enabled;
        ++model::state.host_lighting_serial;
    }
    // Keep the last non-zero slider value through an Auto-dim/off frame.
    if (brightest > 0.001f)
        model::state.host_brightness = brightest;
    std::printf("CCP_NATIVE|rgbcfg|brightness=%.3f|enabled=%d\n", static_cast<double>(brightest),
                any_enabled ? 1 : 0);
}

void note_valid_rpc(Transport source)
{
    last_valid_for(source).store(now_ms(), std::memory_order_release);
    const Transport selected = active_transport();
    if (selected == source && presentation_owner != source) {
        presentation_owner = source;
        reset_presentation_sync(source == Transport::Usb ? "usb_takeover" : "ble_takeover");
        session_for(source).begin();
        std::printf("CCP_NATIVE|transport|authoritative|source=%s\n", transport_name(source));
    }
}

void handle(Transport source, const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) {
        const char* error = cJSON_GetErrorPtr();
        std::printf("CCP_NATIVE|rpc|parse_error|source=%s|bytes=%u|offset=%u\n",
                    transport_name(source), static_cast<unsigned>(std::strlen(json)),
                    error ? static_cast<unsigned>(error - json) : 0U);
        return;
    }
    cJSON* method = cJSON_GetObjectItemCaseSensitive(root, "method");
    cJSON* params = cJSON_GetObjectItemCaseSensitive(root, "params");
    cJSON* id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (!cJSON_IsString(method)) {
        cJSON_Delete(root);
        return;
    }
    // A mounted HID interface only proves that a cable or BLE link exists.
    // A valid Codex RPC proves that this specific companion session is alive.
    hostlink::note_host_activity();
    note_valid_rpc(source);
    const bool owns_state = active_transport() == source && presentation_owner == source;
    auto& session = session_for(source);
    std::printf("CCP_NATIVE|rpc|source=%s|owner=%d|%s\n", transport_name(source),
                owns_state ? 1 : 0, method->valuestring);

    cJSON* result = cJSON_CreateObject();
    if (std::strcmp(method->valuestring, "sys.version") == 0) {
        cJSON_AddStringToObject(result, "version", "1.0.0-cardputer-adv");
    } else if (std::strcmp(method->valuestring, "device.status") == 0) {
        cJSON_AddStringToObject(result, "version", "1.0.0-cardputer-adv");
        cJSON_AddNumberToObject(result, "profile_index", model::state.ble_profile);
        cJSON_AddNumberToObject(result, "layer_index", 0);
        cJSON_AddNumberToObject(result, "battery", std::max(0, model::state.battery));
        cJSON_AddBoolToObject(result, "is_charging", model::state.charging);
        if (owns_state)
            session.note(session_sync::Method::DeviceStatus, now_ms());
    } else if (std::strcmp(method->valuestring, "v.oai.thstatus") == 0) {
        if (owns_state && apply_thread_lights(params, session))
            session.note(session_sync::Method::ThreadStatus, now_ms());
        cJSON_AddBoolToObject(result, "ok", true);
    } else if (std::strcmp(method->valuestring, "v.oai.rgbcfg") == 0) {
        if (owns_state) {
            apply_lighting_config(params);
            session.note(session_sync::Method::LightingConfig, now_ms());
        }
        cJSON_AddBoolToObject(result, "ok", true);
    } else if (std::strcmp(method->valuestring, "lights.preview") == 0) {
        if (owns_state) {
            extend_selection_guard(2500);
            ui::note_composer_control_preview();
        }
        cJSON_AddBoolToObject(result, "ok", true);
    } else if (std::strcmp(method->valuestring, "host.focused_app") == 0) {
        cJSON_AddBoolToObject(result, "ok", true);
    } else {
        cJSON_Delete(result);
        cJSON* error = cJSON_CreateObject();
        cJSON_AddNumberToObject(error, "code", -32601);
        cJSON_AddStringToObject(error, "message", "Method not found");
        cJSON* out = cJSON_CreateObject();
        cJSON_AddItemToObject(out, "id", id ? cJSON_Duplicate(id, true) : cJSON_CreateNull());
        cJSON_AddItemToObject(out, "error", error);
        char* text = cJSON_PrintUnformatted(out);
        if (text) {
            transmit(source, text);
            cJSON_free(text);
        }
        cJSON_Delete(out);
        cJSON_Delete(root);
        return;
    }
    response(source, id, result);
    cJSON_Delete(root);
}

void service_report(const InboundReport& report)
{
    if (report.source == Transport::None)
        return;
    if (report.epoch != epoch_for(report.source).load(std::memory_order_acquire)) {
        std::printf("CCP_NATIVE|rx|stale|source=%s\n", transport_name(report.source));
        return;
    }
    auto& input = input_for(report.source);
    const rpc_framer::FeedResult result = input.feed(
        report.body, report.length,
        [source = report.source](const std::string& message) { handle(source, message.c_str()); });
    if (result == rpc_framer::FeedResult::Invalid) {
        const unsigned count = report.length >= 2 ? report.body[1] : 0;
        std::printf("CCP_NATIVE|rx|invalid|source=%s|length=%u|count=%u\n",
                    transport_name(report.source), static_cast<unsigned>(report.length), count);
    } else if (result == rpc_framer::FeedResult::Oversize) {
        std::printf("CCP_NATIVE|rx|oversize|source=%s|max=%u\n", transport_name(report.source),
                    static_cast<unsigned>(rpc_framer::kMaxJson));
    }
}

} // namespace

void init(SendReport sender)
{
    send_report = sender;
    if (!inbound_queue) {
        inbound_queue = xQueueCreateStatic(kInboundCapacity, sizeof(InboundReport),
                                           inbound_queue_storage, &inbound_queue_state);
    } else {
        xQueueReset(inbound_queue);
    }
    begin_session_sync();
}

bool enqueue_report(Transport source, const uint8_t* body, size_t length)
{
    if (!inbound_queue || transport_index(source) < 0 || !body || length < 2 || length > kBody)
        return false;
    InboundReport report;
    report.source = source;
    report.epoch = epoch_for(source).load(std::memory_order_acquire);
    report.length = static_cast<uint8_t>(length);
    std::memcpy(report.body, body, length);
    if (xQueueSend(inbound_queue, &report, 0) == pdTRUE)
        return true;
    std::printf("CCP_NATIVE|rx|queue_full|source=%s\n", transport_name(source));
    return false;
}

void service()
{
    if (!inbound_queue)
        return;
    apply_pending_reset(Transport::Usb);
    apply_pending_reset(Transport::Ble);
    InboundReport report;
    while (xQueueReceive(inbound_queue, &report, 0) == pdTRUE)
        service_report(report);
    const Transport selected = active_transport();
    if (selected != Transport::None && presentation_owner != selected) {
        presentation_owner = selected;
        reset_presentation_sync(selected == Transport::Usb ? "usb_liveness_takeover"
                                                           : "ble_liveness_takeover");
        session_for(selected).begin();
        std::printf("CCP_NATIVE|transport|authoritative|source=%s|reason=liveness\n",
                    transport_name(selected));
    }
}

void reset_transport(Transport source)
{
    if (transport_index(source) < 0)
        return;
    epoch_for(source).fetch_add(1, std::memory_order_acq_rel);
    last_valid_for(source).store(0, std::memory_order_release);
    reset_pending_for(source).store(true, std::memory_order_release);
}

bool session_alive(Transport source)
{
    if (transport_index(source) < 0)
        return false;
    const uint32_t stamp = last_valid_for(source).load(std::memory_order_acquire);
    return stamp != 0 && now_ms() - stamp <= kSessionIdleMs;
}

Transport active_transport()
{
    if (session_alive(Transport::Usb))
        return Transport::Usb;
    if (session_alive(Transport::Ble))
        return Transport::Ble;
    return Transport::None;
}

const char* transport_name(Transport source)
{
    switch (source) {
    case Transport::Usb:
        return "usb";
    case Transport::Ble:
        return "ble";
    default:
        return "none";
    }
}

void begin_session_sync()
{
    presentation_owner = Transport::None;
    reset_presentation_sync("explicit");
}

void suppress_host_selection(uint32_t duration_ms)
{
    extend_selection_guard(duration_ms);
}

bool send_key_to(Transport target, const char* key, int action, int agent)
{
    if (!key || target == Transport::None)
        return false;
    char json[160];
    if (agent >= 0) {
        std::snprintf(json, sizeof(json),
                      "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%d,\"ag\":%d}}",
                      key, action, agent);
    } else {
        std::snprintf(json, sizeof(json),
                      "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\",\"act\":%d}}", key,
                      action);
    }
    const bool sent = transmit(target, json);
    std::printf("CCP_NATIVE|key|transport=%s|%s|action=%d|agent=%d|sent=%d\n",
                transport_name(target), key, action, agent, sent ? 1 : 0);
    return sent;
}

bool send_joystick_to(Transport target, float angle, float distance)
{
    if (target == Transport::None)
        return false;
    char json[128];
    std::snprintf(json, sizeof(json),
                  "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%.3f,\"d\":%.3f}}", angle, distance);
    return transmit(target, json);
}

bool send_key(const char* key, int action, int agent)
{
    return send_key_to(active_transport(), key, action, agent);
}

bool send_joystick(float angle, float distance)
{
    return send_joystick_to(active_transport(), angle, distance);
}

} // namespace codex_micro
