#!/usr/bin/env python3
"""Opt-in Claude Code hook monitor. See docs/CLAUDE_CODE.md.

Hooks use only the standard library; only `monitor` imports hidapi. No prompts,
transcripts, credentials, permission decisions, or terminal keystrokes are used.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shlex
import sqlite3
import sys
import time

VID, PID, REPORT, CHANNEL, PAYLOAD = 0x303A, 0x8360, 6, 2, 61
COLORS = {"RUNNING": 0x304FFE, "INPUT": 0xFF6D00,
          "DONE": 0x00FF4C, "ERROR": 0xFF0033}
EVENTS = ("UserPromptSubmit", "PreToolUse", "PostToolUse", "PostToolUseFailure",
          "PermissionRequest", "Notification", "Stop", "StopFailure", "SessionEnd")
MAX_INPUT = 1024 * 1024
DEFAULT_STATE = Path.home() / ".local/state/codex-microputer/claude-code"


def event_state(event: dict) -> str | None:
    """Observe the main conversation, never make a hook permission decision."""
    if event.get("agent_id"):
        return None  # A subagent finishing/working must not overwrite its parent.
    name = event.get("hook_event_name")
    if name == "SessionEnd":
        return "REMOVE"
    if name == "Stop":
        return "DONE"  # Response stopped, NOT a claim that the task succeeded.
    if name == "StopFailure":
        return "ERROR"
    if name == "PermissionRequest":
        return "INPUT"
    if name == "Notification":
        return "INPUT" if event.get("notification_type") in (
            "permission_prompt", "elicitation_dialog") else None
    if name == "PreToolUse" and event.get("tool_name") == "AskUserQuestion":
        return "INPUT"
    if name in ("UserPromptSubmit", "PreToolUse", "PostToolUse", "PostToolUseFailure"):
        # An individual tool failure can be recovered from; it is not StopFailure.
        return "RUNNING"
    return None


@contextmanager
def database(directory: Path):
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    if directory.is_symlink() or directory.stat().st_mode & 0o077:
        raise ValueError("state directory must be private (mode 700) and not a symlink")
    path = directory / "sessions.sqlite3"
    if path.is_symlink():
        raise ValueError("state database must not be a symlink")
    db = sqlite3.connect(path, timeout=0.25)
    db.row_factory = sqlite3.Row
    try:
        db.execute("""CREATE TABLE IF NOT EXISTS sessions (
            session_id TEXT PRIMARY KEY, slot INTEGER UNIQUE,
            state TEXT NOT NULL, updated REAL NOT NULL,
            CHECK(slot IS NULL OR slot BETWEEN 0 AND 5),
            CHECK(state IN ('RUNNING','INPUT','DONE','ERROR')))""")
        with db:
            yield db
    finally:
        db.close()


def assign_slots(db):
    used = {row[0] for row in db.execute("SELECT slot FROM sessions WHERE slot IS NOT NULL")}
    for slot in range(6):
        if slot not in used:
            db.execute("""UPDATE sessions SET slot=? WHERE rowid=(
                SELECT rowid FROM sessions WHERE slot IS NULL ORDER BY rowid LIMIT 1)""", (slot,))


def record(directory: Path, event: dict):
    state = event_state(event)
    if state is None:
        return
    sid = event.get("session_id")
    if not isinstance(sid, str) or not sid or len(sid) > 128 or any(ord(c) < 32 for c in sid):
        raise ValueError("missing or invalid session_id")
    with database(directory) as db:
        db.execute("BEGIN IMMEDIATE")
        if state == "REMOVE":
            db.execute("DELETE FROM sessions WHERE session_id=?", (sid,))
        else:
            db.execute("""INSERT INTO sessions(session_id,state,updated) VALUES(?,?,?)
                ON CONFLICT(session_id) DO UPDATE SET state=excluded.state, updated=excluded.updated""",
                       (sid, state, time.time()))
        assign_slots(db)


def snapshot(directory: Path) -> list[dict]:
    with database(directory) as db:
        return [dict(row) for row in db.execute(
            "SELECT * FROM sessions ORDER BY slot IS NULL,slot,rowid")]


def forget(directory: Path, sid: str):
    with database(directory) as db:
        db.execute("BEGIN IMMEDIATE")
        db.execute("DELETE FROM sessions WHERE session_id=?", (sid,))
        assign_slots(db)


def settings(directory: Path) -> dict:
    command = shlex.join([sys.executable, str(Path(__file__).resolve()),
                          "--state-dir", str(directory.absolute()), "hook"])
    hooks = {}
    for event in EVENTS:
        group = {"hooks": [{"type": "command", "command": command, "timeout": 1}]}
        if event == "Notification":
            group["matcher"] = "permission_prompt|elicitation_dialog"
        # Short synchronous DB updates preserve event order; no HID I/O in hooks.
        hooks[event] = [group]
    return {"hooks": hooks}


def reports(message: dict) -> list[bytes]:
    wire = (json.dumps(message, separators=(",", ":"), ensure_ascii=True) + "\n").encode()
    if len(wire) > 4096:
        raise ValueError("RPC message exceeds firmware limit")
    return [bytes((REPORT, CHANNEL, len(wire[i:i + PAYLOAD]))) +
            wire[i:i + PAYLOAD].ljust(PAYLOAD, b"\0") for i in range(0, len(wire), PAYLOAD)]


class Decoder:
    def __init__(self):
        self.buffer = b""

    def feed(self, report) -> list[dict]:
        report = bytes(report)
        if not report or report[0] != REPORT:
            return []  # Ignore the separate keyboard report.
        if len(report) < 3 or report[1] != CHANNEL or report[2] > PAYLOAD or len(report) < report[2] + 3:
            self.buffer = b""
            return []
        self.buffer += report[3:3 + report[2]]
        if len(self.buffer) > 4096:
            self.buffer = b""
            return []
        messages = []
        while b"\n" in self.buffer:
            line, self.buffer = self.buffer.split(b"\n", 1)
            try:
                value = json.loads(line)
                if isinstance(value, dict):
                    messages.append(value)
            except (ValueError, UnicodeError):
                pass
        return messages


def lights(rows: list[dict]) -> list[dict]:
    assigned = {row["slot"]: row["state"] for row in rows if row["slot"] is not None}
    return [{"id": slot, "c": COLORS[assigned[slot]] if slot in assigned else 0,
             "b": 0.7 if slot in assigned else 0, "e": 1 if slot in assigned else 0, "s": 0}
            for slot in range(6)]


def unsupported_control(message: dict) -> bool:
    if message.get("method") == "v.oai.rad":
        return True
    params = message.get("params")
    if message.get("method") != "v.oai.hid" or not isinstance(params, dict):
        return False
    key = params.get("k")
    return (isinstance(key, str) and key.startswith(("ENC", "ACT"))
            and params.get("act") in (1, 2))


class Connection:
    def __init__(self, device):
        self.device = device
        self.decoder = Decoder()
        self.next_id = 0
        self.close_picker = False

    def receive(self, timeout=50):
        messages = self.decoder.feed(self.device.read(64, timeout))
        self.close_picker |= any(unsupported_control(message) for message in messages)
        return messages

    def rpc(self, method: str, params=None):
        self.next_id += 1
        request = {"id": self.next_id, "method": method}
        if params is not None:
            request["params"] = params
        for report in reports(request):
            if self.device.write(report) != len(report):
                raise OSError("short HID write")
            time.sleep(0.005)  # Respect the firmware's bounded RX queue.
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            for message in self.receive():
                if message.get("id") == request["id"]:
                    if "error" in message or "result" not in message:
                        raise OSError("device rejected " + method)
                    return message["result"]
        raise OSError("device did not acknowledge " + method)


def open_device(hid):
    matches = {item["path"] for item in hid.enumerate(VID, PID)
               if item.get("usage_page") == 0xFF00 and item.get("usage") == 1
               and item.get("bus_type") == hid.HID_API_BUS_USB
               and item.get("product_string") == "Codex Micro ADV"}
    if len(matches) != 1:
        raise OSError(f"expected exactly one USB Codex Micro ADV RPC interface; found {len(matches)}")
    device = hid.device()
    try:
        device.open_path(matches.pop())
    except BaseException:
        device.close()
        raise
    return device


@contextmanager
def monitor_lock(directory: Path):
    import fcntl  # The initial HID adapter targets macOS, not Windows.
    with database(directory):
        pass
    flags = os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW
    fd = os.open(directory / "monitor.lock", flags, 0o600)
    try:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("another Claude Code monitor is already running") from None
        yield
    finally:
        os.close(fd)


def monitor(directory: Path, hid):
    device = None
    previous = None
    retry_at = heartbeat = restore_at = 0.0
    last_error = None
    last_overflow = 0
    try:
        while True:
            rows = snapshot(directory)
            overflow = sum(row["slot"] is None for row in rows)
            if overflow != last_overflow:
                print(f"Claude Code: {overflow} sessions waiting for a free slot", file=sys.stderr)
                last_overflow = overflow
            if not rows:
                if device is not None:
                    device.close()
                    device = None
                # All-off thstatus means power-only, not an empty deck. Let the
                # existing native-session timeout return the firmware to Boot.
                time.sleep(0.2)
                continue
            now = time.monotonic()
            try:
                if device is None:
                    if now < retry_at:
                        time.sleep(0.2)
                        continue
                    device = open_device(hid)
                    connection = Connection(device)
                    connection.rpc("device.status")
                    connection.rpc("v.oai.rgbcfg", {
                        "ambient": {"b": 0.7, "e": 1}, "keys": {"b": 0.7, "e": 1}})
                    previous = None
                    heartbeat = restore_at = 0
                    last_error = None
                    print("Claude Code monitor connected (USB; observation only)", file=sys.stderr)
                connection.receive()
                if connection.close_picker:
                    connection.close_picker = False
                    # Unsupported native dial/actions must not leave the UI in
                    # a picker. All-off closes it; restore after its 2.5s guard.
                    connection.rpc("v.oai.thstatus", lights([]))
                    restore_at = time.monotonic() + 2.6
                    previous = None
                frame = lights(rows)
                if frame != previous and time.monotonic() >= restore_at:
                    connection.rpc("v.oai.thstatus", frame)
                    previous = frame
                if time.monotonic() >= heartbeat:
                    connection.rpc("device.status")
                    heartbeat = time.monotonic() + 2
            except OSError as error:
                if device is not None:
                    device.close()
                    device = None
                if str(error) != last_error:
                    print(f"Claude Code monitor: {error}; retrying", file=sys.stderr)
                    last_error = str(error)
                retry_at = time.monotonic() + 2
            time.sleep(0.05)
    finally:
        if device is not None:
            device.close()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--state-dir", type=Path, default=DEFAULT_STATE)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("settings", "hook", "status"):
        commands.add_parser(name)
    commands.add_parser("forget").add_argument("session_id")
    commands.add_parser("monitor").add_argument("--codex-closed", action="store_true",
        help="confirm Codex/ChatGPT desktop is quit and no other HID host owns the device")
    args = parser.parse_args(argv)
    directory = args.state_dir.expanduser().absolute()
    try:
        if args.command == "hook":
            raw = sys.stdin.buffer.read(MAX_INPUT + 1)
            if len(raw) > MAX_INPUT:
                raise ValueError("hook input exceeds 1 MiB")
            event = json.loads(raw)
            if not isinstance(event, dict):
                raise ValueError("hook input must be an object")
            record(directory, event)
        elif args.command == "settings":
            print(json.dumps(settings(directory), indent=2))
        elif args.command == "status":
            print(json.dumps(snapshot(directory), indent=2))
        elif args.command == "forget":
            forget(directory, args.session_id)
        else:
            if sys.platform != "darwin":
                raise ValueError("the experimental HID monitor currently targets macOS USB only")
            if not args.codex_closed:
                raise ValueError("quit Codex/ChatGPT desktop first, then pass --codex-closed; do not run both hosts")
            try:
                import hid
            except ImportError:
                raise ValueError("install tools/requirements-claude-code.txt in this Python environment") from None
            with monitor_lock(directory):
                monitor(directory, hid)
    except (OSError, ValueError, sqlite3.Error) as error:
        print(f"Claude Code monitor: {error}", file=sys.stderr)
        # Hook errors must neither block tools nor generate permission output.
        return 0 if args.command == "hook" else 1
    except KeyboardInterrupt:
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
