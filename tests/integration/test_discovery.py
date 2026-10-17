"""Discovery owner against synthetic HTTP data, including failures/cancellation."""

import json
import time
import unittest
from discovery_mock import DiscoveryMock
from history_mock import CHANNEL, OTHER, uuid
from r8_mock import DM
from test_r8 import Observer


class DiscoveryTests(unittest.TestCase):
    def observer(self, server):
        client = Observer(server.url)
        self.addCleanup(client.close)
        client.wait(
            lambda r: "dms" in r and r.get("session") and CHANNEL in r.get("channel_mentions", {})
        )
        client.command("select " + CHANNEL)
        client.wait(
            lambda r: r.get("messages", 0) > 0 and not r.get("recovering") and not r.get("loading")
        )
        return client

    def search(self, client, filters):
        return client.command("search " + json.dumps(filters))

    def test_cursor_ties_both_orders_and_filter_only_query(self):
        for order in ("asc", "desc"):
            with self.subTest(order=order), DiscoveryMock() as server:
                client = self.observer(server)
                self.search(client, dict(channel_id=CHANNEL, order=order))
                client.wait(lambda r: len(r.get("results") or []) == 100)
                client.command("searchmore")
                client.wait(lambda r: len(r["results"]) == 200)
                client.command("searchmore")
                result = client.wait(lambda r: len(r["results"]) == 205)
                ids = [v["id"] for v in result["results"]]
                self.assertEqual(ids, sorted(set(ids), reverse=order == "desc"))
                self.assertFalse(result["search_more"])
                self.assertEqual(len(server.search_calls), 3)
                self.assertEqual(server.marked, [])
                self.assertFalse(any(DM + "/messages" in r[1] for r in server.requests))

    def test_combined_filters_false_attachment_dates_mentions_and_nullable_results(self):
        with DiscoveryMock(10) as server:
            target = server.messages[3]
            target["content"] = "Olá https://papo.invalid @mention(<@" + uuid(2) + ">)"
            client = self.observer(server)
            filters = dict(
                text="Olá",
                channel_id=CHANNEL,
                author=target["author_id"],
                mention=uuid(2),
                has="link",
                order="asc",
                date_start="2026-10-12",
                date_end="2026-10-12",
                contains_attachment=False,
            )
            self.search(client, filters)
            result = client.wait(lambda r: bool(r.get("results")))
            self.assertEqual([r["id"] for r in result["results"]], [target["id"]])
            self.assertEqual(server.search_calls[0][0], filters)
            target.update(author_id=None, content=None)
            self.search(client, dict(channel_id=CHANNEL, contains_attachment=False))
            result = client.wait(lambda r: len(r.get("results") or []) == 10)
            self.assertTrue(
                any(r["content"] is None and r["author_id"] is None for r in result["results"])
            )
            self.search(client, dict(channel_id=DM))
            result = client.wait(
                lambda r: len(r.get("results") or []) == 1 and r["results"][0]["channel_id"] == DM
            )
            self.assertEqual(result["results"][0]["content"], "Mensagem direta")

    def test_pins_without_selected_conversation_are_explicit(self):
        with DiscoveryMock() as server:
            client = Observer(server.url)
            self.addCleanup(client.close)
            client.wait(lambda r: r.get("session") and CHANNEL in r.get("channel_mentions", {}))
            result = client.command("pins")
            self.assertIn("Abra uma conversa", result["pin_notice"])
            time.sleep(0.1)
            self.assertFalse(any(r[1].endswith("/pinned") for r in server.requests))

    def test_invalid_filters_do_not_send_http(self):
        with DiscoveryMock() as server:
            client = self.observer(server)
            for filters in (
                {},
                {"order": "asc"},
                {"text": "  "},
                {"channel_id": "../../bad"},
                {"date_start": "2026-02-30"},
                {"date_start": "2026-10-13", "date_end": "2026-10-12"},
                {"contains_attachment": "false"},
                {"has": "file"},
            ):
                result = self.search(client, filters)
                self.assertIn("Confira", result["search_notice"])
            time.sleep(0.15)
            self.assertEqual(server.search_calls, [])

    def test_failed_read_does_not_replay_and_bad_cursor_keeps_previous_page(self):
        for code in (400, 403, 404, 429, 500):
            with self.subTest(code=code), DiscoveryMock() as server:
                client = self.observer(server)
                server.search_status = code
                self.search(client, dict(text="Mensagem"))
                client.wait(
                    lambda r: (
                        "Ctrl+R" in r.get("search_notice", "")
                        or "permissão" in r.get("search_notice", "")
                        or "disponível" in r.get("search_notice", "")
                    )
                )
                time.sleep(0.15)
                self.assertEqual(len(server.search_calls), 1)
        with DiscoveryMock() as server:
            client = self.observer(server)
            self.search(client, dict(text="Mensagem"))
            client.wait(lambda r: len(r.get("results") or []) == 100)
            server.repeat_cursor = True
            client.command("searchmore")
            result = client.wait(lambda r: "Falha" in r.get("search_notice", ""))
            self.assertEqual(len(result["results"]), 100)

    def test_search_401_recovers_once_and_close_cancels_without_late_results(self):
        with DiscoveryMock(10) as server:
            server.search_once_401 = True
            client = self.observer(server)
            self.search(client, dict(text="Mensagem"))
            client.wait(lambda r: len(r.get("results") or []) == 10)
            self.assertEqual(len(server.search_calls), 2)
            self.assertEqual(server.refresh_count, 1)
            server.search_delay = 0.35
            server.search_started.clear()
            self.search(client, dict(text="0001"))
            self.assertTrue(server.search_started.wait(3))
            client.command("discoveryclose")
            time.sleep(0.5)
            self.assertEqual(client.command()["results"], [])
            server.search_delay = 0
            self.search(client, dict(text="0002"))
            result = client.wait(lambda r: len(r.get("results") or []) == 1)
            self.assertEqual(result["results"][0]["id"], server.messages[1]["id"])

    def test_pins_mutations_ws_reconciliation_and_no_inactive_histories(self):
        with DiscoveryMock() as server:
            client = self.observer(server)
            before = sum(r[1].split("?")[0].endswith("/messages") for r in server.requests)
            client.command("pins")
            client.wait(lambda r: len(r.get("pins") or []) == 3)
            client.command("unpin " + server.messages[0]["id"])
            result = client.wait(lambda r: len(r.get("pins") or []) == 2)
            self.assertNotIn(server.messages[0]["id"], [m["id"] for m in result["pins"]])
            target = server.messages[-1]["id"]
            client.command("pin " + target)
            client.wait(
                lambda r: (
                    len(r.get("pins") or []) == 3 and any(m["id"] == target for m in r["pins"])
                )
            )
            server.pin_ids[CHANNEL].append(server.messages[5]["id"])
            client.command(
                "event "
                + json.dumps(
                    dict(type="message_pin", message_id=server.messages[5]["id"], is_pinned=True)
                )
            )
            client.wait(lambda r: len(r.get("pins") or []) == 4)
            self.assertEqual(
                sum(r[1].split("?")[0].endswith("/messages") for r in server.requests), before
            )
            self.assertEqual(server.marked, [])
            self.assertEqual(len(server.pin_calls), 2)

    def test_pin_failures_never_replay_and_uncertain_reconciles(self):
        for code in (401, 403, 404, 409, 429, 500):
            with self.subTest(code=code), DiscoveryMock(10) as server:
                client = self.observer(server)
                client.command("pins")
                client.wait(lambda r: len(r.get("pins") or []) == 3)
                server.pin_status = code
                client.command("pin " + server.messages[-1]["id"])
                client.wait(
                    lambda r: (
                        bool(r.get("mutation_notice")) and "aguardando" not in r["mutation_notice"]
                    )
                )
                time.sleep(0.15)
                self.assertEqual(len(server.pin_calls), 1)
        with DiscoveryMock(10) as server:
            client = self.observer(server)
            client.command("pins")
            client.wait(lambda r: len(r.get("pins") or []) == 3)
            server.mutation_malformed = True
            client.command("pin " + server.messages[-1]["id"])
            client.wait(lambda r: len(r.get("pins") or []) == 4)
            self.assertEqual(len(server.pin_calls), 1)

    def test_queued_pin_cancellation_publishes_failure_and_releases_owner(self):
        with DiscoveryMock() as server:
            client = self.observer(server)
            client.command("pins")
            client.wait(lambda r: len(r.get("pins") or []) == 3)
            client.command("connectws")
            client.wait(
                lambda r: r.get("ws_open") and not r.get("recovering") and not r.get("loading")
            )
            server.search_delay = 0.4
            self.search(client, dict(text="Mensagem"))
            self.assertTrue(server.search_started.wait(3))
            target = server.messages[-1]["id"]
            client.command("pin " + target)
            client.command("recover")
            result = client.wait(lambda r: r.get("pin_result_id") == target)
            self.assertFalse(result["pin_result_success"])
            self.assertEqual(server.pin_calls, [])
            client.wait(
                lambda r: (
                    len(r.get("pins") or []) == 3
                    and not r["pin_notice"]
                    and not r.get("recovering")
                    and not r.get("loading")
                )
            )
            client.command("pin " + target)
            client.wait(lambda r: r.get("pin_result_success"))
            self.assertEqual(len(server.pin_calls), 1)

    def test_reaction_cannot_overwrite_an_inflight_pin_receipt(self):
        with DiscoveryMock() as server:
            client = self.observer(server)
            client.command("pins")
            client.wait(lambda r: len(r.get("pins") or []) == 3)
            target = server.messages[-1]["id"]
            server.pin_delay = 0.35
            client.command("pin " + target)
            self.assertTrue(server.pin_started.wait(3))
            client.command("react " + server.messages[-2]["id"])
            result = client.wait(lambda r: r.get("pin_result_id") == target)
            self.assertTrue(result["pin_result_success"])
            self.assertFalse(any("/reactions" in r[1] for r in server.requests))

    def test_result_limit_is_explicit_and_does_not_issue_extra_pages(self):
        with DiscoveryMock(1050) as server:
            client = self.observer(server)
            self.search(client, dict(text="Mensagem"))
            for page in range(1, 11):
                client.wait(lambda r: len(r.get("results") or []) == page * 100)
                if page < 10:
                    client.command("searchmore")
            result = client.command("searchmore")
            self.assertIn("Lista parcial", result["search_notice"])
            self.assertFalse(result["search_more"])
            time.sleep(0.1)
            self.assertEqual(len(server.search_calls), 10)

    def test_context_change_discards_delayed_results(self):
        with DiscoveryMock(10) as server:
            client = self.observer(server)
            server.list_delay = 0.3
            client.command("pins")
            self.assertTrue(server.list_started.wait(3))
            client.command("select " + OTHER)
            time.sleep(0.4)
            result = client.command()
            self.assertIsNone(result["pins"])
            self.assertIsNone(result["results"])


if __name__ == "__main__":
    unittest.main()
