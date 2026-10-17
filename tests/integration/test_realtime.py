import json
import os
import select
import subprocess
import sys
import time
import unittest
from realtime_mock import RealtimeMock
from history_mock import CHANNEL, OTHER, uuid, STAMP

BINARY = sys.argv.pop(1)


class Observer:
    def __init__(self, server):
        self.process = subprocess.Popen(
            [BINARY, server.url],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=dict(os.environ, NO_PROXY="*"),
        )

    def command(self, command="snapshot"):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        if not select.select([self.process.stdout], [], [], 4)[0]:
            raise AssertionError("Observer did not respond")
        line = self.process.stdout.readline()
        if not line:
            raise AssertionError(self.process.stderr.read())
        return json.loads(line)

    def wait(self, predicate, seconds=10):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            result = self.command()
            if predicate(result):
                return result
            time.sleep(0.03)
        raise AssertionError(
            f"Observer did not settle: { {k: v for k, v in result.items() if k not in ('messages', 'channels')} }"
        )

    def ready(self):
        return self.wait(
            lambda r: (
                r.get("session")
                and r.get("selected", -1) >= 0
                and not r.get("loading")
                and not r.get("recovering")
            )
        )

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.write("quit\n")
            self.process.stdin.flush()
        self.process.wait(timeout=4)
        error = self.process.stderr.read()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()
        if self.process.returncode or error:
            raise AssertionError(error)


class RealtimeTests(unittest.TestCase):
    def observer(self, server):
        observer = Observer(server)
        self.addCleanup(observer.close)
        return observer

    def test_live_embed_replacement_survives_edits_and_rest_confirmation(self):
        with RealtimeMock() as server:
            server.messages[-1]["content"] = ""
            client = self.observer(server)
            initial = client.ready()
            event = {
                "type": "message_embeds_update",
                "channel_id": CHANNEL,
                "message_id": uuid(103),
                "embeds": [
                    {
                        "id": uuid(901),
                        "source_type": "custom",
                        "fetch_method": "manual",
                        "title": "Embed recebido ao vivo",
                        "description": "Corpo do embed",
                    }
                ],
            }
            server.emit(event)
            result = client.wait(
                lambda r: r.get("messages", [{}])[-1].get("embeds") == event["embeds"], seconds=3
            )
            self.assertEqual(result["insertions"], initial["insertions"])
            client.command(
                "event "
                + json.dumps(
                    {
                        "type": "message_edit",
                        "channel_id": CHANNEL,
                        "id": uuid(103),
                        "content": "Texto editado",
                        "edited_at": STAMP,
                    }
                )
            )
            result = client.command("confirm " + json.dumps(server.messages[-1]))
            self.assertEqual(result["messages"][-1]["embeds"], event["embeds"])
            self.assertEqual(result["messages"][-1]["content"], "Texto editado")
            # Newer REST text must not be regressed by an older text patch,
            # while the independently received embed snapshot still applies.
            newer = dict(
                server.messages[-1],
                content="Texto REST mais recente",
                edited_at="2027-01-01T00:00:00Z",
            )
            result = client.command("confirm " + json.dumps(newer))
            self.assertEqual(result["messages"][-1]["content"], newer["content"])
            self.assertEqual(result["messages"][-1]["embeds"], event["embeds"])
            for invalid in ({}, [None], [{}] * 11):
                result = client.command("event " + json.dumps(dict(event, embeds=invalid)))
                self.assertEqual(result["messages"][-1]["embeds"], event["embeds"])
            result = client.command(
                "event " + json.dumps(dict(event, embeds=[{"title": "Substituído"}]))
            )
            self.assertEqual(result["messages"][-1]["embeds"], [{"title": "Substituído"}])
            result = client.command("event " + json.dumps(dict(event, embeds=[])))
            self.assertEqual(result["messages"][-1]["embeds"], [])
            client.command("event " + json.dumps(event))
            result = client.command("event " + json.dumps(dict(event, embeds=None)))
            self.assertEqual(result["messages"][-1]["embeds"], [])
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)

    def test_embed_update_before_message_snapshot_and_after_deletion(self):
        with RealtimeMock() as server:
            server.delay = 0.6
            client = self.observer(server)
            self.assertTrue(server.snapshot_started.wait(3))
            event = {
                "type": "message_embeds_update",
                "channel_id": CHANNEL,
                "message_id": uuid(103),
                "embeds": [{"title": "Atualização durante snapshot"}],
            }
            server.emit(event)
            result = client.ready()
            self.assertEqual(result["messages"][-1]["embeds"], event["embeds"])
            event["message_id"] = uuid(104)
            client.command("event " + json.dumps(event))
            result = client.command(
                "event " + json.dumps(dict(server.message(4), type="message", content=""))
            )
            self.assertEqual(result["messages"][-1]["embeds"], event["embeds"])
            client.command(
                "event "
                + json.dumps({"type": "message_delete", "channel_id": CHANNEL, "id": uuid(104)})
            )
            client.command("event " + json.dumps(event))
            result = client.command("confirm " + json.dumps(server.message(4)))
            self.assertEqual(len(result["messages"]), 3)
            result = client.command(
                "event " + json.dumps(dict(event, channel_id=OTHER, message_id=uuid(103)))
            )
            self.assertEqual(result["messages"][-1]["embeds"], event["embeds"])
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)
            server.snapshot_started.clear()
            client.command("recover")
            self.assertTrue(server.snapshot_started.wait(3))
            event.update(
                message_id=uuid(103), embeds=[{"title": "Atualização durante recuperação"}]
            )
            server.emit(event)
            result = client.ready()
            self.assertEqual(result["messages"][-1]["embeds"], event["embeds"])

    def test_live_fragmented_message_duplicate_and_rest_fields(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            initial = client.ready()
            m = server.message(4)
            m["content"] = "Olá em tempo real"
            event = {k: v for k, v in m.items() if k not in ("embeds", "reactions", "edited_at")}
            server.emit(dict(event, type="message"), fragmented=True)
            result = client.wait(lambda r: len(r.get("messages", [])) == 4)
            server.emit(dict(event, type="message"))
            client.command(
                "confirm "
                + json.dumps(dict(m, reactions=[{"count": 1}], embeds=[{"title": "REST"}]))
            )
            result = client.ready()
            self.assertEqual(result["insertions"], initial["insertions"] + 1)
            self.assertEqual(result["messages"][-1]["embeds"], [{"title": "REST"}])
            client.wait(lambda _: ("pong", b"r7-ping") in server.ws_controls)
            self.assertIn(("pong", b"r7-ping"), server.ws_controls)
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)

    def test_delayed_snapshot_edit_delete_and_late_confirmation(self):
        with RealtimeMock() as server:
            server.delay = 0.8
            client = self.observer(server)
            self.assertTrue(server.snapshot_started.wait(4))
            server.emit(
                {
                    "type": "message_edit",
                    "channel_id": CHANNEL,
                    "id": uuid(102),
                    "content": "Edição recente",
                    "edited_at": STAMP,
                }
            )
            server.emit({"type": "message_delete", "channel_id": CHANNEL, "id": uuid(103)})
            result = client.ready()
            self.assertEqual([m["id"] for m in result["messages"]], [uuid(101), uuid(102)])
            self.assertEqual(result["messages"][-1]["content"], "Edição recente")
            result = client.command("confirm " + json.dumps(server.message(3)))
            self.assertEqual(len(result["messages"]), 2)
            result = client.command("confirm " + json.dumps(server.message(2)))
            self.assertEqual(result["messages"][-1]["content"], "Edição recente")

    def test_channel_changes_keep_selection_and_drafts_boundary(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            server.emit(
                {
                    "type": "channel_create",
                    "channel_id": uuid(5),
                    "name": "novo",
                    "channel_type": "text",
                    "position": 0,
                    "topic": None,
                }
            )
            result = client.wait(lambda r: len(r.get("channels", [])) == 5)
            self.assertEqual(result["channels"][result["selected"]]["id"], CHANNEL)
            server.emit(
                {
                    "type": "channel_update",
                    "channel_id": CHANNEL,
                    "name": "renomeado",
                    "position": 8,
                }
            )
            result = client.wait(lambda r: r.get("channels", [{}])[-1].get("name") == "renomeado")
            self.assertIn("Tópico remoto", result["channels"][-1]["topic"])
            server.emit({"type": "message_delete", "channel_id": OTHER, "id": uuid(101)})
            self.assertEqual(len(client.ready()["messages"]), 3)
            server.emit({"type": "channel_delete", "channel_id": CHANNEL})
            result = client.wait(
                lambda r: not r.get("messages") and len(r.get("channels", [])) == 4
            )
            self.assertIn("Canal removido", result["status"])
            self.assertTrue(all(OTHER not in r[1] for r in server.requests if "/messages" in r[1]))

    def test_reconnect_recovers_offline_edits_deletions_and_new_pages(self):
        with RealtimeMock(count=250) as server:
            client = self.observer(server)
            client.ready()
            client.command("older")
            client.ready()
            server.offline = True
            server.disconnect()
            client.wait(
                lambda r: (
                    r.get("session")
                    and any("/auth/whoami" in req[1] for req in server.requests[5:])
                )
            )
            with server.lock:
                server.messages = [m for m in server.messages if m["id"] != uuid(200)]
                server.messages[80]["content"] = "Editada enquanto offline"
                server.messages[80]["edited_at"] = STAMP
                server.messages += [server.message(i) for i in range(251, 371)]
            server.offline = False
            result = client.wait(
                lambda r: (
                    r.get("epoch", 0) >= 2 and not r.get("loading") and not r.get("recovering")
                ),
                seconds=15,
            )
            self.assertNotIn(uuid(200), [m["id"] for m in result["messages"]])
            self.assertIn("Editada enquanto offline", [m["content"] for m in result["messages"]])
            self.assertEqual(result["messages"][-1]["id"], uuid(470))
            self.assertTrue(any("order=asc" in r[1] for r in server.requests))
            self.assertEqual(len({m["id"] for m in result["messages"]}), len(result["messages"]))

    def test_recovery_read_failures_retry_and_preserve_window(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            server.recovery_status = 503
            client.command("recover")
            result = client.wait(lambda r: "Recuperação pendente" in r.get("status", ""))
            self.assertEqual(len(result["messages"]), 3)
            server.recovery_status = None
            result = client.wait(lambda r: not r.get("recovering") and not r.get("loading"))
            self.assertEqual(len(result["messages"]), 3)

    def test_inactive_context_suspends_retries(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            client.command("active 0")
            server.disconnect()
            time.sleep(1.5)
            self.assertEqual(sum(r[1].endswith("/ws") for r in server.requests), 1)
            client.command("active 1")
            client.wait(lambda r: r.get("epoch", 0) >= 2)

    def test_revocation_stops_reconnecting(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            server.revoked = True
            server.scenario = "refresh-revoked"
            server.disconnect()
            client.wait(lambda r: "session" in r and not r["session"] and not r["loading"])
            attempts = sum(r[1].endswith("/ws") for r in server.requests)
            time.sleep(1.5)
            self.assertEqual(sum(r[1].endswith("/ws") for r in server.requests), attempts)

    def test_future_error_and_invalid_events_are_safe(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            for event in (
                {"type": "react_update"},
                {"type": "message_pin"},
                {"type": "message_embeds_update"},
                {"type": "future\u001b]52;secret"},
                {"type": "message", "channel_id": "../../bad"},
                {"type": "error", "message": "Synthetic!123\u001b]52;secret"},
            ):
                result = client.command("event " + json.dumps(event))
                self.assertEqual(len(result["messages"]), 3)
                self.assertNotIn("Synthetic!", result["status"])
                self.assertNotIn("secret", result["status"])

    def test_reverse_edits_null_content_and_rest_confirmation(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            edit = {
                "type": "message_edit",
                "channel_id": CHANNEL,
                "id": uuid(102),
                "content": None,
                "edited_at": "2026-10-12T12:00:00.1234560001Z",
            }
            client.command("event " + json.dumps(edit))
            client.command(
                "event " + json.dumps(dict(edit, content="Edição anterior", edited_at=STAMP))
            )
            result = client.command("confirm " + json.dumps(server.message(2)))
            self.assertIsNone(result["messages"][1]["content"])
            self.assertEqual(result["messages"][1]["edited_at"], edit["edited_at"])

    def test_events_during_recovery_survive_authoritative_replacement(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            server.delay = 0.5
            server.snapshot_started.clear()
            client.command("recover")
            self.assertTrue(server.snapshot_started.wait(3))
            server.emit(
                {
                    "type": "message_edit",
                    "channel_id": CHANNEL,
                    "id": uuid(102),
                    "content": "Edição durante recuperação",
                    "edited_at": STAMP,
                }
            )
            server.emit({"type": "message_delete", "channel_id": CHANNEL, "id": uuid(103)})
            server.emit(dict(server.message(4), type="message"))
            result = client.ready()
            self.assertEqual(
                [m["id"] for m in result["messages"]], [uuid(101), uuid(102), uuid(104)]
            )
            self.assertEqual(result["messages"][1]["content"], "Edição durante recuperação")

    def test_journal_overflow_cancels_snapshot_and_recovers(self):
        with RealtimeMock() as server:
            server.delay = 0.8
            client = self.observer(server)
            self.assertTrue(server.snapshot_started.wait(3))
            for i in range(260):
                client.command(
                    "event "
                    + json.dumps(
                        {
                            "type": "message_edit",
                            "channel_id": CHANNEL,
                            "id": uuid(102),
                            "content": f"Edição {i}",
                            "edited_at": STAMP,
                        }
                    )
                )
            result = client.ready()
            self.assertEqual(len(result["messages"]), 3)
            self.assertEqual(result["messages"][1]["content"], "Edição 259")
            self.assertTrue(all(OTHER not in r[1] for r in server.requests if "/messages" in r[1]))

    def test_recovery_keeps_latest_when_gap_exceeds_window_limit(self):
        with RealtimeMock(count=20) as server:
            client = self.observer(server)
            client.ready()
            with server.lock:
                server.messages += [server.message(i) for i in range(21, 1071)]
            client.command("recover")
            result = client.wait(
                lambda r: not r.get("loading") and not r.get("recovering"), seconds=25
            )
            self.assertEqual(len(result["messages"]), 1000)
            self.assertEqual(result["messages"][-1]["id"], uuid(1170))
            self.assertEqual(len({m["id"] for m in result["messages"]}), 1000)

    def test_known_expiration_ends_offline_recovery(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            server.scenario = "refresh-short"
            client.command("refresh")
            client.ready()
            server.offline = True
            server.disconnect()
            client.wait(
                lambda r: "session" in r and not r["session"] and not r["loading"], seconds=6
            )
            self.assertEqual(server.refresh_count, 1)

    def test_ack_does_not_prevent_periodic_revocation_check(self):
        with RealtimeMock() as server:
            client = self.observer(server)
            client.ready()
            server.revoked = True
            server.scenario = "refresh-revoked"
            server.emit({"type": "heartbeat_ack"})
            result = client.wait(
                lambda r: "session" in r and not r["session"] and not r["loading"], seconds=18
            )
            self.assertEqual(result["epoch"], 0)
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(sum(r[1].endswith("/ws") for r in server.requests), 1)


if __name__ == "__main__":
    unittest.main()
