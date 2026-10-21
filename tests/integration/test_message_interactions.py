"""End-to-end C API/outbox/history interaction owner against synthetic HTTP/WS."""

from test_context import main as test_main

from test_context import argument

import json
import os
import subprocess
import tempfile
import unittest
from history_mock import CHANNEL, uuid
from interaction_mock import InteractionMock

BINARY = argument(0)


class InteractionIntegration(unittest.TestCase):
    def test_reply_reaction_participants_and_removal_share_dispatcher(self):
        with InteractionMock(count=1) as server, tempfile.TemporaryDirectory() as state_dir:
            message = server.messages[0]["id"]
            server.seed(message, {"unicode": "👍"}, [uuid(30)])
            result = subprocess.run(
                [BINARY, server.url, CHANNEL, message],
                env=dict(
                    os.environ,
                    PAPO_PROBE_USERNAME="josé",
                    PAPO_PROBE_PASSWORD="Synthetic!123",
                    XDG_STATE_HOME=state_dir,
                    XDG_CONFIG_HOME=state_dir,
                ),
                capture_output=True,
                text=True,
                encoding="utf-8",
                timeout=60,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            report = json.loads(result.stdout)
            self.assertTrue(all(value for key, value in report.items() if key != "reply_id"))
            self.assertEqual([r[0] for r in server.reaction_calls], ["POST", "DELETE"])
            self.assertEqual([r[0] for r in server.mutation_calls], ["PUT", "DELETE"])
            reply_request = next(
                r for r in server.requests if r[0] == "POST" and r[1].endswith("/messages")
            )
            self.assertEqual(reply_request[3]["reply_to"], message)
            self.assertEqual(server.sent_messages, [])
            original = server.messages[0]
            self.assertEqual(original["reactions"][0]["count"], 1)
            self.assertEqual(original["user_reactions"], [])
            self.assertNotIn("Synthetic!123", result.stdout + result.stderr)


if __name__ == "__main__":
    test_main(
        1,
    )
