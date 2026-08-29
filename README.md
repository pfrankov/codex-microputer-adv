# Codex Microputer ADV

<p align="center">
  <a href="https://github.com/pfrankov/codex-microputer-adv/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/pfrankov/codex-microputer-adv"></a>
  <a href="https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin"><img alt="Download Codex.bin" src="https://img.shields.io/badge/download-Codex.bin-2f6bd8.svg"></a>
  <a href="https://github.com/pfrankov/codex-microputer-adv/actions/workflows/host-tests.yml"><img alt="Build and tests" src="https://github.com/pfrankov/codex-microputer-adv/actions/workflows/host-tests.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="License: Apache 2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue.svg"></a>
</p>

| Task deck | Dial | Stick | Voice |
| --- | --- | --- | --- |
| ![Six-task Codex deck](screenshots/deck.png) | ![Codex dial control](screenshots/composer.png) | ![Codex analog-stick control](screenshots/stick.png) | ![Codex voice input](screenshots/recording.png) |

<p align="center">
  <video src="https://github.com/user-attachments/assets/ce767565-add0-4f31-b462-34a8dcb9e293" poster="screenshots/demo_poster.jpg" controls muted loop playsinline width="100%"></video>
</p>

**Codex Microputer ADV turns an M5Stack Cardputer ADV into a native hardware
controller for Codex.** It implements the Codex Micro HID protocol directly
over USB or Bluetooth Low Energy, so it needs no bridge, daemon, Wi-Fi
connection, OpenAI API key, or background script.

The device mirrors the six Codex Micro task slots, opens a task with one number
key, exposes the native dial, stick, command, send, and voice gestures, and
provides glanceable status animations and local sound.

> [!IMPORTANT]
> This is an independent community project. It is not affiliated with or
> endorsed by OpenAI, M5Stack, or Work Louder. Codex and Codex Micro are
> trademarks of their respective owners.

## At a glance

- **Six native task slots.** `1` through `6` select and open the matching Codex
  task; host-side selection is reflected back on the Cardputer.
- **Native controls, not keyboard shortcuts.** Dial, analog stick, Agent,
  action-slot, voice, and send gestures are emitted through Codex Micro's vendor
  HID protocol.
- **USB and BLE.** A live USB Codex session takes priority; BLE resumes when USB
  is no longer the active session.
- **Three BLE host channels.** Each channel has a distinct device identity,
  while bond records are persisted for reconnects.
- **No audio leaves the Cardputer.** Holding `G0` activates Codex voice input for
  the selected task, but the computer records the audio.
- **Battery-aware.** Battery and charging state are reported to Codex; the LCD
  stays quiet unless charge falls below 15%.
- **M5Apps-native.** Codex runs as an M5Apps application instead of replacing
  the launcher or the rest of the device.

## Compatibility

| Component | Supported / tested |
| --- | --- |
| Device | **M5Stack Cardputer ADV**, 8 MB flash |
| Host | **macOS 14 or later** is the release-tested path |
| Codex | A desktop build with Codex Micro support |
| Transport | Native USB HID and Bluetooth Low Energy HID |
| Launcher | [M5Apps](https://github.com/d4rkmen/M5Apps) |

The source also contains a GPIO-matrix keyboard backend for the original
Cardputer. It is useful for development, but that hardware is not part of the
release qualification matrix. Windows and Linux host behavior has not been
release-tested.

## Install

### Install the latest release

You need an M5Stack Cardputer ADV with M5Apps already installed and a FAT32 or
exFAT microSD card.

1. Download **[`Codex.bin`](https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin)** from the latest release.
2. Optionally download [`Codex.bin.sha256`](https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin.sha256) and verify the image:

   ```bash
   shasum -a 256 -c Codex.bin.sha256
   ```

3. Copy `Codex.bin` to the microSD card.
4. Boot M5Apps and open **Installer → SD card → Codex.bin**.
5. Launch **Codex** from the M5Apps launcher.
6. Connect the detected `Codex Micro ADV` device to Codex over USB or Bluetooth.

> [!IMPORTANT]
> `Codex.bin` is an **M5Apps application image**, not a complete device image.
> Do not flash it at address `0x0`; M5Apps owns the bootloader and partition
> table.

Release binaries are reproducible from the tagged source. The release workflow
checks out the exact release tag, verifies that the tag matches `PROJECT_VER`,
runs the public-tree audit and host regression suite, builds `dist/Codex.bin`,
preserves it as a GitHub Actions artifact, and attaches both `Codex.bin` and its
SHA-256 checksum to the GitHub release.

### Build from source

Build requirements are Git, Python 3, Bash, CMake, Ninja, and enough disk space
for a project-local ESP-IDF toolchain.

```bash
git clone https://github.com/pfrankov/codex-microputer-adv.git
cd codex-microputer-adv

./tools/setup.sh
./tools/test.sh
./tools/build.sh
```

`setup.sh` installs pinned dependencies under `.deps/`: ESP-IDF 5.5.3, the
tested M5Cardputer UserDemo revision, and the required HID compatibility patch.
The finished M5Apps application is written to `dist/Codex.bin`; install it
through M5Apps exactly like the release image above.

### Update over USB during development

After M5Apps has created the `Codex` application partition once:

```bash
source tools/env.sh
./tools/build.sh
./tools/install.py
```

The installer auto-detects `/dev/cu.usbmodem*`, rejects a stale staged image,
writes only the existing `Codex` OTA partition, verifies the flash digest,
selects the partition through M5Apps OTA metadata, and launches it.

Creating or resizing a partition requires `--create-partition`. That option
modifies the partition table and is deliberately never enabled by default.

## Controls

The table below reflects the current firmware mapping in `main/key_layout.h`
and the contextual behavior in `main/main.cpp`.

### Codex controls

| Cardputer control | Actual behavior |
| --- | --- |
| `1` … `6` | Send `AG00` … `AG05` press/release and open the matching task slot. Native double-tap behavior is preserved. |
| `[` | Send the `ENC_CW` dial detent. |
| `]` | Send the `ENC_CC` dial detent. |
| Enter or Space | On the task deck: send `ACT12` press/release to submit the prepared composer message. While the dial page is open: send an `ENC` press/release to confirm the current host control. |
| `;` / `.` / `,` / `/` | Send analog-stick **up / down / left / right** impulses. |
| `T` / `Y` / `U` / `I` / `O` / `P` | Send configurable command slots `ACT06` … `ACT11`. With the default Codex Micro mapping, `Y` is Approve (`ACT07`) and `U` is Reject (`ACT08`). |
| `A` | Send the combined configurable slot `ACT10_ACT11`. |
| Hold `G0` | Select the current task, hold `ACT10` for Codex push-to-talk, and release both gestures when the button is released. The computer microphone records the audio. |
| `\` | Unassigned. It only wakes the display if the panel is asleep. |

Codex owns the meaning of the dial and `ACT06` … `ACT12`. Model selection,
reasoning effort, scrolling, Skills, New Chat, and custom commands therefore
continue to follow the current host configuration rather than being hardcoded
in firmware.

`Delete` is **not a native Codex Micro interrupt gesture** in the current code.
It emits `CCP_INTERRUPT|<thread-id>` only on the USB Serial/JTAG diagnostic
channel. It is not available over BLE and should not be treated as part of the
bridge-free control set.

### Device controls

| Cardputer control | Device behavior |
| --- | --- |
| `-` | Mute local sound or restore the previous volume. |
| `=` | Open or close the on-device key map. |
| Tab | Open or close local settings. |
| Option+Tab | Open or close developer and diagnostic tools. |
| Backtick | Local Back/Esc. Returns to the deck, closes the stick page, or asks Codex to close an active dial surface. It never exits to M5Apps. |
| Hold `[`, `]`, `;`, `.`, `,`, or `/` | Repeat the corresponding dial or stick impulse after 420 ms, then every 120 ms. |
| Any key while dark | Wake the panel only. The first press is intentionally not forwarded to Codex. |

Physical press and release edges are preserved for Agent keys, command slots,
and contextual dial confirmation. The firmware does not synthesize a tap when
Codex needs the duration of a real gesture.

## Interface behavior

### Six-slot task deck

The normal view is a six-column instrument panel with one column per native
Codex Micro task slot:

- **Blue:** running.
- **Orange:** action required.
- **Green:** completed and not yet viewed.
- **Light grey:** completed and viewed.
- **Black with a red mark:** error.
- **Pale fill:** idle.
- **Pale outline:** unassigned.

A bottom rail marks the selected task. Selecting a task on either device moves
the same selection on the other. A real status transition may expand its slot
across the full display, play the matching sound, hold briefly, and return.
Repeated publication of the same state does not replay the event.

### Dial page

Turning `[` or `]` opens the dial page immediately, so the first detent is
visible rather than happening blind. The host's temporary Codex Micro lamp
state is mirrored into the dial's light ring without guessing whether the host
is showing a model picker, reasoning control, scroll position, or custom action.

- Turn with `[` and `]`.
- Confirm with Enter or Space.
- Leave with Backtick.
- If no click is made and the host lights no response, the page closes three
  seconds after the last detent.
- Once the host responds or the user confirms, the page waits for Codex's own
  close frame or Backtick instead of imposing a local timeout.

### Stick page

`;`, `.`, `,`, and `/` send native stick impulses and open a matching visual
surface. Because Codex reports no persistent stick state, this page always
closes three seconds after the last direction.

### Voice input

Holding `G0` expands the selected task across the display and holds the native
Codex voice gesture. Releasing the button ends the gesture. A transport change
or disconnect forcibly releases it so the display cannot claim that recording
is still active.

The Cardputer microphone is not used, and no audio is stored or transmitted by
the firmware.

## Settings and diagnostics

### Tab: local settings

| Setting | Behavior |
| --- | --- |
| Host Channel | Select BLE profile 1, 2, or 3 with Left/Right. The choice is saved and the radio switches immediately. |
| Volume | Adjust from 0 to 100 in steps of 10 with Left/Right; Enter increases one step. |
| Startup Chime | Toggle the startup sound on or off. |
| Return to M5Apps | Requires Enter twice to prevent an accidental reboot out of the app. |

The default local settings are 60% volume, startup sound enabled, and the
`CLOUD` startup composition.

### Option+Tab: developer tools

- **USB HID:** enable native USB HID or switch to Bluetooth-only mode.
- **Screen Checks:** inert previews of Splash, Pairing PIN, Dial, and Stick.
  Preview keys cannot operate Codex; Backtick is the only action.
- **Chime Lab:** select one of ten startup compositions with the arrows and play
  it with Enter. The selected composition is saved.
- **Status Debug:** choose a 100, 200, 300, or 500 ms status debounce; adjust
  a -300..+300 ms status-audio offset in 25 ms steps; and exercise the real
  Running, Input, Done, Error, and Idle rendering/audio paths.

The current defaults are a 100 ms status debounce and a +200 ms audio offset.

## Connection, pairing, and storage

A transport becomes active only after the firmware receives valid Codex Micro
protocol traffic. A live USB session wins over BLE; if USB disappears or stops
being the active Codex session, the selected BLE channel takes over.

Each BLE channel uses a separate device identity. Bond records are persisted
in shared storage, so previously paired hosts reconnect without entering the
passkey again. The Cardputer displays the six-digit passkey during first
pairing. While connected, the firmware monitors RSSI, adjusts transmit power,
and may switch PHY to improve weak links.

Settings and BLE data live in the `codex_ccp2` namespace of M5Apps' shared
`apps_nvs` partition. The firmware never erases that partition and stores no
Codex, OpenAI, GitHub, Wi-Fi, or API credentials.

USB serial remains available for diagnostics, screenshots, and development
tools. It is not a hidden bridge for normal Codex operation, and diagnostic
text is not transported over BLE.

## Brightness and sleep

While connected, the LCD follows Codex Micro lighting state and brightness:

- Host all-off transitions are debounced for 750 ms so temporary lighting
  rebuilds do not blank the display.
- A stable host-off state dims the LCD to roughly 10% and turns it fully off
  after a three-minute grace period.
- Without usable host lighting state, the local fallback dims after 15 seconds
  and turns off after 3 minutes 15 seconds.
- Task or host activity wakes the panel.
- The first physical input after darkness wakes only; repeat the input to
  perform the action.

## Deliberate limitations

- No OpenAI API calls and no API keys.
- No Wi-Fi connection.
- No agents run on the Cardputer.
- No Cardputer microphone streaming.
- No task titles, timestamps, token usage, progress, or arbitrary remote chat
  discovery: the native protocol does not publish those fields.
- Exactly six native task slots.
- Only one BLE host channel is active at a time.
- Windows and Linux are not currently release-tested.
- The Codex Micro protocol is not a public stable API. A future Codex desktop
  update may require a firmware update.

## Development and verification

Run all repository checks before a firmware change:

```bash
./tools/audit_public_tree.py
./tools/test.sh
./tools/build.sh
```

The host suite covers status reduction, event debounce and queuing, animation
continuity, HID framing, session synchronization, keyboard mapping, adaptive
BLE power, display fade, and source-level safety contracts. GitHub Actions runs
the tests with sanitizers and performs a clean pinned ESP-IDF firmware build.

After flashing on macOS:

```bash
source tools/env.sh
./tools/verify_codex_connection.py
```

The verifier checks the local Codex desktop log for successful
`v.oai.rgbcfg`, `v.oai.thstatus`, and `device.status` exchanges and fails on
control-plane initialization errors.

For device-side demos without a live Codex session:

```bash
./tools/devlink.py --demo
```

## Capture device screenshots

The firmware can render stable representative scenes through the production UI
and return the RGB565 frame over USB Serial/JTAG:

```bash
python3 tools/screenshot.py --scene deck --output screenshot.png
python3 tools/screenshot.py --scene live --output current-screen.png
python3 tools/screenshot.py --scene all --output screenshots
```

Available scenes are `live`, `splash`, `pairing`, `deck`, `recording`,
`composer`, `settings`, `debug`, `previews`, `chime`, `status`, `signal`, and
`stick`.

Screenshot traffic is USB-only and does not congest the native BLE HID session.
Representative demo state exists only for the capture and never replaces the
live task deck. The checked-in gallery is in [`screenshots/`](screenshots/).

## Repository layout

- `main/` — firmware, native transports, protocol, UI, audio, storage, and
  keyboard input.
- `tests/` — host-side regression tests built with warnings as errors.
- `tools/` — pinned setup, build, install, verification, diagnostics, and
  screenshot tools.
- `.github/workflows/release-binary.yml` — reproducible release build and binary
  publication.
- `patches/` — the pinned ESP-IDF HID compatibility patch.
- `DESIGN.md` and `PRODUCT.md` — visual and product contracts.

## License

Project source is available under the [Apache License 2.0](LICENSE). Embedded
fonts retain their SIL Open Font License notices in `assets/fonts/`.
