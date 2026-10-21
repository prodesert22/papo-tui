"""Queued HTTP mutations and WS membership changes, using the actual C owners."""

from test_context import main as test_main

from test_context import argument

import copy
import json
import os
import subprocess
import threading
import time
import tempfile
import unittest

from history_mock import CHANNEL, uuid
from interaction_mock import InteractionMock

BINARY = argument(0)
ROLE = uuid(400)
ALLOWED, DENIED, UNKNOWN = 2, 1, 0


class PermissionServer(InteractionMock):
    def __init__(self):
        super().__init__(count=1)
        self.owner_id = uuid(99)
        self.roles = [{"id": ROLE, "name": "Moderator", "permissions": {"manage_roles": True}}]
        self.user_roles = [{"id": ROLE}]
        self.source_status = None
        self.channel_status = None
        self.hold_identity = False
        self.identity_started = threading.Event()
        self.identity_release = threading.Event()
        for channel in self.channels:
            channel["permissions"] = []
            if channel["id"] == CHANNEL:
                channel["permissions"] = [
                    {
                        "role_id": ROLE,
                        "permissions": {
                            "read_channel": True,
                            "send_messages": True,
                            "delete_messages": True,
                        },
                    }
                ]
        self.original_route = self.api_handler
        self.api_handler = self.permission_route

    def permission_route(self, method, path, body):
        if method == "GET" and path.endswith("/pinned"):
            return 200, {"channel_id": CHANNEL, "pinned": []}
        if method == "GET" and path.endswith("/channels") and self.channel_status:
            return self.channel_status, None
        if method == "GET" and path.endswith("/auth/whoami") and self.hold_identity:
            self.hold_identity = False
            snapshot = {
                "id": uuid(1),
                "username": self.username,
                "roles": copy.deepcopy(self.user_roles),
            }
            self.identity_started.set()
            self.identity_release.wait(5)
            return 200, snapshot
        if method == "GET" and path.endswith("/roles") and self.source_status:
            if isinstance(self.source_status, list):
                return self.source_status.pop(0), None
            return self.source_status, None
        return self.original_route(method, path, body)

    def revoke(self):
        self.user_roles = []
        self.emit({"type": "role_remove", "user_id": uuid(1), "role_id": ROLE})


class PermissionsIntegration(unittest.TestCase):
    def start_probe(self, server):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        process = subprocess.Popen(
            [BINARY, server.url, CHANNEL],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
            env=dict(os.environ, XDG_STATE_HOME=directory.name, XDG_CONFIG_HOME=directory.name),
        )
        server.client_cleanup.callback(self.stop_probe, process)
        self.assertEqual(json.loads(process.stdout.readline()), {"ready": True})
        return process

    def stop_probe(self, process):
        if process.poll() is None:
            process.stdin.write("quit\n")
            process.stdin.flush()
        stdout, stderr = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, stderr)
        self.assertNotIn("Synthetic!123", stdout + stderr)
        self.assertNotIn("user-fixture", stdout + stderr)

    def command(self, process, command):
        process.stdin.write(command + "\n")
        process.stdin.flush()
        line = process.stdout.readline()
        self.assertTrue(
            line, process.stderr.read() if process.poll() is not None else "No probe response"
        )
        return json.loads(line)

    def test_revocation_cancels_rate_queued_send_without_http_or_draft_loss(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            self.assertTrue(self.command(process, "queue-send")["accepted"])
            server.revoke()
            result = self.command(process, "poll-long")
            self.assertEqual(result["send"], DENIED)
            self.assertEqual(result["manage_roles"], DENIED)
            self.assertEqual(result["state"], 3)
            self.assertEqual(result["draft"], "Preserved queued draft")
            self.assertEqual(server.message_count, 0)
            self.assertTrue(result["session"])

    def test_attachment_role_revocation_blocks_queued_upload_and_preserves_files(self):
        with PermissionServer() as server:
            server.roles[0]["permissions"]["send_attachment"] = True
            for channel in server.channels:
                if channel["id"] == CHANNEL:
                    channel["permissions"] = []
            process = self.start_probe(server)
            self.assertTrue(self.command(process, "queue-attachment")["accepted"])
            server.revoke()
            result = self.command(process, "poll-long")
            self.assertEqual(result["send"], ALLOWED)
            self.assertEqual(result["state"], 3)
            self.assertEqual(result["draft_files"], 1)
            self.assertEqual(result["draft"], "Preserved attachment draft")
            self.assertEqual(server.message_count, 0)

    def test_pin_role_revocation_blocks_queued_pin_and_unpin(self):
        for command in ("queue-pin", "queue-unpin"):
            with self.subTest(command=command), PermissionServer() as server:
                server.roles[0]["permissions"]["pin_message"] = True
                for channel in server.channels:
                    if channel["id"] == CHANNEL:
                        channel["permissions"] = []
                process = self.start_probe(server)
                loaded = self.command(process, "load-pins")
                self.assertEqual(loaded["pin_state"], 0)
                self.assertEqual(loaded["delete"], DENIED)
                self.assertEqual(loaded["pin"], ALLOWED)
                self.assertTrue(self.command(process, command)["accepted"])
                server.revoke()
                result = self.command(process, "poll-long")
                self.assertEqual(result["read"], ALLOWED)
                self.assertEqual(result["pin"], DENIED)
                self.assertFalse(any(path.endswith("/pin") for _, path, *_ in server.requests))

    def test_pinned_reads_require_read_access_but_not_pin_permission(self):
        for revoke in (False, True):
            with self.subTest(revoke=revoke), PermissionServer() as server:
                process = self.start_probe(server)
                self.assertTrue(self.command(process, "queue-pins-read")["accepted"])
                if revoke:
                    server.revoke()
                result = self.command(process, "poll-long")
                reads = [path for _, path, *_ in server.requests if path.endswith("/pinned")]
                self.assertEqual(result["direct_status"], 403 if revoke else 200)
                self.assertEqual(len(reads), 0 if revoke else 1)

    def test_attachment_and_pin_roles_must_be_confirmed_before_queueing(self):
        for unknown in (False, True):
            with self.subTest(unknown=unknown), PermissionServer() as server:
                process = self.start_probe(server)
                self.assertEqual(self.command(process, "load-pins")["pin_state"], 0)
                if unknown:
                    server.source_status = 503
                    self.command(process, "refresh")
                    self.command(process, "poll-long")
                for command in ("queue-attachment", "queue-pin", "queue-unpin"):
                    result = self.command(process, command)
                    self.assertFalse(result["accepted"])
                    self.assertEqual(result["pin"], UNKNOWN if unknown else DENIED)
                    self.assertEqual(result["attach"], UNKNOWN if unknown else DENIED)
                self.command(process, "poll-long")
                self.assertEqual(server.message_count, 0)
                self.assertFalse(any(path.endswith("/pin") for _, path, *_ in server.requests))

    def test_revocation_cancels_rate_queued_deletion_without_http(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            self.assertTrue(self.command(process, "queue-delete")["accepted"])
            server.revoke()
            result = self.command(process, "poll-long")
            self.assertEqual(result["delete"], DENIED)
            self.assertEqual(server.mutation_calls, [])
            self.assertEqual(len(server.messages), 1)

    def test_metadata_failures_do_not_logout_or_grant_administrative_access(self):
        for code in (403, 429, 503):
            with self.subTest(code=code), PermissionServer() as server:
                process = self.start_probe(server)
                server.source_status = code
                self.command(process, "refresh")
                result = self.command(process, "poll-long")
                self.assertEqual(result["manage_roles"], UNKNOWN)
                self.assertTrue(result["session"])
                self.assertIn("indisponíveis", result["notice"])
                server.source_status = None
                self.command(process, "refresh")
                result = self.command(process, "poll-long")
                self.assertEqual(result["manage_roles"], ALLOWED)
                self.stop_probe(process)

    def test_metadata_401_recovers_once_without_replaying_mutations(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            server.source_status = [401]
            self.command(process, "refresh")
            result = self.command(process, "poll-long")
            self.assertTrue(result["session"])
            self.assertEqual(result["manage_roles"], ALLOWED)
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(server.mutation_calls, [])

    def test_repeated_metadata_401_stops_recovery_and_clears_permissions(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            server.source_status = 401
            self.command(process, "refresh")
            result = self.command(process, "poll-long")
            self.assertFalse(result["session"])
            self.assertEqual(result["manage_roles"], UNKNOWN)
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(server.mutation_calls, [])

    def test_old_identity_response_cannot_restore_revoked_role(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            server.hold_identity = True
            self.command(process, "refresh")
            process.stdin.write("poll-long\n")
            process.stdin.flush()
            self.assertTrue(server.identity_started.wait(4))
            server.revoke()
            time.sleep(0.1)
            server.identity_release.set()
            result = json.loads(process.stdout.readline())
            self.assertEqual(result["send"], DENIED)
            self.assertEqual(result["delete"], DENIED)
            self.assertEqual(result["manage_roles"], DENIED)
            self.assertTrue(result["session"])

    def test_role_permission_edit_without_ws_event_is_reconciled_periodically(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            server.roles[0]["permissions"]["manage_roles"] = False
            deadline = time.monotonic() + 36
            result = self.command(process, "status")
            while result["manage_roles"] == ALLOWED and time.monotonic() < deadline:
                result = self.command(process, "poll-long")
            self.assertEqual(result["manage_roles"], DENIED)
            self.assertTrue(result["session"])
            self.assertEqual(server.mutation_calls, [])

    def test_unknown_access_keeps_loaded_history_but_blocks_new_reads_and_mutations(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            self.assertGreater(self.command(process, "status")["messages"], 0)
            reads = sum(r[0] == "GET" and "/messages" in r[1] for r in server.requests)
            server.channel_status = 503
            self.command(process, "refresh")
            result = self.command(process, "poll-long")
            self.assertEqual(result["read"], UNKNOWN)
            self.assertEqual(result["send"], UNKNOWN)
            self.assertGreater(result["messages"], 0)
            self.assertTrue(result["session"])
            self.assertFalse(self.command(process, "queue-send")["accepted"])
            self.assertEqual(
                reads, sum(r[0] == "GET" and "/messages" in r[1] for r in server.requests)
            )
            self.assertEqual(server.message_count, 0)

    def test_metadata_refresh_does_not_read_inactive_histories(self):
        with PermissionServer() as server:
            process = self.start_probe(server)
            self.command(process, "refresh")
            self.command(process, "poll-long")
            paths = [r[1] for r in server.requests if r[0] == "GET" and "/messages" in r[1]]
            self.assertTrue(paths)
            self.assertTrue(all(CHANNEL in path for path in paths))


if __name__ == "__main__":
    test_main(
        1,
    )
