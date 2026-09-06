// Codex Microputer ADV firmware.
//
// The device is a native Codex Micro HID controller. USB serial remains a
// diagnostic path for synthetic UI and hardware tests.

#include <M5Unified.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "audio.h"
#include "keys.h"
#include "ble_transport.h"
#include "usb_transport.h"
#include "codex_micro_protocol.h"
#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_vfs_fat.h"
#include "firmware.h"
#include "lamp.h"
#include "link.h"
#include "model.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"
#include "storage_partition.h"
#include "store.h"
#include "theme.h"
#include "ui.h"

namespace model {
State state;
}

namespace {

const char* firmware_version() { return firmware::version(); }

model::Task staged[model::kMaxTasks];
bool staged_present[model::kMaxTasks] = {};
int staged_count = 0;
uint32_t staged_started_ms = 0;
constexpr uint32_t kStagedBatchTimeoutMs = 2000;
bool exit_armed = false;
bool ble_reset_armed = false;
struct VoiceGesture {
    bool active = false;
    bool agent_key_down = false;
    int agent = -1;
    codex_micro::Transport transport = codex_micro::Transport::None;
};
VoiceGesture voice_gesture;
codex_micro::Transport agent_key_transport[model::kMaxTasks] = {};
codex_micro::Transport native_action_transport[13] = {};
codex_micro::Transport encoder_press_transport = codex_micro::Transport::None;
uint32_t joystick_release_ms = 0;
bool joystick_deflected = false;
codex_micro::Transport joystick_transport = codex_micro::Transport::None;

using keys::Key;
using keys::Press;

// Fold unsupported punctuation to ASCII without cutting UTF-8 sequences.
void sanitize_utf8(const char* in, char* out, int out_size)
{
    if (!out || out_size <= 0)
        return;
    out[0] = 0;
    if (!in)
        return;
    struct Fold {
        const char* from;
        const char* to;
    };
    static const Fold folds[] = {
        {"\xE2\x80\x94", "-"},  {"\xE2\x80\x93", "-"},   {"\xE2\x80\x95", "-"},
        {"\xE2\x88\x92", "-"},  {"\xE2\x80\xA6", "..."}, {"\xE2\x80\x9C", "\""},
        {"\xE2\x80\x9D", "\""}, {"\xE2\x80\x98", "'"},   {"\xE2\x80\x99", "'"},
        {"\xC2\xAB", "\""},     {"\xC2\xBB", "\""},      {"\xE2\x86\x92", "->"},
        {"\xE2\x86\x90", "<-"}, {"\xE2\x80\xA2", "*"},   {"\xC2\xB7", "*"},
        {"\xC2\xA0", " "},      {"\xE2\x80\xAF", " "}};
    int written = 0;
    for (const char* p = in; *p && written < out_size - 1;) {
        const Fold* hit = nullptr;
        for (const auto& f : folds) {
            size_t n = std::strlen(f.from);
            if (std::strncmp(p, f.from, n) == 0) {
                hit = &f;
                break;
            }
        }
        if (hit) {
            int n = static_cast<int>(std::strlen(hit->to));
            if (written + n >= out_size)
                break;
            std::memcpy(out + written, hit->to, n);
            written += n;
            p += std::strlen(hit->from);
            continue;
        }
        uint8_t lead = static_cast<uint8_t>(*p);
        int n = 1;
        if ((lead & 0xE0u) == 0xC0u)
            n = 2;
        else if ((lead & 0xF0u) == 0xE0u)
            n = 3;
        else if ((lead & 0xF8u) == 0xF0u)
            n = 4;
        else if ((lead & 0x80u) != 0)
            n = 0;
        bool valid = n > 0;
        for (int i = 1; valid && i < n; ++i)
            valid = p[i] && (static_cast<uint8_t>(p[i]) & 0xC0u) == 0x80u;
        if (!valid) {
            out[written++] = '?';
            ++p;
            continue;
        }
        if (written + n >= out_size)
            break;
        std::memcpy(out + written, p, n);
        written += n;
        p += n;
    }
    out[written] = 0;
}
bool parse_int_field(const char* text, int min, int max, int& value)
{
    if (!text || !*text)
        return false;
    errno = 0;
    char* end = nullptr;
    long v = std::strtol(text, &end, 10);
    if (errno || end == text || !end || *end || v < min || v > max)
        return false;
    value = static_cast<int>(v);
    return true;
}
bool parse_status(const char* text, model::Status& status)
{
    if (!text)
        return false;
    if (!std::strcmp(text, "RUN") || !std::strcmp(text, "RUNNING"))
        status = model::Status::Running;
    else if (!std::strcmp(text, "INPUT") || !std::strcmp(text, "APPROVAL") ||
             !std::strcmp(text, "APPROVE"))
        status = model::Status::NeedsInput;
    else if (!std::strcmp(text, "DONE") || !std::strcmp(text, "COMPLETE") ||
             !std::strcmp(text, "COMPLETED"))
        status = model::Status::Done;
    else if (!std::strcmp(text, "ERROR"))
        status = model::Status::Error;
    else if (!std::strcmp(text, "IDLE"))
        status = model::Status::Idle;
    else
        return false;
    return true;
}
uint32_t diagnostic_color(model::Status status)
{
    switch (status) {
    case model::Status::Running:
        return lamp::kRunning;
    case model::Status::NeedsInput:
        return lamp::kNeedsInput;
    case model::Status::Done:
        return lamp::kDoneSeen;
    case model::Status::Error:
        return lamp::kError;
    case model::Status::Idle:
        return 0;
    }
    return 0;
}
void reset_staged_tasks()
{
    for (auto& task : staged)
        task = model::Task{};
    std::memset(staged_present, 0, sizeof(staged_present));
    staged_count = 0;
    staged_started_ms = 0;
}
bool staged_batch_complete(int count)
{
    for (int i = 0; i < count; ++i)
        if (!staged_present[i])
            return false;
    return true;
}
void service_staged_timeout()
{
    if (!staged_started_ms)
        return;
    uint32_t age = lgfx::millis() - staged_started_ms;
    if (age <= kStagedBatchTimeoutMs)
        return;
    hostlink::sendf("CCP_DECK|ERROR|incomplete_timeout|age_ms=%u\n", (unsigned)age);
    reset_staged_tasks();
}

// Two different statements, deliberately kept apart:
//   CCP_CFG    - the user changed something here; the host should apply it.
//   CCP_CFGACK - we adopted what the host sent; informational only.
// Collapsing them into one line makes the host act on its own echo, which
// rewrites the user's Codex config on every reconnect.
void publish_settings(bool user_initiated)
{
    const auto& s = model::state;
    hostlink::sendf("%s|%s|%s|%s|%s\n", user_initiated ? "CCP_CFG" : "CCP_CFGACK",
                    s.models.current_wire(), s.efforts.current_wire(), s.speeds.current_wire(),
                    s.sound_volume > 0 ? "on" : "off");
}

// ------------------------------------------------------------ encoder gestures
// The Codex app listens for the Micro's dial on the same v.oai.hid channel as
// the keys: rotation arrives as ENC_CW / ENC_CC with act 2, and the dial itself
// as ENC with act 1 then 0. Meaning belongs entirely to Codex's current dial
// mode and focused composer control; the firmware must never reinterpret a
// detent as reasoning effort.
bool send_encoder_step(bool right)
{
    return companion_codex_key(right ? "ENC_CC" : "ENC_CW", 2);
}

// Preserve the physical down edge. keys.cpp emits the matching release, so
// Codex can distinguish a click from the long press that opens device settings.
bool send_encoder_press_to(codex_micro::Transport target)
{
    codex_micro::suppress_host_selection(2000);
    return codex_micro::send_key_to(target, "ENC", 1);
}
bool send_native_action_to(codex_micro::Transport target, int slot, bool down, int agent = -1)
{
    if ((slot < 6 || slot > 12) && slot != 1011)
        return false;
    char key[12];
    if (slot == 1011)
        std::snprintf(key, sizeof(key), "ACT10_ACT11");
    else
        std::snprintf(key, sizeof(key), "ACT%02d", slot);
    return codex_micro::send_key_to(target, key, down ? 1 : 0, agent);
}
bool send_agent_key_to(codex_micro::Transport target, int slot, bool down)
{
    if (slot < 0 || slot >= model::kMaxTasks || target == codex_micro::Transport::None)
        return false;
    char key[5];
    std::snprintf(key, sizeof(key), "AG%02d", slot);
    return codex_micro::send_key_to(target, key, down ? 1 : 0, slot);
}
void release_voice_gesture()
{
    if (voice_gesture.transport != codex_micro::Transport::None) {
        if (voice_gesture.active)
            codex_micro::send_key_to(voice_gesture.transport, "ACT10", 0, voice_gesture.agent);
        if (voice_gesture.agent_key_down)
            send_agent_key_to(voice_gesture.transport, voice_gesture.agent, false);
    }
    int agent = voice_gesture.agent;
    voice_gesture = VoiceGesture{};
    ui::set_voice_active(false, std::max(0, agent));
}

void toggle_sound()
{
    auto& s = model::state;
    if (s.sound_volume > 0) {
        audio::play(audio::Cue::Select);
        s.unmuted_volume = s.sound_volume;
        s.sound_volume = 0;
    } else {
        s.sound_volume = std::max<uint8_t>(10, s.unmuted_volume);
        audio::apply_volume();
        audio::play(audio::Cue::Unmute);
    }
    store::save_settings();
    publish_settings(true);
    ui::toast(s.sound_volume > 0 ? "SOUND ON" : "MUTED", "", theme::kInk);
    ui::invalidate();
}

void adjust_volume(int delta)
{
    auto& s = model::state;
    const uint8_t previous = s.sound_volume;
    const int next = std::clamp(static_cast<int>(s.sound_volume) + delta * 10, 0, 100);
    s.sound_volume = static_cast<uint8_t>(next);
    if (next > 0)
        s.unmuted_volume = s.sound_volume;
    audio::apply_volume();
    // Audition the value itself, not the old setting: the same short neutral
    // sound at every step makes adjacent levels directly comparable.
    if (s.sound_volume != previous)
        audio::play(audio::Cue::Select);
    store::save_settings();
    publish_settings(true);
    char value[12];
    std::snprintf(value, sizeof(value), "%d%%", next);
    ui::toast("VOLUME", value, theme::kInk);
    ui::invalidate();
}

void toggle_startup_sound()
{
    auto& s = model::state;
    s.startup_sound_on = !s.startup_sound_on;
    store::save_settings();
    ui::toast(s.startup_sound_on ? "STARTUP CHIME ON" : "STARTUP CHIME OFF", "", theme::kInk);
    audio::play(audio::Cue::Select);
    ui::invalidate();
}

bool send_joystick_impulse(Key key)
{
    // Codex consumes normalized screen-space polar coordinates: zero points
    // right and angles advance clockwise because positive Y points down.
    float angle = 0.f;
    switch (key) {
    case Key::Right:
        angle = 0.00f;
        break;
    case Key::Down:
        angle = 0.25f;
        break;
    case Key::Left:
        angle = 0.50f;
        break;
    case Key::Up:
        angle = 0.75f;
        break;
    default:
        return false;
    }
    const auto target = codex_micro::active_transport();
    if (target == codex_micro::Transport::None)
        return false;
    if (joystick_deflected && joystick_transport != target)
        codex_micro::send_joystick_to(joystick_transport, 0.f, 0.f);
    if (!codex_micro::send_joystick_to(target, angle, 1.f))
        return false;
    joystick_transport = target;
    joystick_deflected = true;
    joystick_release_ms = lgfx::millis() + 85;
    return true;
}

// Commit the staged list, then compare against what was on screen so newly
// finished work can announce itself.
void commit_tasks(int selected_hint)
{
    auto& s = model::state;

    model::Task previous[model::kMaxTasks];
    const int previous_count = s.task_count;
    std::memcpy(previous, s.tasks, sizeof(previous));

    int newly_done = 0;
    int newly_input = 0;
    const char* headline = nullptr;
    for (int i = 0; i < staged_count; ++i) {
        // Carry the unseen flag across refreshes, keyed by thread id rather than
        // slot, because the host reorders by recency.
        const model::Task* before = nullptr;
        for (int j = 0; j < previous_count; ++j) {
            if (std::strcmp(previous[j].id, staged[i].id) == 0) {
                before = &previous[j];
                break;
            }
        }
        if (before) {
            staged[i].unseen_done = before->unseen_done;
            staged[i].completion_hold = before->completion_hold;
            if (before->status != model::Status::Done && staged[i].status == model::Status::Done) {
                staged[i].unseen_done = true;
                staged[i].completion_hold = true;
                // TASK has no separate lamp frame. Use the native unread
                // completion colour so the animation finalizer can preserve
                // background unread state and settle a selected task locally.
                staged[i].color = lamp::kDoneUnseen;
                ++newly_done;
                if (!headline)
                    headline = staged[i].title;
            }
            if (staged[i].status != model::Status::Done) {
                staged[i].unseen_done = false;
                staged[i].completion_hold = false;
            }
            if (before->status != model::Status::NeedsInput &&
                staged[i].status == model::Status::NeedsInput) {
                ++newly_input;
                if (!headline)
                    headline = staged[i].title;
            }
            if (before->status != staged[i].status && i < theme::kCellCount) {
                model::queue_announcement(i, staged[i].status, before->status,
                                          staged[i].unseen_done, staged[i].unseen_done,
                                          before->unseen_done, lgfx::millis());
            }
        } else if (staged[i].status == model::Status::Done && previous_count > 0) {
            // A thread that arrives already-done is new to us but not news.
            staged[i].unseen_done = false;
        }
    }

    std::memcpy(s.tasks, staged, sizeof(s.tasks));
    s.task_count = staged_count;
    if (selected_hint >= 0 && selected_hint < staged_count)
        s.selected = selected_hint;
    // Diagnostic TASK batches exercise the same read-state contract as native
    // Codex lighting: a selected completion stays green through its takeover,
    // then settles to viewed grey. Background completions remain unread.
    model::mark_done_viewed(s.selected);
    ui::select(s.selected, true);
    ui::relayout();

    if (newly_input > 0) {
        ui::toast("NEEDS INPUT", headline ? headline : "", theme::kInput);
    } else if (newly_done > 0) {
        char title[24];
        if (newly_done == 1)
            std::snprintf(title, sizeof(title), "TASK DONE");
        else
            std::snprintf(title, sizeof(title), "%d TASKS DONE", newly_done);
        ui::toast(title, headline ? headline : "", theme::kDone);
    }
    ui::invalidate();
}

void enable_m5apps_autostart()
{
    nvs_handle_t handle = 0;
    const char* partition = storage_partition::nvs_label();
    esp_err_t err = partition
        ? nvs_open_from_partition(partition, "system", NVS_READWRITE, &handle)
        : ESP_ERR_NOT_FOUND;
    if (err == ESP_OK)
        err = nvs_set_u8(handle, "last_app", 1);
    if (err == ESP_OK)
        err = nvs_set_i32(handle, "last_app_to", 2);
    if (err == ESP_OK)
        err = nvs_commit(handle);
    if (handle)
        nvs_close(handle);
    hostlink::emit("m5apps_autostart", err == ESP_OK, esp_err_to_name(err));
}

void return_to_m5apps()
{
    const esp_partition_t* factory = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
    if (!factory) {
        hostlink::emit("return_to_m5apps", false, "factory_not_found");
        return;
    }
    const esp_err_t err = esp_ota_set_boot_partition(factory);
    hostlink::emit("return_to_m5apps", err == ESP_OK, esp_err_to_name(err));
    if (err == ESP_OK) {
        // Settings changed within the write debounce would otherwise be lost.
        store::flush();
        vTaskDelay(pdMS_TO_TICKS(120));
        esp_restart();
    }
}

void send_screenshot(const char* scene)
{
    const uint16_t* pixels = ui::capture_frame(scene);
    if (!pixels) {
        char error[96];
        const int length = std::snprintf(error, sizeof(error), "CCP_SHOT|ERROR|unknown_scene|%s\n",
                                         scene ? scene : "");
        hostlink::send_usb(error, static_cast<size_t>(std::max(0, length)));
        return;
    }

    constexpr int kPixels = theme::kScreenW * theme::kScreenH;
    constexpr int kChunkPixels = 120;
    char line[32 + kChunkPixels * 4];
    hostlink::begin_usb_bulk();
    int length = std::snprintf(line, sizeof(line), "CCP_SHOT|BEGIN|%s|%d|%d|RGB565|%d\n", scene,
                               theme::kScreenW, theme::kScreenH, kPixels);
    hostlink::send_usb(line, static_cast<size_t>(length));

    uint32_t checksum = 2166136261u;
    int sequence = 0;
    for (int offset = 0; offset < kPixels; offset += kChunkPixels, ++sequence) {
        const int count = std::min(kChunkPixels, kPixels - offset);
        int used = std::snprintf(line, sizeof(line), "CCP_SHOT|DATA|%d|", sequence);
        for (int i = 0; i < count; ++i) {
            const uint16_t value = pixels[offset + i];
            checksum = (checksum ^ static_cast<uint8_t>(value >> 8)) * 16777619u;
            checksum = (checksum ^ static_cast<uint8_t>(value)) * 16777619u;
            used += std::snprintf(line + used, sizeof(line) - used, "%04X", value);
        }
        line[used++] = '\n';
        hostlink::send_usb(line, static_cast<size_t>(used));
        if ((sequence & 7) == 7)
            vTaskDelay(pdMS_TO_TICKS(1));
    }
    length = std::snprintf(line, sizeof(line), "CCP_SHOT|END|%08lX\n",
                           static_cast<unsigned long>(checksum));
    hostlink::send_usb(line, static_cast<size_t>(length));
    hostlink::end_usb_bulk();
}

// ============================================================== input actions
void handle_press(const Press& press)
{
    auto& s = model::state;
    if (press.key == Key::None)
        return;
    // Developer previews are inert captures of real rendering paths. Escape is
    // their only control, so testing a screen cannot accidentally operate Codex.
    if (ui::developer_preview_active()) {
        if (press.down && press.key == Key::Back)
            ui::close_developer_preview();
        return;
    }
    // Host actions stay disabled without Codex, but local settings remain
    // reachable from the splash. Tab returns to the splash, never to a fake
    // task deck; Opt+Tab keeps the same rule for diagnostics.
    if (s.link == model::Link::Offline) {
        if (press.key == Key::Settings) {
            ui::go(ui::screen() == ui::Screen::Settings ? ui::Screen::Boot : ui::Screen::Settings);
            return;
        }
        if (press.key == Key::DebugSettings) {
            ui::go(ui::screen() == ui::Screen::DebugSettings ? ui::Screen::Boot
                                                             : ui::Screen::DebugSettings);
            return;
        }
        const auto screen = ui::screen();
        if (screen != ui::Screen::Settings && screen != ui::Screen::DebugSettings &&
            screen != ui::Screen::StatusDebug && screen != ui::Screen::ChimeLab)
            return;
    }

    // Native controls preserve their physical edge. Codex uses these releases
    // to distinguish an Agent Key tap from a double tap and an encoder click
    // from the long press that opens its own device configuration.
    if (!press.down) {
        if (press.key == Key::Digit) {
            int slot = (press.digit == 0 ? 10 : press.digit) - 1;
            if (slot >= 0 && slot < model::kMaxTasks) {
                auto target = agent_key_transport[slot];
                if (target != codex_micro::Transport::None)
                    send_agent_key_to(target, slot, false);
                agent_key_transport[slot] = codex_micro::Transport::None;
            }
        } else if (press.key == Key::NativeAction && press.digit >= 0 && press.digit < 13) {
            auto target = native_action_transport[press.digit];
            if (target != codex_micro::Transport::None)
                send_native_action_to(target, press.digit, false, s.selected);
            native_action_transport[press.digit] = codex_micro::Transport::None;
        } else if (press.key == Key::Enter) {
            // Only the confirm press claims a transport, so an Enter that
            // submitted the composer releases nothing here.
            if (encoder_press_transport != codex_micro::Transport::None)
                codex_micro::send_key_to(encoder_press_transport, "ENC", 0);
            encoder_press_transport = codex_micro::Transport::None;
        }
        return;
    }

    // Modal UI owns physical arrow navigation before global hardware
    // assignments. Brackets remain exclusively encoder detents everywhere.
    // Screen checks are their own list now. Every one of them renders a real
    // path with fake data, which is exactly the sort of thing that should not
    // share a menu with switches that change how the device behaves.
    if (ui::screen() == ui::Screen::Previews) {
        if (press.key == Key::Up || press.key == Key::Down) {
            ui::previews_move(press.key == Key::Down ? 1 : -1);
            audio::play(audio::Cue::Select);
            return;
        }
        if (press.key == Key::Back) {
            ui::go(ui::Screen::DebugSettings);
            audio::play(audio::Cue::Select);
            return;
        }
        if (press.key == Key::Enter) {
            switch (ui::previews_focus()) {
            case ui::PreviewsRow::Splash:
                ui::show_developer_preview(ui::DeveloperPreview::Splash); break;
            case ui::PreviewsRow::Pairing:
                ui::show_developer_preview(ui::DeveloperPreview::Pairing); break;
            case ui::PreviewsRow::Control:
                ui::show_developer_preview(ui::DeveloperPreview::Control); break;
            default:
                ui::show_developer_preview(ui::DeveloperPreview::Stick); break;
            }
        }
        return;
    }
    if (ui::screen() == ui::Screen::DebugSettings) {
        if (press.key == Key::Up) {
            ble_reset_armed = false;
            ui::debug_settings_move(-1);
            hostlink::sendf("CCP_MENU|debug|move=-1|focus=%d\n",
                            static_cast<int>(ui::debug_settings_focus()));
            audio::play(audio::Cue::Select);
            return;
        }
        if (press.key == Key::Down) {
            ble_reset_armed = false;
            ui::debug_settings_move(1);
            hostlink::sendf("CCP_MENU|debug|move=1|focus=%d\n",
                            static_cast<int>(ui::debug_settings_focus()));
            audio::play(audio::Cue::Select);
            return;
        }
        if (press.key == Key::Enter) {
            const ui::DebugSettingsRow row = ui::debug_settings_focus();
            if (row == ui::DebugSettingsRow::UsbHid) {
                ble_reset_armed = false;
                const bool requested = !s.usb_hid_enabled;
                s.usb_hid_enabled = requested;
                // Persist before removing USB so an unexpected cable event or
                // reset cannot resurrect a mode the user explicitly disabled.
                if (!store::flush()) {
                    s.usb_hid_enabled = !requested;
                    ui::toast("USB NOT SAVED", store::last_error_name(), theme::kError);
                    ui::invalidate();
                    return;
                }
                if (!companion_usb_set_enabled(requested)) {
                    s.usb_hid_enabled = !requested;
                    // The rollback itself must land in NVS, or a reboot would
                    // resurrect the mode whose activation just failed.
                    if (!store::flush())
                        ui::toast("USB NOT SAVED", store::last_error_name(),
                                  theme::kError);
                    else
                        ui::toast("USB HID ERROR", "State unchanged", theme::kError);
                } else {
                    ui::toast(requested ? "USB HID ON" : "BLUETOOTH ONLY",
                              requested ? "Codex Micro enabled" : "USB data disabled",
                              requested ? theme::kRun : theme::kInput);
                    audio::play(audio::Cue::Select);
                }
                ui::invalidate();
                return;
            }
            if (row == ui::DebugSettingsRow::ResetBluetooth) {
                if (!ble_reset_armed) {
                    ble_reset_armed = true;
                    ui::toast("RESET BLE BONDS?", "Press enter again", theme::kInput);
                    return;
                }
                ble_reset_armed = false;
                const esp_err_t reset_error = companion_ble_forget_bonds();
                if (reset_error != ESP_OK) {
                    ui::toast("BLE RESET FAILED", esp_err_to_name(reset_error), theme::kError);
                } else {
                    ui::toast("BLE BONDS CLEARED", "Forget device on Mac", theme::kDone);
                    audio::play(audio::Cue::Select);
                }
                ui::invalidate();
                return;
            }
            ble_reset_armed = false;
            if (row == ui::DebugSettingsRow::Previews) {
                ui::go(ui::Screen::Previews);
            } else {
                ui::go(row == ui::DebugSettingsRow::ChimeLab ? ui::Screen::ChimeLab
                                                             : ui::Screen::StatusDebug);
            }
            return;
        }
    }

    switch (press.key) {
    case Key::Mute:
        toggle_sound();
        break;

    case Key::Record:
        // Keyboard-generated Record has no release edge. Treat it as a
        // short native microphone gesture; the physical G0 button below
        // supplies true press/release push-to-talk.
        companion_codex_key("ACT10", 1, s.selected);
        companion_codex_key("ACT10", 0, s.selected);
        break;

    // These keys are the physical substitute for Codex Micro's dial. In
    // Composer navigation they move focus/options; in Reasoning only they
    // adjust effort; custom mappings may do something else entirely.
    case Key::EncoderLeft:
    case Key::EncoderRight: {
        const bool right = press.key == Key::EncoderRight;
        codex_micro::suppress_host_selection(2000);
        if (!send_encoder_step(right)) {
            ui::toast("DIAL", "no host", theme::kOrange);
            break;
        }
        audio::play(right ? audio::Cue::StepRight : audio::Cue::StepLeft);
        // Turning the dial is the request to see the surface. The no-host path
        // returns above, so a dial with nothing to drive still shows nothing.
        ui::notify_composer_control_step(right ? 1 : -1);
        break;
    }


    case Key::NativeAction: {
        // T..P are the six physical command slots ACT06..ACT11. Codex owns
        // their current command/Skill mapping exactly as it does for Micro.
        const auto target = codex_micro::active_transport();
        if (!send_native_action_to(target, press.digit, true, s.selected)) {
            if (press.digit >= 0 && press.digit < 13)
                native_action_transport[press.digit] = codex_micro::Transport::None;
            ui::toast("NO HOST", "action not sent", theme::kOrange);
            break;
        }
        if (press.digit >= 0 && press.digit < 13)
            native_action_transport[press.digit] = target;
        if (press.digit == 7)
            ui::toast("APPROVE", "sent to Codex", theme::kDone);
        else if (press.digit == 8)
            ui::toast("REJECT", "sent to Codex", theme::kInput);
        audio::play(audio::Cue::Select);
        break;
    }

    case Key::Other:
        break; // no action of its own; it already woke the panel

    case Key::Settings:
        ui::go(ui::screen() == ui::Screen::Settings ? ui::Screen::Deck : ui::Screen::Settings);
        break;

    case Key::DebugSettings:
        ui::go(ui::screen() == ui::Screen::DebugSettings ? ui::Screen::Deck
                                                         : ui::Screen::DebugSettings);
        break;

    case Key::Help:
        ui::go(ui::screen() == ui::Screen::Help ? ui::Screen::Deck : ui::Screen::Help);
        break;

    case Key::Back:
        // Esc is the way out of every surface on the device, so it has to work
        // on the host control page too. Micro's HID vocabulary has no escape,
        // so this is deliberately local: the page leaves and quarantines itself
        // against an immediate reopen, and nothing is claimed to the host.
        if (ui::stick_control_active()) {
            ui::dismiss_stick_control();
            std::printf("CCP_UI|stick|escape\n");
            audio::play(audio::Cue::Select);
            break;
        }
        if (ui::composer_control_active()) {
            // Micro has no escape key, so the way out of a control surface is
            // the one a hand would use on the hardware: press the agent key
            // that is lit. It returns focus to that chat, which is what makes
            // Codex drop the picker -- and the host's own close frame then
            // takes this page down, the same as if the surface had been
            // dismissed on the desktop. The local close is only the fallback
            // for when there is no host to answer.
            const auto target = codex_micro::active_transport();
            const bool sent = send_agent_key_to(target, s.selected, true)
                           && send_agent_key_to(target, s.selected, false);
            std::printf("CCP_UI|composer|escape|agent=%d|sent=%d\n",
                        s.selected, sent ? 1 : 0);
            if (!sent) ui::dismiss_composer_control_preview();
            audio::play(audio::Cue::Select);
            break;
        }
        // Back never leaves the app. Handing the device back to M5Apps is a
        // deliberate action from Settings, because it costs a reboot and
        // makes the companion unreachable until the user returns to it.
        ui::go((ui::screen() == ui::Screen::StatusDebug || ui::screen() == ui::Screen::ChimeLab)
                   ? ui::Screen::DebugSettings
                   : ui::Screen::Deck);
        break;

    default:
        break;
    }

    switch (ui::screen()) {
    case ui::Screen::Settings:
        if (press.key == Key::Up)
            ui::settings_move(-1);
        if (press.key == Key::Down)
            ui::settings_move(1);
        if (ui::settings_focus() == ui::SettingsRow::BleProfile &&
            (press.key == Key::Left || press.key == Key::Right)) {
            auto& profile = model::state.ble_profile;
            profile = static_cast<uint8_t>((profile + (press.key == Key::Right ? 1 : 2)) % 3);
            const uint8_t previous_profile =
                static_cast<uint8_t>((profile + (press.key == Key::Right ? 2 : 1)) % 3);
            if (!store::flush()) {
                profile = previous_profile;
                ui::toast("SETTINGS ERROR", "Host channel not saved", theme::kError);
                return;
            }
            const bool switching = companion_ble_select_profile(profile);
            ui::toast(switching ? "HOST CHANNEL" : "BLE SWITCH FAILED",
                      switching ? "Connecting" : "Try again", theme::kInput);
        }
        if (ui::settings_focus() == ui::SettingsRow::Volume &&
            (press.key == Key::Left || press.key == Key::Right))
            adjust_volume(press.key == Key::Right ? 1 : -1);
        if (press.key == Key::Enter) {
            if (ui::settings_focus() == ui::SettingsRow::Exit) {
                if (exit_armed) {
                    return_to_m5apps();
                } else {
                    exit_armed = true;
                    ui::toast("EXIT TO M5APPS?", "Press enter again", theme::kInput);
                }
            } else if (ui::settings_focus() == ui::SettingsRow::Volume) {
                adjust_volume(1);
            } else if (ui::settings_focus() == ui::SettingsRow::StartupSound) {
                toggle_startup_sound();
            }
        }
        if (press.key != Key::Enter)
            exit_armed = false;
        break;

    case ui::Screen::DebugSettings:
        break;

    case ui::Screen::Previews:
        break;

    case ui::Screen::StatusDebug:
        if (press.key == Key::Up)
            ui::debug_move(-1);
        if (press.key == Key::Down)
            ui::debug_move(1);
        if ((press.key == Key::Left && ui::debug_adjust(-1)) ||
            (press.key == Key::Right && ui::debug_adjust(1)))
            store::save_settings();
        if (press.key == Key::Enter && ui::debug_run())
            store::save_settings();
        break;

    case ui::Screen::ChimeLab:
        if (press.key == Key::Left)
            ui::chime_move(-1, 0);
        if (press.key == Key::Right)
            ui::chime_move(1, 0);
        if (press.key == Key::Up)
            ui::chime_move(0, -1);
        if (press.key == Key::Down)
            ui::chime_move(0, 1);
        if (press.key == Key::Left || press.key == Key::Right || press.key == Key::Up ||
            press.key == Key::Down)
            store::save_settings();
        if (press.key == Key::Enter)
            audio::play(audio::Cue::Boot);
        break;

    case ui::Screen::Deck:
    default:
        if (press.key == Key::Up || press.key == Key::Right || press.key == Key::Down ||
            press.key == Key::Left) {
            if (!send_joystick_impulse(press.key))
                ui::toast("NO HOST", "stick not sent", theme::kOrange);
            // Not while the dial owns the screen: that page is about a host
            // surface the arrows have nothing to do with, and stacking a
            // second instrument over it would take it down mid-selection.
            if (!ui::composer_control_active()) {
                ui::notify_stick_step(press.key == Key::Right ? 0
                                    : press.key == Key::Down  ? 1
                                    : press.key == Key::Left  ? 2 : 3);
                // Right and up rise, left and down fall. The dial's detents
                // already speak that way, and an instrument page with no
                // sound at all reads as one that did not register the press.
                const bool rising = press.key == Key::Right || press.key == Key::Up;
                audio::play(rising ? audio::Cue::StepRight : audio::Cue::StepLeft);
            }
        }
        if (press.key == Key::Digit) {
            const int slot = (press.digit == 0 ? 10 : press.digit) - 1;
            if (slot < s.task_count) {
                ui::select(slot);
                ui::notify_press(slot);
                const auto target = codex_micro::active_transport();
                if (send_agent_key_to(target, slot, true)) {
                    agent_key_transport[slot] = target;
                    audio::play(audio::Cue::Select);
                } else {
                    agent_key_transport[slot] = codex_micro::Transport::None;
                    ui::toast("NO HOST", "task not opened", theme::kOrange);
                }
            }
        }
        if (press.key == Key::Enter && ui::composer_control_active()) {
            // While the host owns a control surface, Enter is the confirm.
            // The dial's own click is on `\\`, which is the encoder switch and
            // stays wired to it, but nothing on that page is a message, so
            // submitting the composer from here would be the wrong gesture
            // entirely -- and the page already draws an Enter cap.
            ui::notify_composer_control_select();
            const auto target = codex_micro::active_transport();
            const bool sent = send_encoder_press_to(target);
            encoder_press_transport = sent ? target : codex_micro::Transport::None;
            std::printf("CCP_UI|composer|confirm_via_enter|sent=%d\n", sent ? 1 : 0);
            if (!sent) ui::toast("DIAL", "no host", theme::kOrange);
            else audio::play(audio::Cue::MenuApply);
            break;
        }
        if (press.key == Key::Enter) {
            // Stock Codex Micro's CODEX key submits the composer. Ending
            // push-to-talk only prepares text; Enter is the deliberate,
            // separate send gesture and therefore uses ACT12 press/release.
            if (!companion_codex_key("ACT12", 1, s.selected)) {
                ui::toast("NO HOST", "message not sent", theme::kOrange);
                break;
            }
            companion_codex_key("ACT12", 0, s.selected);
            audio::play(audio::Cue::Select);
        }
        if (press.key == Key::Interrupt) {
            const model::Task* task = model::selected_task();
            if (task) {
                hostlink::sendf("CCP_INTERRUPT|%s\n", task->id);
                ui::toast("INTERRUPT SENT", task->title, theme::kInput);
            }
        }
        break;
    }
}

} // namespace

// ============================================================ link callbacks
namespace hostlink {

void handle_line(char* line)
{
    if (!line || !*line)
        return;
    service_staged_timeout();

    if (std::strcmp(line, "PING") == 0) {
        sendf("CCP_PONG|%s\n", firmware_version());
        return;
    }
    if (std::strncmp(line, "SCREENSHOT|", 11) == 0) {
        send_screenshot(line + 11);
        return;
    }
    if (std::strcmp(line, "AUTOSTART") == 0) {
        enable_m5apps_autostart();
        return;
    }
    // A live Codex Micro session owns the deck and Auto-dim. The leftover USB
    // companion bridge polls DECK/TASK every two seconds; treating that as
    // activity slammed the backlight awake and made the panel blink.
    if (codex_micro::active_transport() != codex_micro::Transport::None)
        return;
    ui::wake();
    if (std::strncmp(line, "HOST|", 5) == 0) {
        // A new host session owns the deck from scratch. Discarding any
        // pending diagnostic batch here keeps a stale partial batch from
        // merging with the next one, whatever slot it starts at.
        if (staged_started_ms) {
            sendf("CCP_DECK|WARN|batch_reset_by_host\n");
            reset_staged_tasks();
        }
        std::snprintf(model::state.host, sizeof(model::state.host), "%s", line + 5);
        ui::invalidate();
        return;
    }

    if (std::strncmp(line, "CFG|", 4) == 0) {
        // Host-authoritative current values. The option lists themselves arrive
        // via OPT lines; here we only move the cursor onto what Codex is using.
        char* fields[3] = {};
        char* cursor = line + 4;
        for (int i = 0; i < 3 && cursor; ++i) {
            fields[i] = cursor;
            char* next = std::strchr(cursor, '|');
            if (next) {
                *next = 0;
                cursor = next + 1;
            } else
                cursor = nullptr;
        }
        auto& s = model::state;
        if (fields[0])
            s.models.select_wire(fields[0]);
        if (fields[1])
            s.efforts.select_wire(fields[1]);
        if (fields[2])
            s.speeds.select_wire(fields[2]);
        // No store::save_settings() here: option cursors are host-owned and
        // re-supplied on every connect, so persisting them would only wear
        // the NVS partition.
        publish_settings(false);
        ui::invalidate();
        return;
    }

    // OPT|<MODEL|EFFORT|SPEED>|<current wire>|<label>=<wire>|...
    // The host owns these catalogues; hardcoding them in firmware would show
    // choices Codex does not offer and hide the ones it does.
    if (std::strncmp(line, "OPT|", 4) == 0) {
        char* cursor = line + 4;
        char* dimension = cursor;
        if ((cursor = std::strchr(cursor, '|')) == nullptr)
            return;
        *cursor++ = 0;
        char* current = cursor;
        if ((cursor = std::strchr(cursor, '|')) == nullptr)
            return;
        *cursor++ = 0;

        model::OptionList* list = nullptr;
        if (std::strcmp(dimension, "MODEL") == 0)
            list = &model::state.models;
        else if (std::strcmp(dimension, "EFFORT") == 0)
            list = &model::state.efforts;
        else if (std::strcmp(dimension, "SPEED") == 0)
            list = &model::state.speeds;
        if (!list)
            return;

        list->count = 0;
        char* save = nullptr;
        for (char* token = ::strtok_r(cursor, "|", &save);
             token && list->count < model::kMaxOptions; token = ::strtok_r(nullptr, "|", &save)) {
            char* equals = std::strchr(token, '=');
            if (!equals)
                continue;
            *equals = 0;
            std::snprintf(list->label[list->count], model::kLabelMax, "%s", token);
            std::snprintf(list->wire[list->count], model::kWireMax, "%s", equals + 1);
            ++list->count;
        }
        list->index = 0;
        list->select_wire(current);
        // Catalogues are host-owned too; see the CFG note about NVS wear.
        publish_settings(false);
        ui::invalidate();
        return;
    }

    // TASK|<slot>|<status>|<threadId>|<title>.
    if (std::strncmp(line, "TASK|", 5) == 0) {
        char* cursor = line + 5;
        char* slot_text = cursor;
        char* status_text = nullptr;
        char* id_text = nullptr;
        char* title_text = nullptr;
        if ((cursor = std::strchr(cursor, '|')) == nullptr)
            return;
        *cursor++ = 0;
        status_text = cursor;
        if ((cursor = std::strchr(cursor, '|')) == nullptr)
            return;
        *cursor++ = 0;
        id_text = cursor;
        if ((cursor = std::strchr(cursor, '|')) == nullptr)
            return;
        *cursor++ = 0;
        title_text = cursor;
        int slot = -1;
        model::Status status = model::Status::Idle;
        if (!parse_int_field(slot_text, 0, model::kMaxTasks - 1, slot) ||
            !parse_status(status_text, status) || !*id_text) {
            sendf("CCP_DECK|ERROR|invalid_task\n");
            return;
        }
        if (staged_started_ms && slot == 0 && staged_present[0]) {
            sendf("CCP_DECK|WARN|batch_restarted\n");
            reset_staged_tasks();
        } else if (staged_started_ms && staged_present[slot]) {
            // A repeated non-zero slot is last-wins within the batch. Keep
            // the batch intact but make the overwrite observable.
            sendf("CCP_DECK|WARN|slot_overwrite|slot=%d\n", slot);
        }
        if (!staged_started_ms)
            staged_started_ms = lgfx::millis();
        staged[slot] = model::Task{};
        staged[slot].status = status;
        staged[slot].color = diagnostic_color(status);
        staged[slot].present = true;
        staged[slot].seen = true;
        std::snprintf(staged[slot].id, sizeof(staged[slot].id), "%s", id_text);
        sanitize_utf8(title_text, staged[slot].title, sizeof(staged[slot].title));
        staged_present[slot] = true;
        staged_count = std::max(staged_count, slot + 1);
        return;
    }
    if (std::strncmp(line, "TASKS|", 6) == 0) {
        char* count_text = line + 6;
        char* selected_text = std::strchr(count_text, '|');
        if (selected_text)
            *selected_text++ = 0;
        int count = 0, selected = -1;
        bool count_ok = parse_int_field(count_text, 0, model::kMaxTasks, count);
        bool selected_ok =
            !selected_text || parse_int_field(selected_text, -1, model::kMaxTasks - 1, selected);
        if (!count_ok || !selected_ok || (count > 0 && !staged_batch_complete(count))) {
            sendf("CCP_DECK|ERROR|incomplete_or_invalid|count=%s\n", count_text);
            reset_staged_tasks();
            return;
        }
        staged_count = count;
        commit_tasks(selected_text ? selected : -1);
        reset_staged_tasks();
        sendf("CCP_DECK|%d|%d\n", model::state.task_count, model::state.selected);
        return;
    }
}

} // namespace hostlink

extern "C" void companion_ble_link_changed(bool connected)
{
    std::printf("CCP_NATIVE|ble_link|%s\n", connected ? "connected" : "disconnected");
}

// ==================================================================== startup
extern "C" void app_main(void)
{
    auto config = M5.config();
    M5.begin(config);

    hostlink::init();
    usb_serial_jtag_vfs_use_driver();
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    const esp_err_t default_nvs = nvs_flash_init();
    hostlink::emit("default_nvs", default_nvs == ESP_OK || default_nvs == ESP_ERR_NOT_FOUND,
                   esp_err_to_name(default_nvs));
    store::init();
    ui::init();

    hostlink::emit("boot", true, firmware_version());
    hostlink::sendf("CCP_HELLO|%s|%s\n", firmware_version(), esp_get_idf_version());
    hostlink::sendf("CCP_BOOT|reset_reason=%d\n", static_cast<int>(esp_reset_reason()));

    keys::init();
    {
        char detail[64];
        std::snprintf(detail, sizeof(detail), "%s,board=%d", keys::backend_name(),
                      static_cast<int>(M5.getBoard()));
        hostlink::emit("keyboard", keys::backend() != keys::Backend::None, detail);
    }
    audio::init();

    // The startup score goes out before the radio does. It is a second and a
    // half of continuously fed DMA, and starting it after the radio put its
    // first blocks in exactly the window where a bonded host reconnects and
    // floods the device with RPC.
    if (model::state.startup_sound_on)
        audio::play(audio::Cue::Boot);

    companion_ble_start();
    if (model::state.usb_hid_enabled)
        companion_usb_start();
    char bonds[24];
    std::snprintf(bonds, sizeof(bonds), "bonds=%d", companion_ble_bond_count());
    hostlink::emit("ble_start", true, bonds);

    publish_settings(false);

    uint32_t last_status_ms = 0;
    uint32_t offline_since_ms = 0;
    bool pairing_was_active = false;
    constexpr uint32_t kOfflineGraceMs = 3000;

    while (true) {
        M5.update();
        hostlink::poll();
        companion_ble_service();
        codex_micro::service();
        service_staged_timeout();
        const bool pairing_is_active = companion_ble_pairing_active();
        if (pairing_is_active != pairing_was_active) {
            pairing_was_active = pairing_is_active;
            ui::set_pairing_pin(pairing_is_active, companion_ble_pairing_passkey());
        }
        if (joystick_deflected && static_cast<int32_t>(lgfx::millis() - joystick_release_ms) >= 0) {
            codex_micro::send_joystick_to(joystick_transport, 0.f, 0.f);
            joystick_deflected = false;
            joystick_transport = codex_micro::Transport::None;
        }
        const auto active_transport = codex_micro::active_transport();
        if ((voice_gesture.active || voice_gesture.agent_key_down) &&
            active_transport != voice_gesture.transport)
            release_voice_gesture();
        const model::Link previous_link = model::state.link;
        const model::Link detected_link =
            active_transport == codex_micro::Transport::Usb   ? model::Link::Usb
            : active_transport == codex_micro::Transport::Ble ? model::Link::Ble
                                                              : model::Link::Offline;
        const uint32_t link_now = lgfx::millis();
        if (detected_link == model::Link::Offline && previous_link != model::Link::Offline) {
            if (offline_since_ms == 0) {
                offline_since_ms = link_now;
                hostlink::sendf("CCP_LINK|offline_grace|start|ms=%u\n",
                                static_cast<unsigned>(kOfflineGraceMs));
            }
            if (link_now - offline_since_ms < kOfflineGraceMs) {
                model::state.link = previous_link;
            } else {
                model::state.link = model::Link::Offline;
                hostlink::sendf("CCP_LINK|offline_grace|expired\n");
            }
        } else {
            if (offline_since_ms != 0 && detected_link != model::Link::Offline) {
                hostlink::sendf("CCP_LINK|offline_grace|recovered|after_ms=%u\n",
                                static_cast<unsigned>(link_now - offline_since_ms));
            }
            offline_since_ms = 0;
            model::state.link = detected_link;
        }
        if (model::state.link != previous_link) {
            ui::invalidate();
            if (previous_link == model::Link::Offline &&
                model::state.link != model::Link::Offline) {
                if (ui::screen() == ui::Screen::Boot)
                    ui::go(ui::Screen::Deck);
                else
                    ui::toast("LINKED", model::state.link == model::Link::Usb ? "USB" : "Bluetooth",
                              theme::kRun);
                publish_settings(false);
            } else if (model::state.link == model::Link::Offline) {
                codex_micro::begin_session_sync();
                if (voice_gesture.active || voice_gesture.agent_key_down)
                    release_voice_gesture();
                ui::go(ui::Screen::Boot);
            }
        }

        // G0 mirrors the stock Codex Micro microphone key. Codex records from
        // the Mac and attaches the transcription to its currently selected
        // chat; Cardputer stores no audio and does not need an SD card.
        if (M5.BtnA.wasPressed()) {
            if (voice_gesture.active || voice_gesture.agent_key_down)
                release_voice_gesture();
            if (!ui::wake()) {
                auto target = codex_micro::active_transport();
                int agent = std::clamp(model::state.selected, 0, model::kMaxTasks - 1);
                voice_gesture.transport = target;
                voice_gesture.agent = agent;
                voice_gesture.agent_key_down = send_agent_key_to(target, agent, true);
                voice_gesture.active = codex_micro::send_key_to(target, "ACT10", 1, agent);
                if (!voice_gesture.active && voice_gesture.agent_key_down) {
                    send_agent_key_to(target, agent, false);
                    voice_gesture.agent_key_down = false;
                }
                ui::set_voice_active(voice_gesture.active, agent);
                hostlink::sendf("CCP_VOICE|press|slot=%d|transport=%s|sent=%d\n", agent,
                                codex_micro::transport_name(target), voice_gesture.active ? 1 : 0);
            }
        }
        if (M5.BtnA.wasReleased() && (voice_gesture.active || voice_gesture.agent_key_down)) {
            int agent = voice_gesture.agent;
            auto target = voice_gesture.transport;
            release_voice_gesture();
            hostlink::sendf("CCP_VOICE|release|slot=%d|transport=%s\n", agent,
                            codex_micro::transport_name(target));
        }

        for (Press press = keys::next(); press.key != Key::None; press = keys::next()) {
            // Telemetry first: the host sees every key even when the panel was
            // asleep and swallowed it, which is what makes input debuggable.
            hostlink::sendf("CCP_KEY|%s|%d|%s\n", keys::name(press.key), press.digit,
                            press.down ? "down" : "up");
            // A blanked panel eats the first key so the user never acts blind.
            if (ui::wake()) {
                // WAKE restores Codex lighting too. It is intentionally unknown
                // to the action map so
                // this first key cannot also approve, reject or switch a task.
                companion_codex_key("WAKE", 1);
                companion_codex_key("WAKE", 0);
                continue;
            }
            handle_press(press);
        }

        const uint32_t now = lgfx::millis();
        if (now - last_status_ms >= 5000) {
            last_status_ms = now;
            model::state.battery = M5.Power.getBatteryLevel();
            model::state.charging = M5.Power.isCharging();
            ui::invalidate();
        }
        store::service();
        ui::service_power();
        ui::service();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
