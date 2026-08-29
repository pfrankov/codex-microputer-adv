<h1 align="center">Codex Microputer ADV</h1>

<p align="center">
  <strong>A native six-task hardware controller for Codex, built for the M5Stack Cardputer ADV.</strong><br>
  USB or Bluetooth Low Energy. No bridge, daemon, Wi-Fi connection, API key, or helper script.
</p>

<p align="center">
  <a href="https://github.com/pfrankov/codex-microputer-adv/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/pfrankov/codex-microputer-adv"></a>
  <a href="https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin"><img alt="Download Codex.bin" src="https://img.shields.io/badge/download-Codex.bin-2f6bd8.svg"></a>
  <a href="https://github.com/pfrankov/codex-microputer-adv/actions/workflows/host-tests.yml"><img alt="Build and tests" src="https://github.com/pfrankov/codex-microputer-adv/actions/workflows/host-tests.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="License: Apache 2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue.svg"></a>
</p>

<p align="center">
  <a href="https://github.com/user-attachments/assets/ce767565-add0-4f31-b462-34a8dcb9e293">
    <img src="screenshots/demo_poster.jpg" alt="Codex Microputer ADV controlling Codex from an M5Stack Cardputer ADV" width="100%">
  </a>
</p>

<p align="center"><sub>Click the image to watch the demo.</sub></p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#controls">Controls</a> ·
  <a href="#compatibility">Compatibility</a> ·
  <a href="#build-from-source">Build from source</a>
</p>

## Overview

Codex Microputer ADV runs inside [M5Apps](https://github.com/d4rkmen/M5Apps)
and presents the Cardputer ADV to Codex as a native Codex Micro device. Its
purpose is simple: keep six active tasks visible and make the controls used
throughout the day physical.

- See all six task slots and their current state at a glance.
- Open a task immediately with keys `1` through `6`.
- Use the native Codex dial, stick, action, send, and voice gestures.
- Connect directly over USB HID or Bluetooth Low Energy HID.
- Keep M5Apps and the rest of the Cardputer intact.

| Task deck | Dial | Stick | Voice |
| --- | --- | --- | --- |
| ![Six-task Codex deck](screenshots/deck.png) | ![Codex dial control](screenshots/composer.png) | ![Codex analog-stick control](screenshots/stick.png) | ![Codex voice input](screenshots/recording.png) |

> [!NOTE]
> The firmware controls an existing Codex desktop session. It does not run
> Codex or an agent on the Cardputer.

## Compatibility

| Component | Supported path |
| --- | --- |
| Device | **M5Stack Cardputer ADV** with 8 MB flash |
| Host | **macOS 14 or later** is the release-tested path |
| Codex | A desktop build with Codex Micro support |
| Launcher | [M5Apps](https://github.com/d4rkmen/M5Apps) |
| Connection | Native USB HID and Bluetooth Low Energy HID |

The source includes a GPIO-matrix keyboard backend for the original Cardputer,
but that hardware is intended for development and is not part of the release
qualification matrix. Windows and Linux host behavior has not been
release-tested.

## Install

You need M5Apps already installed on the Cardputer ADV and a FAT32 or exFAT
microSD card.

1. Download the latest **[`Codex.bin`](https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin)**.
2. Copy `Codex.bin` to the microSD card.
3. Boot M5Apps and open **Installer → SD card → Codex.bin**.
4. Launch **Codex** from the M5Apps launcher.
5. Open Codex on the Mac and complete the normal Codex Micro connection flow
   for `Codex Micro ADV` over USB or Bluetooth.

To update, install the newer `Codex.bin` through M5Apps using the same steps.

> [!IMPORTANT]
> `Codex.bin` is an **M5Apps application image**, not a complete device image.
> Do not flash it at address `0x0`; M5Apps owns the bootloader and partition
> table.

<details>
<summary><strong>Verify the release checksum</strong></summary>

Download
[`Codex.bin.sha256`](https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin.sha256)
into the same directory as `Codex.bin`, then run:

```bash
shasum -a 256 -c Codex.bin.sha256
```

Every release binary is built from its exact Git tag, checked against the
firmware version, tested, preserved as a GitHub Actions artifact, and published
with this checksum.

</details>

## Connect and start

### USB

Connect the Cardputer with a data-capable USB cable and open Codex. A valid USB
Codex session takes priority over Bluetooth.

### Bluetooth

Open local settings with Tab, choose one of the three **Host Channel** profiles,
and pair `Codex Micro ADV` using the six-digit code shown on the Cardputer.
Previously paired profiles reconnect without entering the code again.

When USB is no longer the active Codex session, the selected Bluetooth profile
resumes automatically. Only one Bluetooth host profile is active at a time.

Press `1` through `6` to open the matching Codex task slot.

## Controls

### Everyday controls

| Cardputer control | What it does |
| --- | --- |
| `1` … `6` | Select and open the matching Codex task slot |
| `[` / `]` | Rotate the native Codex dial |
| Enter or Space | Confirm an open dial surface; otherwise send the prepared composer message |
| `;` / `.` / `,` / `/` | Move the native stick **up / down / left / right** |
| `T` / `Y` / `U` / `I` / `O` / `P` | Trigger Codex-configurable action slots; by default `Y` is Approve and `U` is Reject |
| `A` | Trigger the combined `ACT10_ACT11` action |
| Hold `G0` | Hold Codex push-to-talk for the selected task; the computer records the audio |
| `-` | Mute local sound or restore the previous volume |
| `=` | Open or close the on-device key map |
| Tab | Open or close local settings |
| Option+Tab | Open or close developer and diagnostic tools |
| Backtick | Go back locally, or ask Codex to close an active dial surface |

Holding the dial or stick keys repeats the gesture. When the display is dark,
the first key press only wakes it; repeat the key to perform the action.

<details>
<summary><strong>Exact native HID mapping and edge behavior</strong></summary>

| Cardputer control | Native output |
| --- | --- |
| `1` … `6` | `AG00` … `AG05` press and release |
| `[` | `ENC_CW` detent |
| `]` | `ENC_CC` detent |
| Enter or Space on the dial page | `ENC` press and release |
| Enter or Space on the task deck | `ACT12` press and release |
| `T` … `P` | `ACT06` … `ACT11` press and release |
| `A` | `ACT10_ACT11` press and release |
| Hold `G0` | Selected `AGxx` plus held `ACT10`, both released physically |
| `;` / `.` / `,` / `/` | Joystick up / down / left / right impulse |

Codex owns the meaning of the dial and action slots. Model selection, reasoning
effort, scrolling, Skills, New Chat, and custom commands therefore follow the
current Codex configuration rather than being hardcoded in the firmware.

Backslash is intentionally unassigned. Delete emits
`CCP_INTERRUPT|<thread-id>` only on the USB Serial/JTAG diagnostic channel; it
is not a native Codex Micro interrupt gesture and is unavailable over
Bluetooth.

</details>

## Task states and interface

| State | Display |
| --- | --- |
| Running | Blue |
| Action required | Orange |
| Completed, not yet viewed | Green |
| Completed and viewed | Light grey |
| Error | Black with a red mark |
| Idle | Pale fill |
| Unassigned | Pale outline |

Slot identity is always conveyed by number and position, not colour alone. A
bottom rail marks the selected task. Selecting a task in either Codex or on the
Cardputer moves the same selection on the other device.

A real status transition can expand the affected slot across the display, play
its matching local sound, hold briefly, and return. Repeated publication of the
same state does not replay the event.

Turning the dial opens a surface that mirrors the temporary Codex Micro lamp
state. Stick input opens a directional surface and closes automatically after
the impulse. Holding `G0` expands the selected task and keeps the voice gesture
active until the button is released or the connection is lost.

## Settings

Open local settings with Tab.

| Setting | Behavior |
| --- | --- |
| Host Channel | Select Bluetooth profile 1, 2, or 3 |
| Volume | Adjust local sound from 0 to 100 |
| Startup Chime | Enable or disable the startup sound |
| Return to M5Apps | Requires two confirmations to prevent an accidental exit |

The defaults are 60% volume, startup sound enabled, and the `CLOUD` startup
composition.

<details>
<summary><strong>Developer and diagnostic tools</strong></summary>

Open these tools with Option+Tab.

- **USB HID:** enable native USB HID or use Bluetooth only.
- **Screen Checks:** preview production Splash, Pairing PIN, Dial, and Stick
  screens without operating Codex.
- **Chime Lab:** choose and audition one of ten startup compositions.
- **Status Debug:** exercise production Running, Input, Done, Error, and Idle
  rendering and audio, choose status debounce, and adjust the status-audio
  offset across `-300..+300 ms` in 25 ms steps.

</details>

## Connection, privacy, and storage

The device becomes active only after it receives valid Codex Micro protocol
traffic. A live USB session wins over Bluetooth; the selected Bluetooth profile
takes over when USB is no longer active.

The firmware:

- makes no OpenAI API calls and stores no API keys;
- has no Wi-Fi connection and does not run background network services;
- does not run agents on the Cardputer;
- does not record or transmit audio from the Cardputer microphone;
- stores only local settings and Bluetooth bonds in its own `codex_ccp2`
  namespace inside M5Apps' shared `apps_nvs` partition;
- keeps USB Serial/JTAG available for diagnostics, not as a hidden bridge for
  normal operation.

Battery and charging state are reported to Codex. The display follows Codex
Micro lighting when available, filters brief all-off transitions, and otherwise
uses a local fallback: dim after 15 seconds and switch off after a further
three minutes. Task or host activity wakes it.

## Deliberate limitations

- The native protocol exposes exactly six task slots.
- It does not publish task titles, timestamps, token usage, progress, or
  arbitrary remote chat history to the device.
- Only one of the three Bluetooth host profiles can be active at a time.
- The Cardputer microphone is not used for Codex voice input.
- Windows and Linux host behavior is not currently release-tested.
- The original Cardputer is not part of the release qualification matrix.
- Codex Micro is not a public stable protocol; a future Codex desktop update
  may require a firmware update.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Codex does not detect the device | Confirm that the desktop build supports Codex Micro and that the USB cable carries data |
| Bluetooth is paired but inactive | Disconnect an active USB Codex session and confirm the selected Host Channel |
| A key only wakes the display | This is intentional after sleep; press the key again to perform the action |
| The release file is missing or invalid | Download `Codex.bin` and its checksum again from the latest release |

## Build from source

Requirements: Git, Python 3, Bash, CMake, Ninja, and enough disk space for a
project-local ESP-IDF toolchain.

```bash
git clone https://github.com/pfrankov/codex-microputer-adv.git
cd codex-microputer-adv

./tools/setup.sh
./tools/test.sh
./tools/build.sh
```

`setup.sh` installs pinned dependencies under `.deps/`: ESP-IDF 5.5.3, the
tested M5Cardputer UserDemo revision, and the required HID compatibility patch.
The finished M5Apps application is written to `dist/Codex.bin`.

<details>
<summary><strong>Update over USB during development</strong></summary>

After M5Apps has created the `Codex` application partition once:

```bash
source tools/env.sh
./tools/build.sh
./tools/install.py
```

The installer auto-detects `/dev/cu.usbmodem*`, rejects a stale staged image,
writes only the existing `Codex` OTA partition, verifies the flash digest,
selects the partition through M5Apps OTA metadata, and launches it.

Creating or resizing a partition requires `--create-partition`. This option
modifies the partition table and is deliberately never enabled by default.

</details>

<details>
<summary><strong>Verification and test suite</strong></summary>

Run all repository checks before a firmware change:

```bash
./tools/audit_public_tree.py
./tools/test.sh
./tools/build.sh
```

The host suite covers status reduction, event debounce and queuing, animation
continuity, HID framing, session synchronization, keyboard mapping, adaptive
Bluetooth power, display fade, and source-level safety contracts. GitHub
Actions runs the tests with sanitizers and performs a clean pinned ESP-IDF
firmware build.

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

</details>

<details>
<summary><strong>Capture device screenshots</strong></summary>

The firmware can render representative scenes through the production UI and
return the RGB565 frame over USB Serial/JTAG:

```bash
python3 tools/screenshot.py --scene deck --output screenshot.png
python3 tools/screenshot.py --scene live --output current-screen.png
python3 tools/screenshot.py --scene all --output screenshots
```

Available scenes are `live`, `splash`, `pairing`, `deck`, `recording`,
`composer`, `settings`, `debug`, `previews`, `chime`, `status`, `signal`, and
`stick`.

Screenshot traffic is USB-only and does not congest the native Bluetooth HID
session. The checked-in gallery is in [`screenshots/`](screenshots/).

</details>

## Project documentation

- [`CHANGELOG.md`](CHANGELOG.md) — release history and notable changes.
- [`PRODUCT.md`](PRODUCT.md) — product purpose and design principles.
- [`DESIGN.md`](DESIGN.md) — visual, motion, and sound system.
- [`main/`](main/) — firmware, native transports, protocol, UI, audio, storage,
  and keyboard input.
- [`tests/`](tests/) — host-side regression tests compiled with warnings as
  errors.
- [`tools/`](tools/) — reproducible setup, build, install, verification,
  diagnostics, and screenshot tools.
- [`patches/`](patches/) — pinned ESP-IDF HID compatibility patch.

## Project status and trademarks

This is an independent community project. It is not affiliated with or endorsed
by OpenAI, M5Stack, or Work Louder. Codex and Codex Micro are trademarks of
their respective owners.

Project source is available under the [Apache License 2.0](LICENSE). Embedded
fonts retain their SIL Open Font License notices in `assets/fonts/`.
