#!/usr/bin/env python3
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
failures: list[str] = []


def require(path: str, pattern: str, message: str) -> None:
    text = (ROOT / path).read_text()
    if not re.search(pattern, text, re.MULTILINE):
        failures.append(message)


def forbid(path: str, pattern: str, message: str) -> None:
    text = (ROOT / path).read_text()
    if re.search(pattern, text, re.MULTILINE):
        failures.append(message)


def forbid_within(path: str, start: str, end: str, pattern: str, message: str) -> None:
    text = (ROOT / path).read_text()
    section = re.search(start + r"[\s\S]*?" + end, text, re.MULTILINE)
    if section is None:
        failures.append(message + " (section not found)")
    elif re.search(pattern, section.group(0), re.MULTILINE):
        failures.append(message)


require("main/model.h", r"status_debounce_ms\s*=\s*100;", "default debounce must be 100 ms")
require("main/model.h", r"status_audio_offset_ms\s*=\s*200;", "default audio offset must be +200 ms")
require("main/store.cpp", r'"audio_of3"', "current NVS audio-offset key must be audio_of3")
require("main/ui.cpp", r"std::clamp\([\s\S]{0,160}-300,\s*300\)",
        "audio offset UI must clamp to -300..+300")
require("main/status_timing.h", r"hold\s*=\s*1\.85f;", "status hold must be 1.85 s")
require("README.md", r"-300\.\.[+]300 ms status-audio offset", "README must publish -300..+300 ms")
require("SCENARIOS.ru.md", r"фиксируется на 1,85 секунды",
        "scenario duration must match status_timing.h")
require("main/codex_micro_protocol.cpp",
        r'"lights\.preview"[\s\S]{0,180}ui::note_composer_control_preview\(\)',
        "native light preview must feed the composer control overlay")
require("main/ui.cpp",
        r"void note_composer_control_closed\(\)[\s\S]{0,200}dismiss_composer_control_preview\(\)",
        "the host's own close frame must take the control page down")
require("main/codex_micro_protocol.cpp",
        r"if \(!any_lit\) \{[\s\S]{0,400}note_composer_control_closed\(\)",
        "an all-off frame must be read as the host closing its surface")
require("main/codex_micro_protocol.cpp",
        r"bool uniform_white = true;[\s\S]{0,600}note_composer_control_closed\(\)",
        "a uniform white frame must be read as the host closing its surface")
forbid("main/ui.cpp", r"composer_control_idle >= 8\.f",
       "the control page must not close on a timer of ours; only the host or Esc")
require("main/ui.cpp",
        r"void note_composer_control_preview\(\)[\s\S]{0,450}if \(composer_control_target\)",
        "late light preview must not reopen a confirmed control")
require("main/codex_micro_protocol.cpp",
        r"if \(selection_guarded\(\) \|\| ui::composer_control_active\(\)\)[\s\S]{0,2400}picker_preview_mirrored[\s\S]{0,80}return false;",
        "picker lamp preview must bypass the task status reducer")
require("main/codex_micro_protocol.cpp",
        r"ui::note_composer_control_lamps\(preview_rgb, preview_level\)",
        "the guarded lamp frame must be mirrored onto the control page, not dropped")
require("main/ui.cpp",
        r"void notify_composer_control_step\(int direction\)[\s\S]{0,420}"
        r"if \(!composer_control_target\) \{[\s\S]{0,220}set_composer_control_active\(true\)",
        "turning the dial must open the control surface, not only pressing it")
require("main/ui.cpp",
        r"composer_knob_angle\.to\(composer_knob_angle\.target\s*\+ composer_control_step_dir \* kDetentRadians\)",
        "each detent must advance the knob by exactly one flute")
require("main/ui.cpp",
        r"void draw_knob\(int cx, int cy, float angle",
        "the dial must be drawn as the cylinder it physically is")
require("main/ui.cpp",
        r"if \(!composer_control_engaged && composer_control_idle >= 3\.f\) \{"
        r"[\s\S]{0,160}dismiss_composer_control_preview\(\);",
        "a dial nudged with nothing clicked and nothing lit must not leave a page behind")
require("main/ui.cpp",
        r"composer_lamp_marker\.to\(static_cast<float>\(lit\)\);\s*"
        r"composer_control_engaged = true;",
        "a lit lamp from the host must engage the session and stop the idle timer")
require("main/main.cpp",
        r"if \(press\.key == Key::Enter && ui::composer_control_active\(\)\) \{"
        r"[\s\S]{0,600}send_encoder_press_to\(target\)",
        "Enter must confirm the host's control surface instead of submitting the composer")
forbid("main/ui.cpp",
       r"fill_rect\(cx \+ static_cast<int>\(kKnobRx \* 0\.60f",
       "the dial must not claim to know its absolute position")
require("main/ui.cpp",
        r"void note_composer_control_lamps\(const uint32_t\* rgb, const float\* level\)"
        r"[\s\S]{0,700}composer_lamp_marker\.to\(static_cast<float>\(lit\)\)",
        "the mirrored lamps must move the marker to the lit lamp")
require("main/main.cpp",
        r"audio::play\(right \? audio::Cue::StepRight : audio::Cue::StepLeft\);"
        r"[\s\S]{0,220}ui::notify_composer_control_step\(right \? 1 : -1\);",
        "a successful detent must always reach the control surface")
require("main/main.cpp",
        r"case Key::Back:[\s\S]{0,700}if \(ui::composer_control_active\(\)\) \{"
        r"[\s\S]{0,800}send_agent_key_to\(target, s\.selected, true\)",
        "escape must ask the host to drop its surface, not just hide the page")
forbid("main/ui.cpp", r"draw_keycap",
       "the control page carries one object, the dial; the confirm is a hint")
require("main/ui.cpp",
        r"draw_knob\(kScreenW / 2 \+ lean,",
        "the dial must sit on the centre line with its chevrons")
require("main/ui.cpp",
        r"const int ax = kScreenW - 18, ay = kScreenH - 16;[\s\S]{0,320}"
        r"hline\(ax, ay \+ 5, 9, faint\);",
        "the confirm must be marked by its own arrow in the far corner")
forbid("main/ui.cpp", r'draw_tracked_right\("ENTER"',
       "the confirm is a mark, not a word")
require("main/ui.cpp",
        r"constexpr int kOutline  = 2;",
        "the instrument must carry a keyline heavy enough to survive the grid")
require("main/main.cpp",
        r"Cue::StepRight\s*:\s*audio::Cue::StepLeft",
        "encoder detents must have directional audio")
require("main/main.cpp",
        r"confirm_via_enter[\s\S]{0,200}Cue::MenuApply",
        "host-previewed composer selection must have a distinct apply cue")
require("main/audio.cpp",
        r"apply: C5 -> E5 -> G5",
        "composer confirmation must use the successful rising major cue")
require("main/codex_micro_protocol.cpp",
        r"selection_guarded\(\) \|\| ui::composer_control_active\(\)",
        "host-opened composer control must keep previews out of task status")
require("main/ui.cpp",
        r"composer_control_open_sound_pending[\s\S]{0,260}audio::play\(audio::Cue::MenuOpen\)",
        "host-opened composer cue must be deferred to the main UI task")
require("main/main.cpp",
        r"send_encoder_step\(right\)[\s\S]{0,200}no host[\s\S]{0,320}notify_composer_control_step",
        "a dial with no host must stay silent instead of opening a local mode")
require("main/main.cpp",
        r"press\.key == Key::Enter[\s\S]{0,420}companion_codex_key\(\"ACT12\", 1[\s\S]{0,180}companion_codex_key\(\"ACT12\", 0",
        "Enter must send the native Codex Micro composer action")
require("main/key_layout.h",
        r"col >= 5 && col <= 10[\s\S]{0,80}Key::NativeAction, col \+ 1",
        "T through P must expose native command slots ACT06 through ACT11")
require("main/main.cpp",
        r"if \(!press\.down\)[\s\S]{0,700}Key::NativeAction[\s\S]{0,400}send_native_action_to\([\s\S]{0,180}false",
        "all native command slots must preserve their release edge")
require("main/main.cpp",
        r"if \(!press\.down\)[\s\S]{0,400}send_agent_key_to\([\s\S]{0,160}false[\s\S]{0,20000}send_agent_key_to\([\s\S]{0,160}true",
        "agent keys must preserve down and up for native double tap")
require("main/main.cpp",
        r"Key::Enter\)[\s\S]{0,320}send_key_to\([\s\S]{0,140}\"ENC\",\s*0[\s\S]{0,30000}send_encoder_press_to\(",
        "encoder release must remain physical so Codex can detect long press")
require("main/ble_transport.cpp", r"target == codex_micro::Transport::Usb[\s\S]{0,140}companion_usb_send_rpc", "native responses must remain bound to USB requester")
require("main/ble_transport.cpp", r"kNativeAddresses\[3\]\[6\]",
        "three BLE host identities must exist")
require("main/store.cpp", r'"ble_slot"',
        "selected BLE host channel must persist")
require("main/ble_transport.cpp",
        r"ble_gap_adv_stop\(\)[\s\S]{0,900}ble_gap_terminate",
        "BLE profile switching must restart only the radio session")
require("main/main.cpp", r"companion_ble_select_profile\(profile\)",
        "settings must hot-switch the selected BLE profile")
require("main/main.cpp", r"SettingsRow::BleProfile[\s\S]{0,500}(?!esp_restart)",
        "BLE profile settings must not restart the device")
require("main/main.cpp", r"active_transport == codex_micro::Transport::Usb[\s\S]{0,220}codex_micro::Transport::Ble", "visible link state must follow source-aware native session")
require("main/codex_micro_protocol.cpp",
        r"if \(!cJSON_IsString\(method\)\)[\s\S]{0,260}hostlink::note_host_activity\(\)",
        "a valid native Codex RPC must refresh the live session")
require("main/codex_micro_protocol.cpp", r"usb_last_valid_ms[\s\S]{0,200}ble_last_valid_ms", "liveness must be tracked per transport")
require("main/main.cpp",
        r"s\.link == model::Link::Offline[\s\S]{0,260}press\.key == Key::Settings[\s\S]{0,180}ui::Screen::Boot\s*:\s*ui::Screen::Settings",
        "Tab must toggle local settings from the offline splash")
require("main/main.cpp",
        r"developer_preview_active\(\)[\s\S]{0,180}Key::Back[\s\S]{0,100}close_developer_preview\(\)[\s\S]{0,40}return;",
        "developer previews must capture all input and close only on Esc")
require("main/main.cpp",
        r"PreviewsRow::Splash[\s\S]{0,120}DeveloperPreview::Splash[\s\S]{0,200}"
        r"DeveloperPreview::Pairing[\s\S]{0,200}DeveloperPreview::Control"
        r"[\s\S]{0,200}DeveloperPreview::Stick",
        "the screen checks list must reach every previewable path")
require("main/main.cpp",
        r"ui::go\(ui::Screen::Previews\);",
        "screen checks must live on their own list, not among behaviour switches")
require("main/ui.cpp",
        r"if \(stick_control_idle >= kStickIdleClose\) \{[\s\S]{0,120}dismiss_stick_control\(\);",
        "the stick page fixes on nothing and must always close itself")
require("main/main.cpp",
        r"if \(!ui::composer_control_active\(\)\) \{\s*ui::notify_stick_step",
        "an arrow must not throw the dial page off the screen mid-selection")
require("main/main.cpp",
        r"const bool rising = press\.key == Key::Right \|\| press\.key == Key::Up;"
        r"\s*audio::play\(rising \? audio::Cue::StepRight : audio::Cue::StepLeft\);",
        "the stick must be as audible as the dial, and in the same direction")
require("main/ui.cpp",
        r"stick_lean_x\.to\(dirs\[direction\]\[0\] \* 1\.f\);[\s\S]{0,140}stick_throw_hold = 0\.f;",
        "the cap must spring out to its throw, not be teleported to it")
require("main/ui.cpp",
        r"void draw_stick\(int cx, int cy, float lx, float ly",
        "the stick must be drawn as the object it is, in the dial's family")
require("main/audio.cpp",
        r"sound_volume\) \* 250u",
        "default 60 percent must preserve original hardware output 150")
require("main/audio.cpp",
        r"CCP_CHIME_READY[\s\S]{0,500}playing_buffer\.store\(0,[\s\S]{0,160}playRaw\(status_pcm\[0\]",
        "startup chime buffer must be reserved before speaker DMA can consume it")
require("main/audio.cpp",
        r"float voice_gain\(float hz\)[\s\S]{0,200}"
        r"if \(f >= kLoudRef\) return 1\.f;[\s\S]{0,80}"
        r"return std::pow\(kLoudRef / f, 0\.75f\);",
        "every voice must be corrected for the register it sits in: this "
        "speaker radiates almost nothing low, so equal level is not equal "
        "loudness")
forbid_within("main/audio.cpp", r"for \(size_t i = 0; i < kStatusLen; \+\+i\)",
              r"CCP_AUDIO\|status=", r"voice_gain\(",
              "register correction must be worked out before the sample loop, "
              "never inside it: it is a std::pow per call")
require("main/audio.cpp",
        r"void trim_to_peak\(int16_t\* pcm, size_t len, float peak, float target\)"
        r"[\s\S]{0,260}factor = target \* 32700\.f / \(peak \* kRawScale\);",
        "a score's peak is only known once it is rendered, so its trim must "
        "be a second pass, not a fixed output scale")
require("main/audio.cpp",
        r"kPeakAttention = 0\.95f;[\s\S]{0,120}kPeakDone      = 0\.95f;"
        r"[\s\S]{0,60}kPeakError     = 0\.92f;[\s\S]{0,60}kPeakRunning   = 0\.80f;"
        r"[\s\S]{0,60}kPeakIdle      = 0\.55f;",
        "the loudness hierarchy between cues must live in one place: a "
        "request and a result own the room, work in progress sits under them")
require("main/audio.cpp",
        r"trim_to_peak\(output, kStatusLen, peak, target_peak\);",
        "every status score must be trimmed to its target peak")
require("main/audio.cpp",
        r"trim_to_peak\(thock_pcm, kThockLen, peak, kPeakThock\);",
        "the key press must be trimmed to its target peak")
require("main/audio.cpp",
        r"trim_to_peak\(status_pcm\[0\], kBootLen, peak, kPeakChime\);",
        "the startup chime must be trimmed to its target peak")
require("main/audio.cpp",
        r"kAskSteps\[4\] = \{[\s\S]{0,200}\{1\.f,\s*\{1\.f, 1\.5f,\s*1\.25f\},\s*false\},[\s\S]{0,180}"
        r"\{0\.8f,\s*\{1\.f, 1\.3333f, 1\.125f\},\s*false\},[\s\S]{0,200}"
        r"\{1\.2f,\s*\{1\.f, 1\.5f,\s*1\.3333f\}, true\},[\s\S]{0,200}"
        r"\{0\.8889f,\s*\{1\.f, 1\.25f,\s*1\.125f\},\s*false\},",
        "a series of input requests must walk one four-chord progression, "
        "peaking on the third step and leaning back on the fourth")
require("main/audio.cpp",
        r"ask_step = static_cast<uint8_t>\(\(ask_step \+ 1\) & 3\);",
        "each input request must take the next step of the progression")
require("main/audio.cpp",
        r"if \(last_ask_us == 0 \|\| now_us - last_ask_us > kSeriesGapUs\) \{\s*\n"
        r"\s*ask_step = 0;\s*\n\s*series_pitch = pitch;",
        "the key must be held for a whole series and rolled only once it has "
        "gone quiet: four chords in four keys are not a progression")
forbid_within("main/audio.cpp", r"for \(size_t i = 0; i < kStatusLen; \+\+i\)",
              r"CCP_AUDIO\|status=", r"ask_step|last_ask_us|series_pitch",
              "the progression step must be chosen once per score, outside "
              "the sample loop")
require("main/audio.cpp",
        r"const float anchor = 220\.f \* series_pitch \* step\.chord_mult;[\s\S]{0,60}"
        r"pad_root    = anchor;[\s\S]{0,320}"
        r"call_root   = anchor \* 1\.5f \* step\.notes\[0\];[\s\S]{0,90}"
        r"mid_note    = anchor \* 1\.5f \* step\.notes\[1\];[\s\S]{0,90}"
        r"answer_note = anchor \* 1\.5f \* step\.notes\[2\];",
        "the input cue must speak a fifth above its chord, where completion "
        "never goes, and never from the bell's octave")
require("main/audio.cpp",
        r"if \(ask_shimmer && on2 < kAskSpan \* kShimmerTau\)",
        "only the peak of the progression may be lit: a glint on every ask is "
        "just a bright cue")
require("main/audio.cpp",
        r"inline float ask_voice\(float freq, float onset, float t, float tau\)"
        r"[\s\S]{0,400}fast_sin\(phase\) \+ fast_sin\(phase \* 3\.f\) \* 0\.13f\)\s*\n"
        r"\s*\* attack \* attack \* decay;",
        "the input cue must be hollow where the completion bell is glassy: "
        "odd harmonics only, no octave partial, and a squared attack")
forbid_within("main/audio.cpp", r"inline float ask_voice",
              r"^\}", r"phase \* 2\.f",
              "the input voice must not carry the bell's octave partial: that "
              "is what made a request sound like a result")
require("main/audio.cpp",
        r"kMidAt     = 0\.068f;\s*\nconstexpr float kAnswerAt  = 0\.255f;",
        "the input phrase must be speech, not the bell's even roll: two notes "
        "close together, then a gap before the one it settles on")
require("main/audio.cpp",
        r"v = ask_voice\(call_root, kAskAttack, on1, kCallTau\) \* 0\.34f \* g_call;[\s\S]{0,200}"
        r"v \+= ask_voice\(mid_note, kAskAttack, on2, kMidTau\) \* 0\.28f \* g_mid;[\s\S]{0,1300}"
        r"v \+= ask_voice\(answer_note, kAnswerAttack, on3,\s*\n"
        r"\s*kAnswerTau\) \* 0\.17f \* g_answer;",
        "the input cue must be three notes, each quieter than the one before, "
        "sharing one voice")
require("main/audio.cpp",
        r"kAnswerAttack = 0\.038f;",
        "the last input note must open far more slowly than the two that "
        "reach it: it is the highest, and it is the one left ringing")
require("main/audio.cpp",
        r"const float on2 = gesture_t - kMidAt;\s*\n"
        r"\s*const float on3 = gesture_t - kAnswerAt;",
        "the input notes must ring together, never cut each other off")
require("main/audio.cpp",
        r"const float phase = 6\.28318f \* freq \* t;",
        "no input note may bend its pitch: a rising leading tone on a pure "
        "tone is a whine, and the figure asks by where it stops instead")
forbid_within("main/audio.cpp", r"\} else if \(cue == Cue::Attention\)",
              r"\} else if \(cue == Cue::Error\)",
              r"lfo_next|pad_bed|raw_pulse|kAskStep|recede",
              "the input cue must not gate, pulse or repeat its bed: that is "
              "what turns a request into nagging")
require("main/audio.cpp",
        r"bed_phase = 6\.28318f \* pad_root \* cue_t;[\s\S]{0,120}"
        r"fast_sin\(bed_phase\) \* 0\.044f \* g_ask_bed_1[\s\S]{0,110}"
        r"fast_sin\(bed_phase \* 1\.3333f\) \* 0\.032f \* g_ask_bed_2\) \* bed;",
        "the input bed must be a quiet bare open fourth on the shared hold "
        "envelope: a fifth would sit exactly on the phrase's first note")
forbid("main/audio.cpp", r"tense_ratio|1\.4142f",
       "no status cue may be built on a tritone")
require("main/audio.cpp",
        r"inline float fast_decay\(float x\)[\s\S]{0,400}decay_table\[index \+ 1\] - decay_table\[index\]",
        "envelope decay must come from an interpolated table")
forbid_within("main/audio.cpp", r"for \(size_t i = 0; i < kStatusLen; \+\+i\)", r"CCP_AUDIO\|status=",
              r"std::(exp|sin|cos|tan|pow|floor|fmod)\(",
              "the status score must render without libm on its per-sample path")
require("main/audio.cpp",
        r"kStatusChannel\s*=\s*0[\s\S]{0,80}kInterfaceChannel\s*=\s*1",
        "status and interface sounds must use separate hardware mixer channels")
require("main/audio.cpp",
        r"void thock\(\)[\s\S]{0,520}kInterfaceChannel, true",
        "button cues must stay on the interface channel")
require("main/audio.cpp",
        r"void play_control[\s\S]{0,260}kInterfaceChannel, true",
        "control cues must stay on the interface channel")
forbid("main/audio.cpp",
       r"void thock\(\)[\s\S]{0,420}Speaker\.stop\(\)|void play_control[\s\S]{0,220}Speaker\.stop\(\)",
       "interface cues must not globally stop an in-flight status score")
require("main/audio.cpp",
        r"bool play_prepared_status\(\)[\s\S]{0,900}retry=channel_busy[\s\S]{0,260}armed_buffer\.store\(-1",
        "status playback must retain its armed buffer until playRaw succeeds")
require("main/ui.cpp",
        r"if \(audio::play_prepared_status\(\)\)\s*announce_audio_armed = false",
        "the UI must retry an unaccepted prepared status score")
require("main/model.h", r"sound_volume\s*=\s*60;[\s\S]{0,80}unmuted_volume\s*=\s*60;[\s\S]{0,120}startup_chime\s*=\s*3;",
        "clean settings must default to original 60% volume and CLOUD")
require("main/store.cpp", r'"volume_v4"', "restored original volume scale must use a fresh NVS key")
require("main/store.cpp", r'"chime_d3"', "CLOUD default must use a new NVS key")
require("main/ui.cpp",
        r"draw_pairing_takeover\(\)[\s\S]{0,500}draw_splash_field\(0\.48f, 63\)[\s\S]{0,1000}micro5_pin_glyph[\s\S]{0,500}draw_micro5_digit",
        "pairing must reuse splash styling and centre PIN in the deck numeral face")
require("tools/generate_micro5_digits.py", r'GLYPHS = "1234560789"',
        "Micro5 asset must cover PIN digits without changing deck glyph indices")
require("main/ui.cpp",
        r"packet < 24[\s\S]{0,500}0\.145f \+ \(packet % 5\) \* 0\.014f[\s\S]{0,700}packet % 6 == 0 \? kBlue : kDim[\s\S]{0,500}tail <= 2",
        "the splash background must animate hard-edged pixel packets and tails")
require("main/ui.cpp", r'USB OR BLUETOOTH  OPEN CODEX',
        "the splash must present USB and Bluetooth as equal native transports")
require("main/ui.cpp",
        r'mark_y = static_cast<int>\(motion::lerp\(51\.f, 44\.f, intro\)\)[\s\S]{0,260}draw_tracked_transparent\("CODEX", mark_x \+ 1',
        "the splash wordmark must be lowered and pixel-overprinted for weight")
require("main/ui.cpp",
        r"fmod\(clock_phase, 2\.00f\) < 1\.55f[\s\S]{0,180}USB OR BLUETOOTH",
        "the connection prompt must retain its readable arcade blink cadence")
require("main/theme.h", r"kGreen\s*=\s*rgb\(38,\s*198,\s*58\)",
        "unread completion green must remain the saturated product colour")
require("main/theme.h", r"kViewed\s*=\s*rgb\(228,\s*229,\s*225\)",
        "viewed completion gray must remain close to the paper background")
require("main/store.cpp", r'"usb_hid"',
        "developer USB HID preference must persist")
require("main/usb_transport.cpp",
        r"companion_usb_set_enabled\(bool requested\)[\s\S]{0,650}tinyusb_driver_uninstall\(\)[\s\S]{0,1600}tinyusb_driver_install",
        "USB HID must support real runtime detach and reattach")
require("main/main.cpp",
        r"DebugSettingsRow::UsbHid[\s\S]{0,600}companion_usb_set_enabled\(requested\)",
        "developer settings must control the USB HID transport")
require("main/ble_transport.cpp", r"target == codex_micro::Transport::Usb[\s\S]{0,260}target == codex_micro::Transport::Ble", "responses must use explicit transport routes")
require("main/ble_transport.cpp",
        r"esp_hid_ble_gap_adv_init\(ESP_HID_APPEARANCE_KEYBOARD[\s\S]{0,450}sm_mitm\s*=\s*1;[\s\S]{0,100}sm_io_cap\s*=\s*BLE_HS_IO_DISPLAY_ONLY",
        "BLE pairing policy must be applied after helper defaults with authenticated display PIN")
require("main/ble_transport.cpp",
        r"constexpr auto kReportMap[\s\S]{0,260}0x05,\s*0x01,\s*0x09,\s*0x06[\s\S]{0,700}0x06,\s*0x00,\s*0xFF[\s\S]{0,350}0x85,\s*0x06",
        "BLE HID map must expose the complete Codex Micro keyboard and vendor RPC collections")
require("main/ui.cpp",
        r"void draw_pairing_takeover\(\)[\s\S]{0,1500}ENTER PIN ON MAC",
        "Cardputer must display the BLE passkey and where to enter it")
require("main/main.cpp",
        r"companion_ble_pairing_active\(\)[\s\S]{0,240}ui::set_pairing_pin",
        "BLE security events must control the pairing screen")
require("main/ble_transport.cpp",
        r"stored_bond_count\(\) == 0[\s\S]{0,100}pairing_active\.store\(needs_pairing",
        "bonded BLE reconnects must not reopen the pairing overlay")
require("main/ble_transport.cpp",
        r"BLE_GAP_EVENT_ENC_CHANGE[\s\S]{0,180}pairing_active\.store\(false",
        "successful encryption must dismiss the pairing PIN")
require("main/ble_transport.cpp", r"ESP_HIDD_OUTPUT_EVENT[\s\S]{0,700}codex_micro::enqueue_report", "BLE callback must enqueue immutable protocol input")
require("main/ble_transport.cpp",
        r"ble_gap_conn_rssi[\s\S]{0,1100}adaptive_ble::next_tier",
        "connected BLE power must adapt to measured RSSI")
require("main/ble_transport.cpp",
        r"void apply_connection_power[\s\S]{0,350}esp_ble_tx_power_set_enhanced",
        "adaptive BLE tiers must be applied to the active connection handle")
require("main/ble_transport.cpp",
        r"BLE_GAP_LE_PHY_CODED_MASK[\s\S]{0,220}BLE_GAP_LE_PHY_CODED_S2",
        "weak BLE links must be able to negotiate coded S2 PHY")
require("main/ble_transport.cpp",
        r"adaptive_ble::tuning_ready\(connected_ms\) && next_coded != coded_phy[\s\S]{0,450}ble_gap_set_prefered_le_phy",
        "PHY negotiation must wait until the controller connection has settled")
require("main/ble_transport.cpp",
        r"ble_hid_task_start_up[\s\S]{0,300}BLE_HS_CONN_HANDLE_NONE[\s\S]{0,180}esp_hid_ble_gap_adv_start",
        "HID lifecycle hook must not restart advertising during an active connection")
require("main/ble_transport.cpp",
        r"ble_gap_adv_active\(\)[\s\S]{0,220}advertise_profile",
        "advertising watchdog must recover a stopped reconnect path")
require("main/ble_transport.cpp",
        r"ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P20",
        "disconnected advertising must use maximum discovery power")
require("main/ble_transport.cpp",
        r"adaptive_ble::weak_signal[\s\S]{0,180}ui::invalidate",
        "RSSI hysteresis must repaint when weak-link state changes")
require("main/theme.h", r"kSignalStripW\s*=\s*2 \* kCellPitchX",
        "the weak-link annunciator must span exactly two task columns")
require("main/ui.cpp",
        r"ble_signal_weak[\s\S]{0,1400}fill_rect\(x, 0, kSignalStripW, kSignalStripH, kPaper\)"
        r"[\s\S]{0,300}fill_rect\(x, 0, 1, kSignalStripH, kInk\);[\s\S]{0,120}BLE",
        "weak BLE must be a grid-aligned inset of the interface's own paper, "
        "delimited by a hairline -- not a dark sticker applied over the deck")
require("main/ui.cpp",
        r"for \(int dot = 0; dot < 3; \+\+dot\) \{[\s\S]{0,200}"
        r"dot == 0 \? kVerm : spent\);",
        "the weak-link level must be three equal dots with one lit, and the "
        "spent positions must stay drawn")
require("main/ui.cpp",
        r"const int right = weak_link \? kScreenW - kSignalStripW - 5 : 225;",
        "the critical battery readout must step aside for the annunciator")
require("tools/install.py",
        r"select_ota\(a\.port,\s*label,\s*\"115200\"\)[\s\S]{0,180}installed and launched",
        "M5Apps installer must select and launch the flashed OTA app without a manual picker")
require("main/codex_micro_protocol.cpp",
        r"status_reducer::apply\(\s*task, frame, session\.baseline\(\)\)",
        "all status snapshots during control-plane bootstrap must be baseline-only")
require("main/status_reducer.h",
        r"if \(initial_sync\)[\s\S]{0,420}locally_viewed_done = true[\s\S]{0,120}unseen_done = false",
        "connection baseline must not manufacture unread completions")
require("main/codex_micro_protocol.cpp",
        r"if \(task\.status == model::Status::Done &&\s*id->valueint == model::state\.selected\)[\s\S]{0,120}mark_done_viewed",
        "selected completion must settle viewed even during the local selection guard")
forbid("main/codex_micro_protocol.cpp",
       r"selection_guarded\(\)\s*&&\s*task\.status == model::Status::Done",
       "selection guard must not suppress selected completion read state")
require("main/codex_micro_protocol.cpp",
        r"if \(!session\.baseline\(\)\)\s*model::mark_done_viewed\(model::state\.selected\)",
        "every live native snapshot must repair selected completion read state")
require("main/ui.cpp",
        r"!announcing\(\)[\s\S]{0,420}settle_viewed_completion\([\s\S]{0,120}has_announcement_for_slot\(slot\)",
        "a completed selected task must settle even if its animation finalizer is missed")
require("main/main.cpp",
        r"s\.selected = selected_hint[\s\S]{0,320}mark_done_viewed\(s\.selected\)",
        "diagnostic task batches must use the same selected-completion read contract")
require("main/main.cpp",
        r"before->status != model::Status::Done[\s\S]{0,420}staged\[i\]\.color = lamp::kDoneUnseen",
        "diagnostic fresh completions must retain native unread green")
require("main/lamp.h",
        r"kRunning\s*=\s*0x304ffe[\s\S]{0,400}kError\s*=\s*0xff0033",
        "host lamp wire colours must stay byte-exact in lamp.h")
require("main/main.cpp",
        r'strncmp\(line, "HOST\|", 5\)[\s\S]{0,460}reset_staged_tasks',
        "a new host session must discard any pending diagnostic batch")
require("main/codex_micro_protocol.cpp",
        r"if\s*\([\s\S]{0,160}apply_thread_lights\([\s\S]{0,160}\)[\s\S]{0,360}session_sync::Method::ThreadStatus",
        "all-off and picker preview frames must not complete session baseline")
require("main/ui.cpp",
        r"current == Screen::Boot && !transition\.running\(\)[\s\S]{0,180}display_fade::apply",
        "low-brightness colour collapse must be restricted to the stable splash")
require("main/theme.h",
        r"kIdleDigit\s*=\s*rgb\(142,\s*138,\s*128\)[\s\S]{0,100}kIdleDigitSelected\s*=\s*rgb\(78,\s*76,\s*71\)",
        "idle digits must use distinct dark-grey resting and selected tones")
require("main/ui.cpp",
        r"Status::Done\)\s+return t\.unseen_done \? kInk : kIdleDigit[\s\S]{0,300}Status::Done && !t\.unseen_done",
        "viewed completed tasks must use the inactive grey digit scale")
forbid("main/key_layout.h", r"Key::EncoderPress",
       "backslash must not duplicate Enter as a second confirm key")
require("main/ui.cpp",
        r"for \(int k = 0; k < 24; \+\+k\)[\s\S]{0,500}vline\(x, y, kKnobWall",
        "the knob must be knurled around its wall, like the Work Louder encoder")
require("main/ui.cpp",
        r"fill_ellipse\(cx, ring_y \+ 1, kKnobRx \+ kOutline \+ 1, kKnobRy \+ 1, lit\)",
        "the knob must carry the host lamp colour in its base light ring")
require("main/ui.cpp",
        r"dismiss_composer_control_preview\(\)[\s\S]{0,180}suppressed_until_ms = lgfx::millis\(\) \+ 5000",
        "late host previews must not reopen an explicitly dismissed overlay")
require("main/ui.cpp",
        r"kDimHoldMs\s*=\s*180000[\s\S]{0,100}kDarkAfterMs\s*=\s*kDimAfterMs \+ kDimHoldMs",
        "the readable ten-percent dim state must last three full minutes")
require("main/ui.cpp",
        r"now - lighting_changed_ms >= kDimHoldMs[\s\S]{0,180}configured / 10",
        "host auto-dim must use the same three-minute readable hold")
require("main/main.cpp",
        r"void handle_line\([\s\S]{0,1200}active_transport\(\) != codex_micro::Transport::None[\s\S]{0,80}return;[\s\S]{0,40}ui::wake\(\)",
        "USB diagnostic traffic must not wake Auto-dim during a live native session")

require("main/theme.h", r"kOrdinal\s*=\s*rgb\(171,\s*168,\s*157\)",
        "row ordinals must use their own tone between rule and label type")
require("main/ui.cpp",
        r"void draw_selection_plate\([\s\S]{0,220}rows\(false\)[\s\S]{0,120}fill_rect\([\s\S]{0,60}kInk\)[\s\S]{0,60}clip\([\s\S]{0,60}rows\(true\)[\s\S]{0,40}unclip",
        "list inversion must be clipped to the travelling plate, not coloured per row")
require("main/ui.cpp",
        r"void draw_segment_meter\([\s\S]{0,600}fill_rect\(x, y \+ kSegH - 2, kSegW, 2, seat\)",
        "an empty meter step must keep a seat so the full range stays visible")
require("main/ui.cpp",
        r"SettingsRow::Volume\)[\s\S]{0,200}draw_segment_meter\(value_right, meter_y, 10, settings_level\.x",
        "volume must be a ten-step segment meter driven by its own spring")
require("main/ui.cpp",
        r"SettingsRow::BleProfile\)[\s\S]{0,200}draw_segment_selector\(value_right, meter_y, 3, s\.ble_profile",
        "the host channel must be a one-of-three segment selector")
require("main/ui.cpp",
        r"glow = 1\.f - motion::ease_out_cubic\(cell_press\[i\] / kPressTime\)",
        "key press feedback must attack instantly and ease out")
require("main/ui.cpp",
        r"chime_marker_x\.to\([\s\S]{0,140}chime_marker_y\.to\(",
        "the chime grid selection must travel on two springs")
require("tools/screenshot.py",
        r'"debug", "previews", "chime", "status", "signal", "stick"',
        "the chime, status, weak-link, screen-check and stick screens must be capturable")
require("main/firmware.h", r"esp_app_get_description\(\)->version",
        "the running firmware version must have exactly one accessor")
forbid("main/main.cpp", r"esp_app_get_description\(\)",
       "main must read the firmware version through firmware.h")
require("main/ui.cpp",
        r"firmware::version\(\)[\s\S]{0,300}"
        r"mix\(kPaper, kDim,\s*\n\s*static_cast<uint8_t>\(motion::clamp01\(intro\) \* 92\.f\)\);\s*\n"
        r"\s*draw_tracked_right\(build, kScreenW - 26, 12, 0, stamp, kPaper\);",
        "the splash's build stamp belongs in the trim with the registration "
        "marks: tight against the corner mark and faded towards the paper")

require("main/codex_micro_protocol.cpp", r"rpc_framer::Assembler usb_input;[\s\S]{0,120}rpc_framer::Assembler ble_input;", "USB and BLE must not share partial RPC state")
require("main/codex_micro_protocol.cpp", r"xQueueCreateStatic[\s\S]{0,1200}xQueueReceive", "protocol callbacks must hand reports to main task")
forbid("main/ble_store_apps_nvs.cpp", r"nvs_erase_all\(", "BLE migration must never erase user settings")
require("main/ble_store_apps_nvs.cpp",
        r"companion_ble_store_forget_all[\s\S]{0,700}erase_bond_keys[\s\S]{0,500}"
        r"memset\(peer_secs",
        "explicit BLE recovery must clear only persisted and in-memory bond records")
require("main/main.cpp",
        r"DebugSettingsRow::ResetBluetooth[\s\S]{0,180}ble_reset_armed[\s\S]{0,240}"
        r"companion_ble_forget_bonds",
        "BLE bond reset must require a second explicit Enter press")
require("main/store.cpp",
        r"esp_err_t open\([\s\S]{0,300}nvs_open_from_partition[\s\S]{0,1800}"
        r"last_error = e",
        "settings storage must preserve the concrete NVS failure code")
require("main/storage_partition.h",
        r"ESP_PARTITION_SUBTYPE_DATA_NVS,\s*\"apps_nvs\"[\s\S]{0,220}"
        r"ESP_PARTITION_SUBTYPE_DATA_NVS,\s*nullptr",
        "storage must prefer M5Apps' label and fall back by stable NVS subtype")
require("main/ble_store_apps_nvs.cpp", r"storage_partition::nvs_label\(\)",
        "BLE bonds must use the same runtime-discovered NVS partition as settings")
require("main/store.cpp", r"settings_dirty = false[\s\S]{0,260}settings_dirty = true", "failed settings writes must remain dirty")
require("main/input_event_queue.h", r"if \(repeat \|\| !is_release\(item\)\)\s+return false", "release edges must survive queue pressure")
require("main/keys.cpp", r"is_adv[\s\S]{0,300}Backend::None", "ADV TCA failure must not drive legacy matrix pins")
require("main/main.cpp", r"send_agent_key_to\(voice_gesture.transport[\s\S]{0,160}false", "G0 must release Agent Key on original transport")
require("main/link.cpp", r"discard_until_newline", "oversized diagnostic lines must be discarded through newline")
forbid("main/ble_transport.h", r"companion_receive_line",
       "the BLE text-channel entry point must stay removed until a GATT text characteristic exists")
require("main/main.cpp", r"store::flush\(\);\s*\n\s*vTaskDelay\(pdMS_TO_TICKS\(120\)\)",
        "returning to M5Apps must flush debounced settings before reboot")
require("tools/install.py", r"partition table backup[\s\S]{0,3000}verification failed", "partition edits must be backed up and verified")
require("main/ui.cpp", r"strcmp\(scene, \"live\"\)[\s\S]{0,120}canvas\.getBuffer", "live screenshots must expose the current framebuffer without demo-state mutation")
require(".github/workflows/host-tests.yml", r"SANITIZE[\s\S]{0,900}firmware-build", "CI must run sanitizers and firmware build")

if failures:
    for failure in failures:
        print(f"FAIL source_contracts: {failure}", file=sys.stderr)
    raise SystemExit(1)
print("PASS source_contracts")
