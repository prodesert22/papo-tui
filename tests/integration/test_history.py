import json
import os
import subprocess
import sys
import unittest
from history_mock import HistoryMock, CHANNEL, OTHER, uuid

BINARY = sys.argv.pop(1)


class HistoryTests(unittest.TestCase):
    def run_probe(self, server, mode="pages"):
        result = subprocess.run(
            [BINARY, server.url, mode],
            env=dict(os.environ, LC_ALL="C.UTF-8", NO_PROXY="*"),
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def test_message_revisions_and_unreferenced_user_expiry(self):
        with HistoryMock(count=3) as server:
            result = self.run_probe(server, "cache")
            self.assertTrue(all(result["cache_checks"].values()), result["cache_checks"])
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)

    def test_webhook_history_allows_unreferenced_user_expiry(self):
        for author in (None, "", "missing"):
            with self.subTest(author=author), HistoryMock(count=3) as server:
                message = server.messages[-1]
                if author == "missing":
                    message.pop("author_id")
                else:
                    message["author_id"] = author
                message["webhook_id"] = uuid(900)
                result = self.run_probe(server, "cache")
                self.assertTrue(all(result["cache_checks"].values()), result["cache_checks"])

    def test_three_pages_equal_timestamps_authors_and_no_prefetch(self):
        with HistoryMock() as server:
            result = self.run_probe(server)
            self.assertEqual([c["position"] for c in result["channels"]], [1, 2, 3, 4])
            self.assertEqual(result["initial"]["count"], 0)
            self.assertEqual([p["count"] for p in result["pages"]], [100, 200, 250])
            final = result["pages"][-1]
            self.assertEqual(final["ids"], [uuid(100 + n) for n in range(1, 251)])
            self.assertEqual(len(final["ids"]), len(set(final["ids"])))
            self.assertFalse(final["more"])
            self.assertIn("Autor indisponível", final["authors"])
            self.assertIn("Apelido", final["authors"])
            self.assertTrue(all(CHANNEL in r[1] for r in server.requests if "/messages" in r[1]))
            self.assertEqual(server.summary_calls, 1)
            self.assertTrue(
                all(
                    r[2] == "Auth=user-fixture"
                    for r in server.requests
                    if "/messages" in r[1] or "summary_batch" in r[1]
                )
            )

    def test_overlap_merges_by_id(self):
        with HistoryMock("overlap") as server:
            result = self.run_probe(server)
            self.assertEqual(result["pages"][-1]["ids"], [uuid(100 + n) for n in range(1, 251)])

    def test_confirmations_merge_exact_fractional_order_and_reject_other_contexts(self):
        with HistoryMock(count=3) as server:
            result = self.run_probe(server, "confirm")
            self.assertTrue(all(result["confirmation_checks"].values()))
            self.assertEqual(
                result["confirmed"]["ids"],
                [
                    uuid(0xFFF1),
                    uuid(2),
                    uuid(101),
                    uuid(102),
                    uuid(103),
                    uuid(0xFFF2),
                    uuid(0xFFF3),
                ],
            )
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)

    def test_confirmation_keeps_latest_within_message_limit(self):
        with HistoryMock(count=1000) as server:
            result = self.run_probe(server, "confirm-full")
            self.assertTrue(all(result["confirmation_checks"].values()))
            self.assertEqual(result["confirmed"]["count"], 1000)
            self.assertTrue(result["confirmed"]["evicted"])
            self.assertEqual(result["confirmed"]["ids"][-2:], [uuid(0xFFF2), uuid(0xFFF3)])

    def test_empty_pages_stop_even_if_has_more(self):
        for case, counts in [("empty", [0]), ("empty-page", [100, 100])]:
            with self.subTest(case=case), HistoryMock(case) as server:
                result = self.run_probe(server)
                self.assertEqual([p["count"] for p in result["pages"]], counts)
                self.assertFalse(result["pages"][-1]["more"])

    def test_read_failures_are_visible_and_not_replayed(self):
        for case, status in [
            ("forbidden", "Sem permissão"),
            ("removed", "Canal removido"),
            ("limited", "Limite"),
            ("failure", "Falha"),
            ("wrong-channel", "Falha"),
            ("invalid-date", "Falha"),
            ("null-item", "Falha"),
            ("too-many", "Falha"),
        ]:
            with self.subTest(case=case), HistoryMock(case) as server:
                result = self.run_probe(server)
                self.assertEqual(result["pages"][0]["count"], 0)
                self.assertIn(status, result["pages"][0]["status"])
                self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)
                self.assertEqual(server.summary_calls, 0)

    def test_late_selection_response_is_discarded(self):
        with HistoryMock("slow") as server:
            result = self.run_probe(server, "switch")
            self.assertEqual(result["pages"][0]["count"], 1)
            self.assertEqual(result["pages"][0]["ids"], [uuid(101)])
            self.assertTrue(any(OTHER in r[1] for r in server.requests))

    def test_summary_read_post_can_refresh_once(self):
        with HistoryMock("authors-401") as server:
            result = self.run_probe(server, "single")
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(server.summary_calls, 2)
            self.assertIn("Apelido", result["pages"][0]["authors"])
            summaries = [r for r in server.requests if "summary_batch" in r[1]]
            self.assertEqual(summaries[0][3], summaries[1][3])
            self.assertEqual(summaries[1][2], "Auth=rotated-fixture-1")

    def test_summary_failure_has_fallback_without_losing_history(self):
        with HistoryMock("authors-error") as server:
            result = self.run_probe(server, "single")
            self.assertEqual(result["pages"][0]["count"], 100)
            self.assertEqual(set(result["pages"][0]["authors"]), {"Autor indisponível"})
            self.assertEqual(server.summary_calls, 1)

    def test_retry_old_page_retains_window(self):
        with HistoryMock("older-error") as server:
            result = self.run_probe(server, "retry")
            self.assertEqual([p["count"] for p in result["pages"]], [100, 100, 200, 250])
            self.assertEqual(server.older_calls, 3)

    def test_bounded_window_can_continue_and_return_recent(self):
        with HistoryMock(count=1250) as server:
            result = self.run_probe(server, "recent")
            self.assertTrue(all(p["count"] <= 1000 for p in result["pages"]))
            self.assertEqual(result["pages"][-1]["ids"], [uuid(100 + n) for n in range(1, 1001)])
            all_ids = set(id for p in result["pages"] for id in p["ids"])
            self.assertEqual(len(all_ids), 1250)
            self.assertTrue(result["pages"][-1]["evicted"])
            self.assertEqual(result["recent"]["ids"], [uuid(100 + n) for n in range(1151, 1251)])

    def test_sliding_window_moves_in_both_directions_with_exact_cursors(self):
        for case in ("normal", "overlap"):
            with self.subTest(case=case), HistoryMock(case, count=1250) as server:
                result = self.run_probe(server, "sliding")
                oldest = result["pages"][-1]
                self.assertEqual(oldest["ids"], [uuid(100 + n) for n in range(1, 1001)])
                self.assertTrue(oldest["newer"])
                forward = result["forward"]
                self.assertTrue(all(p["count"] <= 1000 for p in forward))
                self.assertEqual(forward[-1]["ids"], [uuid(100 + n) for n in range(251, 1251)])
                self.assertFalse(forward[-1]["newer"])
                self.assertTrue(forward[-1]["more"])
                all_ids = set(oldest["ids"]).union(*(p["ids"] for p in forward))
                self.assertEqual(all_ids, {uuid(100 + n) for n in range(1, 1251)})
                self.assertEqual(
                    len(result["backward"]["ids"]), len(set(result["backward"]["ids"]))
                )
                self.assertTrue(result["backward"]["newer"])
                self.assertEqual(
                    result["backward"]["ids"][0], uuid(251 if case == "normal" else 252)
                )

    def test_forward_byte_budget_and_live_messages_do_not_skip_evicted_tail(self):
        with HistoryMock(count=700) as server:
            for m in server.messages:
                m["content"] = "x" * 7800
            result = self.run_probe(server, "sliding")
            self.assertTrue(result["pages"][-1]["newer"])
            self.assertLess(result["forward"][-1]["count"], 700)
            self.assertEqual(result["forward"][-1]["ids"][-1], uuid(800))
            self.assertFalse(result["forward"][-1]["newer"])
        with HistoryMock("forward-live", count=1250) as server:
            result = self.run_probe(server, "sliding-live")
            self.assertEqual(result["live"]["ids"], result["pages"][-1]["ids"])
            self.assertEqual(
                result["forward"][-1]["ids"], [uuid(100 + n) for n in range(252, 1252)]
            )

    def test_forward_failure_retains_window_and_requires_manual_retry(self):
        with HistoryMock("newer-error", count=1250) as server:
            result = self.run_probe(server, "sliding-retry")
            self.assertEqual(result["forward"][0]["ids"], result["pages"][-1]["ids"])
            self.assertIn("Falha", result["forward"][0]["status"])
            self.assertFalse(result["forward"][-1]["newer"])
            self.assertEqual(server.newer_calls, 4)
        with HistoryMock("repeat-newer", count=1250) as server:
            result = self.run_probe(server, "sliding")
            self.assertEqual(result["forward"][0]["ids"], result["pages"][-1]["ids"])
            self.assertIn("Falha", result["forward"][0]["status"])
            self.assertEqual(server.newer_calls, 1)

    def test_empty_forward_page_stops_and_pagination_waits_for_author_lookup(self):
        with HistoryMock("empty-newer", count=1250) as server:
            result = self.run_probe(server, "sliding")
            self.assertEqual(result["forward"][0]["ids"], result["pages"][-1]["ids"])
            self.assertFalse(result["forward"][0]["newer"])
            self.assertEqual(server.newer_calls, 1)
        with HistoryMock() as server:
            result = self.run_probe(server, "queue")
            self.assertEqual(result["pages"][0]["count"], 200)
            self.assertEqual(server.older_calls, 1)

    def test_cancel_clears_instance_data_and_voice_has_message_history(self):
        with HistoryMock() as server:
            self.assertTrue(self.run_probe(server, "context")["cleared"])
        with HistoryMock() as server:
            result = self.run_probe(server, "voice")
            self.assertEqual(result["voice"]["count"], 100)
            self.assertEqual(result["voice"]["ids"], [uuid(500 + n) for n in range(151, 251)])
            self.assertEqual(sum("/messages" in r[1] for r in server.requests), 2)

    def test_byte_budget_and_global_request_budget(self):
        with HistoryMock(count=700) as server:
            for m in server.messages:
                m["content"] = "x" * 7800
            result = self.run_probe(server)
            final = result["pages"][-1]
            self.assertLess(final["count"], 700)
            self.assertTrue(final["evicted"])
            self.assertEqual(len(set(id for p in result["pages"] for id in p["ids"])), 700)
        with HistoryMock(count=1250) as server:
            self.run_probe(server)
            times = server.request_times
            for n, at in enumerate(times):
                self.assertLessEqual(n + 1, 25 + 10 * (at - times[0]) + 1)

    def test_maximum_author_batch_and_session_rejection(self):
        with HistoryMock(count=1000) as server:
            for n, m in enumerate(server.messages):
                m["author_id"] = uuid(10000 + n)
            self.run_probe(server)
            batches = [r[3]["ids"] for r in server.requests if "summary_batch" in r[1]]
            self.assertTrue(all(1 <= len(batch) <= 1000 for batch in batches))
            ids = [id for batch in batches for id in batch]
            self.assertEqual(len(ids), 1000)
            self.assertEqual(len(set(ids)), 1000)
        with HistoryMock("authors-twice") as server:
            result = self.run_probe(server, "single")
            self.assertEqual(server.refresh_count, 1)
            self.assertEqual(server.summary_calls, 2)
            self.assertEqual(result["pages"][0]["count"], 0)
            self.assertEqual(result["final_channels"], 0)
            self.assertFalse(result["session"])

    def test_mentions_share_summary_batches_and_resolve_all_batches(self):
        with HistoryMock(count=100) as server:
            targets = {uuid(20000 + n) for n in range(1100)}
            for n, message in enumerate(server.messages):
                message["content"] = " ".join(
                    f"@mention(<@{uuid(20000 + n * 11 + k)}>)" for k in range(11)
                )
            result = self.run_probe(server, "single")
            self.assertEqual(result["pages"][0]["count"], 100)
            batches = [r[3]["ids"] for r in server.requests if "summary_batch" in r[1]]
            self.assertEqual([len(batch) for batch in batches], [1000, 103])
            ids = [id for batch in batches for id in batch]
            self.assertEqual(len(ids), len(set(ids)))
            self.assertTrue(targets.issubset(ids))

    def test_cancel_external_post_during_refresh_finishes_rotation_without_replay(self):
        with HistoryMock("authors-401") as server:
            server.scenario = "refresh-slow"
            result = self.run_probe(server, "switch")
            self.assertTrue(result["session"])
            self.assertEqual(result["pages"][0]["count"], 1)
            self.assertEqual(server.refresh_count, 1)
            requests = [r for r in server.requests if "summary_batch" in r[1]]
            self.assertEqual(len(requests), 2)
            self.assertEqual(len(requests[0][3]["ids"]), 3)
            self.assertEqual(len(requests[1][3]["ids"]), 1)

    def test_webhook_messages_with_null_author_do_not_break_history(self):
        with HistoryMock(count=5) as server:
            for message in server.messages:
                message["author_id"] = None
                message["webhook_id"] = uuid(900)
                message["external_author"] = {"source": "ci", "id": "1", "name": "Bot"}
            result = self.run_probe(server, "single")
            self.assertEqual(result["pages"][0]["count"], 5)
            self.assertEqual(set(result["pages"][0]["authors"]), {"Autor indisponível"})
            self.assertEqual(server.summary_calls, 0)

    def test_same_uuid_in_another_instance_does_not_reuse_history_or_authors(self):
        with HistoryMock(count=1) as first, HistoryMock(count=1) as second:
            second.messages[0]["content"] = "Segunda instância"
            output = subprocess.run(
                [BINARY, first.url, "instance", CHANNEL, second.url],
                env=dict(os.environ, LC_ALL="C.UTF-8", NO_PROXY="*"),
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(output.returncode, 0, output.stderr)
            result = json.loads(output.stdout)
            self.assertTrue(result["cleared"])
            self.assertEqual(result["second_content"], "Segunda instância")
            self.assertEqual(first.summary_calls, 1)
            self.assertEqual(second.summary_calls, 1)


if __name__ == "__main__":
    unittest.main()
