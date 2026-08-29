<h1 align="center">Codex Microputer ADV</h1>

<p align="center">
  <strong>Turn an M5Stack Cardputer ADV into a six-task hardware controller for Codex.</strong><br>
  Direct USB or Bluetooth Low Energy HID. No bridge, daemon, Wi-Fi connection, API key, or helper script.
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

## What it is

Codex Microputer ADV is an application for
[M5Apps](https://github.com/d4rkmen/M5Apps). It presents the Cardputer ADV to
the Codex desktop app as a native Codex Micro device and turns its keyboard,
display, speaker, USB, and Bluetooth radio into one dedicated control surface.

- Keep all six Codex Micro task slots visible at a glance.
- Open a task immediately with keys `1` through `6`.
- Use Codex's native dial, stick, action, send, and voice gestures.
- Hear distinct local cues when a task starts, needs input, completes, or fails.
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

The source contains a keyboard backend for the original Cardputer, but that
hardware is intended for development and is not release-qualified. Windows and
Linux host behavior has not been release-tested.

## Install

You need [M5Apps](https://github.com/d4rkmen/M5Apps) already installed on the
Cardputer ADV and a FAT32 or exFAT microSD card.

1. Download the latest **[`Codex.bin`](https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin)**.
2. Copy `Codex.bin` to the microSD card.
3. Boot M5Apps and open **Installer → SD card → Codex.bin**.
4. Launch **Codex** from the M5Apps launcher.
5. Open Codex on the Mac and connect `Codex Micro ADV` over USB or Bluetooth.

Updates use the same process: install the newer `Codex.bin` through M5Apps.

> [!IMPORTANT]
> `Codex.bin` is an **M5Apps application image**, not a complete device image.
> Do not flash it at address `0x0`; M5Apps owns the bootloader and partition
> table.

<details>
<summary><strong>Verify the release checksum</strong></summary>

Download
[`Codex.bin.sha256`](https://github.com/pfrankov/codex-microputer-adv/releases/latest/download/Codex.bin.sha256)
into the same directory and run:

```bash
shasum -a 256 -c Codex.bin.sha256
```

Release automation checks out the exact Git tag, verifies the firmware version,
runs the audit and regression suite, builds the image, preserves it as a GitHub
Actions artifact, and publishes both files.

</details>

## Connect

### USB

Connect the Cardputer with a data-capable USB cable and open Codex. A valid USB
Codex session takes priority over Bluetooth.

### Bluetooth

Open local settings with Tab, select one of the three **Host Channel** profiles,
and pair `Codex Micro ADV` using the six-digit code shown on the Cardputer.
Previously paired profiles reconnect automatically. When USB is no longer the
active Codex session, the selected Bluetooth profile resumes.

Press `1` through `6` to open the matching task slot.

## Controls

| Cardputer control | What it does |
| --- | --- |
| `1` … `6` | Select and open the matching Codex task slot |
| `[` / `]` | Rotate the native Codex dial |
| Enter or Space | Confirm an open dial surface; otherwise send the prepared message |
| `;` / `.` / `,` / `/` | Move the native stick **up / down / left / right** |
| `T` / `Y` / `U` / `I` / `O` / `P` | Trigger configurable action slots; by default `Y` is Approve and `U` is Reject |
| `A` | Trigger the combined `ACT10_ACT11` action |
| Hold `G0` | Hold Codex push-to-talk for the selected task; the Mac records the audio |
| `-` | Mute local sound or restore the previous volume |
| `=` | Open or close the on-device key map |
| Tab | Open or close local settings |
| Option+Tab | Open or close developer tools |
| Backtick | Go back locally, or ask Codex to close an active dial surface |

Holding a dial or stick key repeats the gesture. When the display is dark, the
first key press only wakes it; press the key again to perform the action.

<details>
<summary><strong>Exact native mapping</strong></summary>

- `1` … `6` send `AG00` … `AG05` press and release.
- `[` sends `ENC_CW`; `]` sends `ENC_CC`.
- Enter or Space sends `ENC` on the dial surface and `ACT12` on the task deck.
- `T` … `P` send `ACT06` … `ACT11`; `A` sends `ACT10_ACT11`.
- Holding `G0` holds the selected `AGxx` and `ACT10` until physical release.
- The four stick keys send directional joystick impulses.

Codex owns the meaning of the dial and action slots, so model selection,
reasoning effort, scrolling, Skills, New Chat, and custom commands follow the
current host configuration rather than being hardcoded in firmware.

Backslash is intentionally unassigned. Delete emits
`CCP_INTERRUPT|<thread-id>` only on the USB Serial/JTAG diagnostic channel; it
is not a native Codex Micro interrupt gesture and is unavailable over
Bluetooth.

</details>

## Task states

| State | Display |
| --- | --- |
| Running | Blue |
| Action required | Orange |
| Completed, not yet viewed | Green |
| Completed and viewed | Light grey |
| Error | Black with a red mark |
| Idle | Pale fill |
| Unassigned | Pale outline |

Task identity is always conveyed by number and position, not colour alone. A
bottom rail marks the selected task, and selection stays synchronized between
Codex and the Cardputer.

A real status change can expand its slot across the display, play the matching
sound, hold briefly, and return. Repeated publication of the same state does not
replay the event. Dial, stick, and voice gestures each have a dedicated visual
surface without inventing data the native protocol does not provide.

## Settings and device behavior

Tab opens local settings for Bluetooth host profile, volume, startup chime, and
returning to M5Apps. The defaults are 60% volume, startup sound enabled, and the
`CLOUD` composition.

Option+Tab opens developer tools: USB HID control, production screen previews,
Chime Lab, and Status Debug. Status Debug exposes a
`-300..+300 ms status-audio offset` in 25 ms steps alongside status debounce and
production state checks.

The display follows Codex Micro lighting when available. Without usable host
lighting, it dims after 15 seconds and switches off after a further three
minutes. The first key after darkness wakes the panel without triggering its
action.

## Privacy and limitations

The firmware makes no OpenAI API calls, stores no API keys, has no Wi-Fi
connection, and runs no background network service. The Cardputer microphone is
not used: Codex voice input records on the Mac. Local settings and Bluetooth
bonds are stored only in the firmware's `codex_ccp2` namespace inside M5Apps'
shared `apps_nvs` partition.

Current boundaries:

- the native protocol exposes exactly six task slots;
- it does not publish task titles, timestamps, token usage, progress, or
  arbitrary chat history to the device;
- only one Bluetooth host profile is active at a time;
- the original Cardputer, Windows, and Linux are not release-qualified;
- Codex Micro is not a public stable protocol, so a future desktop update may
  require a firmware update.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Codex does not detect the device | Confirm Codex Micro support and use a data-capable USB cable |
| Bluetooth is paired but inactive | Disconnect an active USB session and verify the selected Host Channel |
| A key only wakes the display | This is intentional after sleep; press it again |
| The release file is invalid | Download `Codex.bin` and its checksum again from the latest release |

## Build from source

```bash
git clone https://github.com/pfrankov/codex-microputer-adv.git
cd codex-microputer-adv

./tools/setup.sh
./tools/test.sh
./tools/build.sh
```

The setup script installs pinned project-local dependencies under `.deps/`,
including ESP-IDF 5.5.3 and the tested M5Cardputer UserDemo revision. The M5Apps
image is written to `dist/Codex.bin`.

<details>
<summary><strong>Development install and verification</strong></summary>

After M5Apps has created the `Codex` application partition once:

```bash
source tools/env.sh
./tools/build.sh
./tools/install.py
```

The installer writes only the existing `Codex` OTA partition, verifies the flash
digest, selects it through M5Apps metadata, and launches it. Partition creation
or resizing requires the explicit `--create-partition` option.

Run the full repository checks before a firmware change:

```bash
./tools/audit_public_tree.py
./tools/test.sh
./tools/build.sh
```

After flashing on macOS, verify the native exchanges with:

```bash
source tools/env.sh
./tools/verify_codex_connection.py
```

For a device-side demo without a live Codex session, run
`./tools/devlink.py --demo`.

</details>

## Documentation

- [`CHANGELOG.md`](CHANGELOG.md) — release history.
- [`PRODUCT.md`](PRODUCT.md) — product purpose and principles.
- [`DESIGN.md`](DESIGN.md) — visual, motion, and sound system.
- [`main/`](main/) — firmware and native protocol implementation.
- [`tests/`](tests/) — host-side regression tests.
- [`tools/`](tools/) — setup, build, install, verification, and diagnostics.

## Project status

This is an independent community project. It is not affiliated with or endorsed
by OpenAI, M5Stack, or Work Louder. Codex and Codex Micro are trademarks of
their respective owners.

The source is available under the [Apache License 2.0](LICENSE). Embedded fonts
retain their SIL Open Font License notices in `assets/fonts/`.
