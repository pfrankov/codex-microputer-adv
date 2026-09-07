# Claude Code: optional USB status monitor

This is an **experimental, opt-in, read-only host adapter**, not built-in Claude
Code support for Codex Micro hardware. It uses Claude Code's documented command
hooks and the firmware's existing USB HID RPC channel. The native Codex desktop
path remains unchanged and still needs no bridge. No firmware changes or
reflashing are required by this adapter.

## Which native Claude Code interfaces can be reused?

| Interface | Available information or action | This adapter |
| --- | --- | --- |
| Command hooks | Prompt submission, tool execution, permission waits, response stop/failure, session end | Used for six-slot lifecycle monitoring |
| `statusLine` JSON | Model, context use, estimated session cost, and optional subscription limit fields | Not used: the existing Micro HID display protocol has no numeric-stat fields |
| `PermissionRequest` hook output | Allow or deny a particular permission request | Not used: this monitor never emits decisions or changes permissions |
| `claude agents --json` | Built-in listing of background sessions | Not used: ordinary local interactive sessions are observed through hooks instead |
| OpenTelemetry | Usage and performance telemetry | Not used; no collector is needed for lifecycle events |

No documented Claude Code implementation of the Micro HID protocol was found.
This adapter implements the host side explicitly. It does not scrape terminal
output, read private Claude databases or transcripts, call Anthropic APIs, or
require an API key. Native Remote Control is not treated as a third-party HID
interface.

## Setup

The initial adapter targets **macOS, USB, Python 3.10 or later**, and a current
Claude Code CLI that supports the documented events, including `StopFailure`.
Bluetooth and other host platforms are not enabled here. Hardware acceptance
is still required; automated tests use a simulated HID device.

From a checkout containing this change, install the optional host dependency
outside the repository:

```bash
python3 -m venv "$HOME/.local/share/codex-microputer/claude-venv"
PY="$HOME/.local/share/codex-microputer/claude-venv/bin/python"
"$PY" -m pip install -r tools/requirements-claude-code.txt

# Generate a separate, session-scoped configuration. No existing file is edited.
MICRO_SETTINGS="$(mktemp)"
"$PY" tools/claude_code.py settings > "$MICRO_SETTINGS"

# Run from the project you want Claude Code to work on.
claude --settings "$MICRO_SETTINGS"
```

The generated hook commands contain absolute, shell-quoted paths to this
checkout, its Python interpreter, and the state directory. Regenerate the file
when moving the checkout or virtual environment. Reuse the same settings file
for the local sessions you want to observe; start Claude from each desired
project directory. Keep the file until those sessions exit.

Claude Code merges hook entries across settings sources. The generated file
contains only `hooks`: it does not set permissions, change models, replace a
status line, or disable existing hooks. Verify the effective entries with
`/hooks` and resolve any workspace-trust prompt normally. Managed policy,
`disableAllHooks`, `--bare`, or safe mode can prevent these hooks from running;
the adapter does not bypass them.

In another terminal, from this checkout:

```bash
PY="$HOME/.local/share/codex-microputer/claude-venv/bin/python"

# First QUIT Codex/ChatGPT desktop and other Micro hosts, then connect over USB.
"$PY" tools/claude_code.py monitor --codex-closed
```

`--codex-closed` is an explicit ownership confirmation, not automatic detection
of a competing application. **Do not run this monitor and Codex as simultaneous
hosts of one device.** It opens only the vendor RPC collection of exactly one
USB `Codex Micro ADV` device, not its keyboard collection. No device or multiple
matching devices produces a diagnostic and bounded reconnect attempts.

Nothing is installed as a service. To return to Codex, stop the monitor with
Ctrl+C before reopening Codex. Omit `--settings` on future Claude launches to
remove this integration, and remove the temporary file after its sessions end.

## What the device shows

| Observed main-session event | Device state |
| --- | --- |
| `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `PostToolUseFailure` | Running (blue) |
| `PermissionRequest`, permission/elicitation notification, `AskUserQuestion` before execution | Needs input (orange) |
| `Stop` | Response finished (green, then the existing viewed state) |
| `StopFailure` | Response failure (red/error) |
| `SessionEnd` | Remove the session and release its slot |

A recoverable tool failure is not a failed session. `Stop` means that Claude
stopped responding, **not** that the requested work was successfully completed.
Another hook can cause a stopped turn to continue; subsequent observed tool or
prompt activity returns the slot to Running. Subagent events carrying
`agent_id`, idle notifications, and unknown events are ignored.

Slots are stable while a session is tracked. The first six sessions get slots;
additional sessions retain their latest state in a queue and are promoted when
a slot becomes free. A newly opened, untouched session is not allocated: the
native lamp protocol cannot represent a truthful independent Idle transition.
The monitor publishes only changed status frames and uses `device.status` for
its heartbeat, rather than repeatedly waking the display with status traffic.
With no tracked sessions, it releases the HID handle and lets the firmware's
native session timeout return to the offline screen.

Keys `1` through `6` select locally on the device. They **do not focus a Claude
terminal**. Approve/reject, voice, sending messages, model/effort selection, and
interrupt are not implemented. Unsupported dial/action controls are dismissed
and the status deck is restored after the firmware's picker guard expires;
no terminal keystrokes or permission decisions are generated.

## State, privacy, and failure behavior

Hooks only perform a short SQLite update; HID access stays in the separately
started monitor. Each hook has a one-second timeout, a bounded input size, and
returns no stdout/decision. Handled input, filesystem, or database errors exit
zero with a diagnostic on stderr so this observer does not block agent work.
Events missed through hook timeout, policy, or failure cannot be reconstructed.

The private state directory defaults to:

```text
~/.local/state/codex-microputer/claude-code/
```

It stores only session ID, slot, lifecycle state, and receipt timestamp, plus
SQLite/monitor-lock bookkeeping. Prompt text, tool arguments, working-directory
paths, transcripts, API keys, and permission requests are not persisted. The
state directory must have mode `700`; symlinked state directories/databases are
rejected. A file lock prevents two monitors using the same state directory.
Use the same global `--state-dir PATH` **before** `settings`, `monitor`, `status`,
or `forget` to override it.

These are **last-reported event states, not process-liveness guarantees**.
Ctrl+C does not necessarily produce a `Stop` event, and a killed CLI process
can miss `SessionEnd`. The monitor deliberately does not guess that a long task
has died from a time-to-live. Inspect and explicitly remove stale entries:

```bash
"$PY" tools/claude_code.py status
"$PY" tools/claude_code.py forget SESSION_ID
```

`status` prints zero-based slots (`0` maps to device key `1`); `null` means
waiting for a free slot. `forget` only removes monitor state and never stops a
Claude session. USB disconnection or a missing acknowledgement closes the
handle and retries; it does not change Claude execution. Host lighting is held
at 70% while the monitor is connected; Codex's own auto-dim policy is not cloned.

## Verification

The host tests require neither hidapi nor a Claude login:

```bash
python3 tests/test_claude_code.py
./tools/test.sh
python3 tools/audit_public_tree.py
./tools/build.sh
```

The new tests cover lifecycle mapping, stable slots and overflow promotion,
parallel writers, minimal/private persistence, fail-open hooks, configuration
quoting, HID framing and acknowledgements, malformed packets, timeouts, device
selection, duplicate-monitor locking, and changed-frame-only publication.
They are automatically included by `tools/test.sh`. They do not replace a real
Claude CLI / macOS HID / Cardputer acceptance run.

Before treating this path as release-qualified, exercise two real sessions:
submit prompts, wait for permission, finish a response, and close a session;
check slot identity and state transitions. Disconnect/reconnect USB, verify
local selection and unsupported-control recovery, verify the final session
ending returns to offline, then stop the monitor and verify native Codex still
connects. A seven-session run should promote the queued session without moving
the other five occupied slots.

## Sources

Reviewed against the official interfaces on 2026-09-07:

- [Claude Code hooks](https://code.claude.com/docs/en/hooks)
- [Claude Code status line](https://code.claude.com/docs/en/statusline)
- [Claude Code CLI reference](https://code.claude.com/docs/en/cli-reference)
- [Claude Code monitoring](https://code.claude.com/docs/en/monitoring-usage)
- [Claude Code Remote Control](https://code.claude.com/docs/en/remote-control)
- [Python hidapi implementation](https://github.com/trezor/cython-hidapi)

The wire constants and host behavior come from this repository's
`main/usb_transport.cpp`, `main/rpc_framer.h`, `main/codex_micro_protocol.cpp`,
`main/status_reducer.h`, and `main/lamp.h`. The diagnostic serial `TASK` protocol
is intentionally not used: it does not establish a native HID session and can
be ignored while one is active.
