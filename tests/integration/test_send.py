"""R6 multipart and bounded outbox against controlled synthetic fixtures."""

import json
import os
import subprocess
import sys
import unittest
from mock_server import MockServer

BINARY = sys.argv.pop(1)
TEXT = "Olá, ação!\nSegunda linha."
CONFIRMED, FAILED, UNCERTAIN = 2, 3, 4


class Sending(unittest.TestCase):
    def probe(self, server, mode="send", **settings):
        result = subprocess.run(
            [BINARY, server.url, mode],
            capture_output=True,
            text=True,
            env=dict(os.environ, LC_ALL="C.UTF-8", **settings),
            timeout=25,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Synthetic!123", result.stdout + result.stderr)
        self.assertNotIn("user-fixture", result.stdout + result.stderr)
        return json.loads(result.stdout)

    def test_native_multipart_confirms_and_clears_draft(self):
        with MockServer() as server:
            result = self.probe(server)
            self.assertEqual(result["state"], CONFIRMED)
            self.assertEqual(result["draft"], "")
            self.assertEqual(result["confirmed_count"], 1)
            self.assertTrue(result["busy"])
            self.assertEqual(server.sent_messages[0]["content"], TEXT)
            self.assertTrue(server.multipart_types[0].startswith("multipart/form-data; boundary="))
            request = next(r for r in server.requests if r[1].endswith("/messages"))
            self.assertEqual(set(request[3]), {"channel_id", "content"})
            self.assertEqual(request[2], "Auth=user-fixture")

    def test_empty_text_rejected_without_request(self):
        with MockServer() as server:
            result = self.probe(server, "empty")
            self.assertEqual(result["submit"], 1)
            self.assertEqual(server.message_count, 0)
            self.assertEqual(result["draft"], " \n\t")

    def test_edit_during_send_preserves_new_text(self):
        for mode, expected in (
            ("append", " continuação"),
            ("replace", "Outro rascunho"),
            ("restore", TEXT + " continuação"),
            ("grapheme", "cafe\u0301"),
            ("emoji-append", " continuação"),
        ):
            with self.subTest(mode=mode), MockServer("send-delayed") as server:
                result = self.probe(server, mode)
                self.assertEqual(result["state"], CONFIRMED)
                self.assertEqual(result["draft"], expected)
                self.assertEqual(server.message_count, 1)

    def test_rejections_preserve_draft_without_retry(self):
        for scenario, code in (
            ("send-validation", 400),
            ("send-auth", 401),
            ("send-forbidden", 403),
            ("send-missing", 404),
            ("send-limited", 429),
        ):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server)
                self.assertEqual(result["state"], FAILED)
                self.assertEqual(result["status"], code)
                self.assertEqual(result["draft"], TEXT)
                self.assertEqual(server.message_count, 1)
                if code == 401:
                    self.assertEqual(server.refresh_count, 1)
                    self.assertTrue(result["session"])

    def test_uncertain_outcomes_never_automatically_replay(self):
        for scenario in (
            "send-drop",
            "send-server",
            "send-invalid",
            "send-wrong-channel",
            "send-wrong-author",
        ):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                result = self.probe(server)
                self.assertEqual(result["state"], UNCERTAIN)
                self.assertEqual(result["draft"], TEXT)
                self.assertEqual(server.message_count, 1)
                self.assertEqual(result["confirmed_count"], 0)

    def test_timeout_after_creation_is_uncertain_and_does_not_replay(self):
        with MockServer("send-timeout") as server:
            result = self.probe(server)
            self.assertEqual(result["state"], UNCERTAIN)
            self.assertEqual(result["draft"], TEXT)
            self.assertTrue(result["transmitted"])
            self.assertEqual(server.message_count, 1)
            self.assertEqual(len(server.sent_messages), 1)

    def test_manual_retry_requires_duplication_confirmation_and_new_id(self):
        with MockServer("send-drop-once") as server:
            result = self.probe(server, "retry")
            self.assertTrue(result["need_confirmation"])
            self.assertGreater(result["second_id"], result["id"])
            self.assertEqual(result["second_state"], CONFIRMED)
            self.assertEqual(result["draft"], "")
            self.assertEqual(server.message_count, 2)
            self.assertEqual(len(server.sent_messages), 2)

    def test_channel_results_are_independent(self):
        with MockServer("send-delayed") as server:
            result = self.probe(server, "channels")
            self.assertEqual(result["confirmed_count"], 2)
            self.assertEqual(result["state"], CONFIRMED)
            self.assertEqual(result["second_state"], CONFIRMED)
            self.assertGreater(result["second_id"], result["id"])
            self.assertEqual(result["draft"], "")
            self.assertEqual(result["second_draft"], "")
            self.assertNotEqual(
                server.sent_messages[0]["channel_id"], server.sent_messages[1]["channel_id"]
            )

    def test_cancel_before_dispatch_is_known_and_after_transmission_is_uncertain(self):
        with MockServer() as server:
            result = self.probe(server, "queued-cancel")
            self.assertEqual(result["state"], FAILED)
            self.assertFalse(result["transmitted"])
            self.assertEqual(server.message_count, 0)
            self.assertEqual(result["draft"], TEXT)
        with MockServer("send-cancel") as server:
            result = self.probe(server, "cancel")
            self.assertEqual(result["state"], UNCERTAIN)
            self.assertTrue(result["transmitted"])
            self.assertEqual(server.message_count, 1)
            self.assertEqual(result["draft"], TEXT)

    def test_instance_switch_does_not_share_same_uuid_drafts_or_results(self):
        with MockServer() as first, MockServer() as second:
            result = self.probe(first, "context", PAPO_SEND_NEW_URL=second.url)
            self.assertEqual(result["draft"], "")
            self.assertFalse(result["result_ready"])
            self.assertEqual(first.message_count, 1)
            self.assertEqual(second.message_count, 0)

    def test_message_posts_use_persistent_global_budget(self):
        with MockServer() as server:
            result = self.probe(server, "rate")
            self.assertEqual(result["confirmed_count"], 30)
            self.assertEqual(server.message_count, 30)
            self.assertEqual(len(server.request_times), 33)
            self.assertGreaterEqual(server.request_times[-1] - server.request_times[0], 0.75)


if __name__ == "__main__":
    unittest.main()
