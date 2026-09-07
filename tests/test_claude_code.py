#!/usr/bin/env python3
"""Host-only regression tests; no Claude login, HID library, or board needed."""
from concurrent.futures import ThreadPoolExecutor
import io
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import claude_code as cc


def event(name, sid="session-1", **fields):
    return {"hook_event_name": name, "session_id": sid, **fields}


class FakeDevice:
    def __init__(self):
        self.decoder = cc.Decoder()
        self.incoming = []
        self.requests = []
        self.closed = False

    def open_path(self, path):
        self.path = path

    def write(self, report):
        for request in self.decoder.feed(report):
            self.requests.append(request)
            self.incoming.extend(cc.reports({"id": request["id"], "result": {"ok": True}}))
        return len(report)

    def read(self, length, timeout):
        return list(self.incoming.pop(0)) if self.incoming else []

    def close(self):
        self.closed = True


def device_info(**kwargs):
    return {"path": b"usb-1", "usage_page": 0xFF00, "usage": 1,
            "bus_type": 1, "product_string": "Codex Micro ADV", **kwargs}


def fake_hid(device, entries=None):
    return mock.Mock(HID_API_BUS_USB=1, enumerate=mock.Mock(return_value=(
        [device_info()] if entries is None else entries)), device=mock.Mock(return_value=device))


class HookTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.state = Path(self.temp.name) / "private"

    def test_lifecycle_and_stable_slot(self):
        for name, expected in (("UserPromptSubmit", "RUNNING"), ("PermissionRequest", "INPUT"),
                               ("PostToolUse", "RUNNING"), ("Stop", "DONE"),
                               ("UserPromptSubmit", "RUNNING"), ("StopFailure", "ERROR")):
            cc.record(self.state, event(name))
            row = cc.snapshot(self.state)[0]
            self.assertEqual((row["slot"], row["state"]), (0, expected))
        cc.record(self.state, event("SessionEnd"))
        self.assertEqual(cc.snapshot(self.state), [])

    def test_only_meaningful_events_allocate_slots(self):
        for name in ("SessionStart", "SubagentStop", "Unknown"):
            cc.record(self.state, event(name))
        self.assertEqual(cc.snapshot(self.state), [])

    def test_ignores_subagent_activity(self):
        cc.record(self.state, event("Stop"))
        cc.record(self.state, event("PreToolUse", agent_id="agent-1"))
        self.assertEqual(cc.snapshot(self.state)[0]["state"], "DONE")

    def test_notifications_are_filtered(self):
        for kind in ("idle_prompt", "auth_success", "other"):
            self.assertIsNone(cc.event_state(event("Notification", notification_type=kind)))
        for kind in ("permission_prompt", "elicitation_dialog"):
            self.assertEqual(cc.event_state(event("Notification", notification_type=kind)), "INPUT")

    def test_question_waits_and_tool_failure_is_not_session_failure(self):
        self.assertEqual(cc.event_state(event("PreToolUse", tool_name="AskUserQuestion")), "INPUT")
        self.assertEqual(cc.event_state(event("PostToolUseFailure")), "RUNNING")

    def test_overflow_is_retained_and_promoted_without_shuffling(self):
        for i in range(8):
            cc.record(self.state, event("UserPromptSubmit", f"s-{i}"))
        rows = cc.snapshot(self.state)
        self.assertEqual([row["slot"] for row in rows], [0, 1, 2, 3, 4, 5, None, None])
        cc.record(self.state, event("PermissionRequest", "s-7"))
        cc.forget(self.state, "s-2")
        rows = {row["session_id"]: row for row in cc.snapshot(self.state)}
        self.assertEqual(rows["s-6"]["slot"], 2)
        self.assertEqual(rows["s-3"]["slot"], 3)
        self.assertIsNone(rows["s-7"]["slot"])
        self.assertEqual(rows["s-7"]["state"], "INPUT")

    def test_concurrent_sessions_do_not_collide(self):
        # Create schema before exercising independent connections in parallel.
        cc.snapshot(self.state)
        with ThreadPoolExecutor(max_workers=6) as pool:
            list(pool.map(lambda i: cc.record(self.state, event("UserPromptSubmit", f"s-{i}")), range(12)))
        rows = cc.snapshot(self.state)
        self.assertEqual(len(rows), 12)
        self.assertEqual(sorted(row["slot"] for row in rows if row["slot"] is not None), list(range(6)))

    def test_no_prompts_paths_or_credentials_are_stored(self):
        secret = "DO-NOT-PERSIST-THIS-UNIQUE-MARKER"
        cc.record(self.state, event("UserPromptSubmit", prompt=secret, transcript_path=secret,
                                   cwd=secret, tool_input={"command": secret}))
        for path in self.state.iterdir():
            if path.is_file():
                self.assertNotIn(secret.encode(), path.read_bytes())
        self.assertEqual(self.state.stat().st_mode & 0o777, 0o700)

    def test_invalid_session_ids_do_not_touch_database(self):
        for sid in (None, "", "x" * 129, "bad\nid", 123):
            with self.assertRaises(ValueError):
                cc.record(self.state, event("Stop", sid))
        self.assertFalse(self.state.exists())

    def test_settings_are_additive_and_shell_quoted(self):
        directory = self.state / "spaces and 'quotes'"
        config = cc.settings(directory)
        self.assertEqual(set(config), {"hooks"})
        self.assertEqual(set(config["hooks"]), set(cc.EVENTS))
        for group in config["hooks"].values():
            hook = group[0]["hooks"][0]
            argv = shlex.split(hook["command"])
            self.assertEqual(argv[-3:], ["--state-dir", str(directory.resolve()), "hook"])
            self.assertEqual(hook["timeout"], 1)
            self.assertNotIn("async", hook)
        self.assertFalse(self.state.exists())

    def test_hook_failures_are_zero_exit_and_silent_stdout(self):
        script = str(ROOT / "tools/claude_code.py")
        for data in (b"not json", b"[]", b'{"hook_event_name":"Stop"}', b"x" * (cc.MAX_INPUT + 1)):
            result = subprocess.run([sys.executable, script, "--state-dir", str(self.state), "hook"],
                                    input=data, capture_output=True, check=False)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b"")
            self.assertTrue(result.stderr)

    def test_hook_cli_needs_no_hid_and_returns_no_decision(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/claude_code.py"),
                                 "--state-dir", str(self.state), "hook"],
                                input=json.dumps(event("PermissionRequest")).encode(),
                                capture_output=True, check=True)
        self.assertEqual((result.stdout, result.stderr), (b"", b""))
        self.assertEqual(cc.snapshot(self.state)[0]["state"], "INPUT")

    def test_insecure_directory_is_rejected(self):
        self.state.mkdir(mode=0o755)
        with self.assertRaises(ValueError):
            cc.record(self.state, event("Stop"))

    def test_state_directory_symlink_is_rejected_by_cli(self):
        target = Path(self.temp.name) / "target"
        target.mkdir(mode=0o700)
        self.state.symlink_to(target, target_is_directory=True)
        result = subprocess.run([sys.executable, str(ROOT / "tools/claude_code.py"),
                                 "--state-dir", str(self.state), "hook"],
                                input=json.dumps(event("Stop")).encode(),
                                capture_output=True, check=True)
        self.assertEqual(result.stdout, b"")
        self.assertTrue(result.stderr)
        self.assertFalse((target / "sessions.sqlite3").exists())

    def test_database_symlink_is_rejected(self):
        self.state.mkdir(mode=0o700)
        (self.state / "sessions.sqlite3").symlink_to(self.state / "target")
        with self.assertRaises(ValueError):
            cc.snapshot(self.state)

    @unittest.skipIf(sys.platform == "win32", "POSIX lock")
    def test_duplicate_monitor_lock_is_rejected(self):
        with cc.monitor_lock(self.state):
            with self.assertRaises(ValueError):
                with cc.monitor_lock(self.state):
                    self.fail("second monitor acquired lock")
        with cc.monitor_lock(self.state):
            pass

    def test_missing_session_end_requires_explicit_forget(self):
        with mock.patch.object(cc.time, "time", return_value=1):
            cc.record(self.state, event("UserPromptSubmit"))
        self.assertEqual(cc.snapshot(self.state)[0]["state"], "RUNNING")
        cc.forget(self.state, "session-1")
        self.assertEqual(cc.snapshot(self.state), [])


class WireTests(unittest.TestCase):
    def test_native_report_layout_and_fragmentation(self):
        message = {"id": 5, "method": "v.oai.thstatus", "params": cc.lights([
            {"slot": 0, "state": "RUNNING"}, {"slot": 5, "state": "INPUT"}])}
        packets = cc.reports(message)
        self.assertGreater(len(packets), 1)
        for packet in packets:
            self.assertEqual(len(packet), 64)
            self.assertEqual(packet[:2], bytes([6, 2]))
            self.assertLessEqual(packet[2], 61)
            self.assertEqual(packet[3 + packet[2]:], b"\0" * (61 - packet[2]))
        decoder = cc.Decoder()
        self.assertEqual([msg for p in packets for msg in decoder.feed(p)], [message])

    def test_byte_boundaries_and_unicode(self):
        for count in (0, 40, 60, 61, 62, 120, 300):
            message = {"value": "я" * count}
            decoder = cc.Decoder()
            self.assertEqual([m for p in cc.reports(message) for m in decoder.feed(p)], [message])

    def test_decoder_recovers_after_malformed_and_oversize_input(self):
        decoder = cc.Decoder()
        decoder.feed(bytes([6, 2, 62]) + b"x" * 62)
        for _ in range(70):
            decoder.feed(bytes([6, 2, 61]) + b"x" * 61)
        decoder.feed(bytes([6, 2, 62]))  # Explicitly reset the remaining partial line.
        self.assertEqual(decoder.feed(cc.reports({"id": 1})[0]), [{"id": 1}])
        self.assertEqual(decoder.feed(bytes([1, 2, 3])), [])

    def test_oversized_rpc_is_rejected(self):
        with self.assertRaises(ValueError):
            cc.reports({"text": "x" * 4096})

    def test_colors_slots_and_no_fake_idle_state(self):
        self.assertEqual(cc.COLORS, {"RUNNING": 0x304FFE, "INPUT": 0xFF6D00,
                                    "DONE": 0x00FF4C, "ERROR": 0xFF0033})
        frame = cc.lights([{"slot": 4, "state": "DONE"}, {"slot": None, "state": "ERROR"}])
        self.assertEqual([lamp["id"] for lamp in frame], list(range(6)))
        self.assertEqual([lamp["e"] for lamp in frame], [0, 0, 0, 0, 1, 0])
        self.assertTrue(all(lamp["b"] == 0 for lamp in frame if lamp["id"] != 4))

    def test_unsupported_controls_are_not_permission_decisions(self):
        for key in ("ACT07", "ACT08", "ENC", "ENC_CW"):
            self.assertTrue(cc.unsupported_control({"method": "v.oai.hid", "params": {"k": key, "act": 1}}))
        for key in ("AG00", "AG05", "WAKE"):
            self.assertFalse(cc.unsupported_control({"method": "v.oai.hid", "params": {"k": key, "act": 1}}))
        self.assertFalse(cc.unsupported_control({"method": "v.oai.hid", "params": None}))

    @mock.patch.object(cc.time, "sleep", return_value=None)
    def test_rpc_acknowledgements(self, _):
        device = FakeDevice()
        connection = cc.Connection(device)
        self.assertEqual(connection.rpc("device.status"), {"ok": True})
        connection.rpc("v.oai.thstatus", cc.lights([{ "slot": 0, "state": "RUNNING"}]))
        self.assertEqual([request["id"] for request in device.requests], [1, 2])

    def test_short_write_fails(self):
        with self.assertRaisesRegex(OSError, "short HID write"):
            cc.Connection(mock.Mock(write=mock.Mock(return_value=1))).rpc("device.status")

    @mock.patch.object(cc.time, "sleep", return_value=None)
    def test_rpc_timeout_is_bounded(self, _):
        device = mock.Mock(write=lambda packet: len(packet))
        with mock.patch.object(cc.time, "monotonic", side_effect=[0, 3]):
            with self.assertRaisesRegex(OSError, "did not acknowledge"):
                cc.Connection(device).rpc("device.status")

    def test_device_selection_does_not_open_keyboard_or_bluetooth(self):
        device = FakeDevice()
        hid = fake_hid(device, [device_info(usage_page=1), device_info(path=b"bt", bus_type=2),
                               device_info(), device_info()])
        self.assertIs(cc.open_device(hid), device)
        self.assertEqual(device.path, b"usb-1")

    def test_ambiguous_or_wrong_devices_are_rejected(self):
        for entries in ([], [device_info(product_string="Other")],
                        [device_info(), device_info(path=b"usb-2")]):
            with self.assertRaises(OSError):
                cc.open_device(fake_hid(FakeDevice(), entries))

    @mock.patch.object(cc.time, "sleep", return_value=None)
    def test_monitor_publishes_only_changed_frames_and_closes_when_empty(self, _):
        device = FakeDevice()
        running = [{"slot": 0, "state": "RUNNING"}]
        done = [{"slot": 0, "state": "DONE"}]
        with mock.patch.object(cc, "snapshot", side_effect=[running, running, done, [], KeyboardInterrupt]), \
                mock.patch("sys.stderr", new=io.StringIO()):
            with self.assertRaises(KeyboardInterrupt):
                cc.monitor(Path("unused"), fake_hid(device))
        states = [r["params"][0]["c"] for r in device.requests if r["method"] == "v.oai.thstatus"]
        self.assertEqual(states, [cc.COLORS["RUNNING"], cc.COLORS["DONE"]])
        self.assertTrue(device.closed)
        self.assertTrue(all(r["method"] in ("device.status", "v.oai.rgbcfg", "v.oai.thstatus")
                            for r in device.requests))

    def test_monitor_cli_requires_explicit_host_handoff(self):
        with mock.patch.object(cc.sys, "platform", "darwin"), mock.patch("sys.stderr", new=io.StringIO()):
            self.assertEqual(cc.main(["monitor"]), 1)


if __name__ == "__main__":
    unittest.main()
