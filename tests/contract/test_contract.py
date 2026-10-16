import json
import unittest
from tempfile import TemporaryDirectory
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
from uuid import UUID

import requests

from tools.contract import Contract, ROOT
from tools.contract_inventory import main as inventory_main, OPERATIONS, render
from tools.check_backend import Probe, ProbeError, normalize, run
from tools.ws_contract import validate_event


class ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = Contract()
        cls.folder = ROOT / "tests/fixtures/contract"

    def test_all_rest_fixtures_match_their_expected_contract_result(self):
        manifest = json.loads((self.folder / "manifest.json").read_text())
        self.assertEqual(len({case["name"] for case in manifest}), len(manifest))
        for case in manifest:
            with self.subTest(case=case["name"]):
                payload = json.loads((self.folder / case["file"]).read_text())
                if case["kind"] == "request":
                    pointer = self.contract.request_schema(
                        case["path"], case["method"], case["media"]
                    )
                else:
                    pointer = self.contract.response_schema(
                        case["path"], case["method"], case["status"], case["media"]
                    )
                self.assertEqual(not self.contract.errors(pointer, payload), case["valid"])

    def test_same_timestamp_cursor_pages_have_no_missing_or_duplicate_messages(self):
        all_ids = None
        for order in ("asc", "desc"):
            pages = [
                json.loads((self.folder / f"history_{order}_page_{number}.json").read_text())
                for number in (1, 2)
            ]
            self.assertEqual(len(pages[0]["messages"]), 100)
            self.assertTrue(pages[0]["has_more"])
            self.assertFalse(pages[1]["has_more"])
            data = pages[0]["messages"] + pages[1]["messages"]
            ids = [UUID(item["id"]).int for item in data]
            self.assertEqual(len(set(ids)), 102)
            self.assertEqual(ids, sorted(ids, reverse=order == "desc"))
            self.assertEqual(len({item["created_at"] for item in data}), 1)
            last = pages[0]["messages"][-1]
            tail = (
                [
                    item
                    for item in data
                    if (item["created_at"], UUID(item["id"]).int)
                    > (last["created_at"], UUID(last["id"]).int)
                ]
                if order == "asc"
                else [
                    item
                    for item in data
                    if (item["created_at"], UUID(item["id"]).int)
                    < (last["created_at"], UUID(last["id"]).int)
                ]
            )
            self.assertEqual(tail, pages[1]["messages"])
            if all_ids is not None:
                self.assertEqual(set(ids), all_ids)
            all_ids = set(ids)

    def test_websocket_examples_include_partial_and_unknown_events(self):
        events = json.loads((self.folder / "websocket_events.json").read_text())
        for event in events:
            with self.subTest(event=event["type"]):
                known, errors = validate_event(self.contract, event)
                self.assertEqual(errors, [])
                self.assertEqual(known, event["type"] != "future_event")
        message = next(item for item in events if item["type"] == "message")
        self.assertNotIn("reactions", message)
        self.assertNotIn("embeds", message)
        broken = dict(message)
        del broken["id"]
        self.assertTrue(validate_event(self.contract, broken)[1])
        partial = {
            "type": "presence_sync",
            "members": [{"user_id": str(UUID(int=1)), "status": "online", "user_voice": []}],
        }
        self.assertEqual(validate_event(self.contract, partial), (True, []))
        partial["members"][0].pop("user_id")
        self.assertTrue(validate_event(self.contract, partial)[1])

    def test_spec_limits_reject_empty_and_oversized_batches(self):
        pointer = self.contract.request_schema("/users/profile_batch", "post")
        user_id = str(UUID(int=1))
        self.assertFalse(self.contract.errors(pointer, {"ids": [user_id] * 50}))
        self.assertTrue(self.contract.errors(pointer, {"ids": [user_id] * 51}))
        pointer = self.contract.request_schema("/users/user_summary_batch", "post")
        self.assertTrue(self.contract.errors(pointer, {"ids": []}))
        self.assertTrue(self.contract.errors(pointer, {"ids": [user_id] * 1001}))

    def test_yaml_12_keeps_off_as_a_string_and_inventory_covers_operations(self):
        self.assertEqual(self.contract.spec["openapi"], "3.0.3")
        self.assertEqual(
            self.contract.spec["components"]["schemas"]["NotificationSettings"]["enum"],
            ["off", "only_mentions", "all"],
        )
        inventory = render()
        for path, methods in OPERATIONS.items():
            for method in methods:
                self.assertIn(f"## {method.upper()} `{path}`", inventory)

    def test_inventory_generates_and_checks_local_output(self):
        with TemporaryDirectory() as folder:
            target = Path(folder) / "contract/inventory.md"
            with patch("sys.argv", ["inventory", "--output", str(target)]):
                inventory_main()
            self.assertEqual(target.read_text(), render())
            with patch("sys.argv", ["inventory", "--check", "--output", str(target)]):
                inventory_main()
            target.write_text("stale")
            with (
                patch("sys.argv", ["inventory", "--check", "--output", str(target)]),
                self.assertRaises(SystemExit),
            ):
                inventory_main()


class ProbeTests(unittest.TestCase):
    def test_provided_account_mode_never_persists_its_credentials(self):
        probe = Probe("http://127.0.0.1:8080")
        user_id = str(UUID(int=1))

        def response(session, method, path, expected, **kwargs):
            if path == "/health":
                return "OK"
            if path == "/server":
                return {"public": True}
            if path == "/auth/login":
                session.cookies.set("Auth", "synthetic-cookie", domain="127.0.0.1", path="/")
                return {}
            if path == "/auth/whoami":
                return {"id": user_id, "username": "provided"}
            if path == "/channels":
                return {"channels": []}
            if path == "/auth/refresh":
                return {"connection": {"id": str(UUID(int=2))}}
            return {}

        with TemporaryDirectory() as folder:
            arguments = SimpleNamespace(
                credentials=str(Path(folder) / "accounts.json"),
                username="provided",
                password_env="PAPO_FIXTURE_PASSWORD",
                create_test_accounts=False,
                auth_probe=None,
                prepare_history=False,
                check_dm_counters=False,
            )
            with (
                patch.dict("os.environ", {"PAPO_FIXTURE_PASSWORD": "Synthetic!provided-secret"}),
                patch.object(probe, "request", side_effect=response),
                patch.object(probe, "websocket", side_effect=ProbeError("No fixture socket")),
                patch("tools.check_backend.private_write") as write,
            ):
                run(arguments, probe)
            write.assert_not_called()
            self.assertFalse(Path(arguments.credentials).exists())
            self.assertEqual(probe.report["checks"]["authenticated_accounts"], 1)
            self.assertNotIn("Synthetic!provided-secret", json.dumps(probe.report))

    def test_normalize_rejects_remote_insecure_urls_and_embedded_credentials(self):
        for url in [
            "http://example.com",
            "https://user:secret@example.com",
            "https://example.com?q=1",
            "ftp://example.com",
        ]:
            with self.subTest(url=url), self.assertRaises(ProbeError):
                normalize(url)
        self.assertEqual(normalize("127.0.0.1:8080"), "http://127.0.0.1:8080")
        self.assertEqual(normalize("https://example.com/papo/"), "https://example.com/papo")

    def test_local_secure_cookie_compatibility_preserves_original_flags_and_scope(self):
        probe = Probe("http://127.0.0.1:8080")
        with probe.session() as session:
            session.cookies.set(
                "Auth", "synthetic-cookie", domain="127.0.0.1", path="/", secure=True
            )
            session.cookies.set(
                "Other", "foreign-cookie", domain="example.com", path="/", secure=True
            )
            self.assertEqual(probe.cookie(session), "Auth=synthetic-cookie")
            self.assertTrue(next(item for item in session.cookies if item.name == "Auth").secure)
            other = Probe("https://different.example")
            self.assertEqual(other.cookie_header(session, "/ws"), "")

    def test_report_never_serializes_response_body_or_auth_cookie(self):
        probe = Probe("http://127.0.0.1:8080")
        with probe.session() as session:
            session.cookies.set("Auth", "cookie-secret", domain="127.0.0.1", path="/")
            response = requests.Response()
            response.status_code = 200
            response.headers["Content-Type"] = "application/json"
            response._content = json.dumps(
                {
                    "user": {"id": "not-a-uuid", "username": "fixture"},
                    "unexpected": "password-secret",
                }
            ).encode()
            response._content_consumed = True
            with patch.object(session, "request", return_value=response):
                probe.request(
                    session,
                    "post",
                    "/auth/login",
                    200,
                    json={"username": "fixture", "password": "password-secret"},
                )
            report = json.dumps(probe.report)
            self.assertNotIn("password-secret", report)
            self.assertNotIn("cookie-secret", report)
            self.assertIn("schema_errors", report)

    def test_network_errors_do_not_echo_exception_secrets(self):
        probe = Probe("http://127.0.0.1:8080")
        with (
            probe.session() as session,
            patch.object(session, "request", side_effect=requests.RequestException("secret-value")),
        ):
            with self.assertRaises(ProbeError) as caught:
                probe.request(session, "get", "/server", 200)
            self.assertNotIn("secret-value", str(caught.exception))

    def test_rate_limit_stops_subsequent_requests(self):
        probe = Probe("http://127.0.0.1:8080")
        response = requests.Response()
        response.status_code = 429
        response.headers["Content-Type"] = "application/problem+json"
        response._content = (
            b'{"type":"https://papo.invalid/errors/limit","title":"Limited","status":429}'
        )
        response._content_consumed = True
        with (
            probe.session() as session,
            patch.object(session, "request", return_value=response) as request,
        ):
            with self.assertRaises(ProbeError):
                probe.request(session, "get", "/server", 200)
            with self.assertRaises(ProbeError):
                probe.request(session, "get", "/server", 200)
            self.assertEqual(request.call_count, 1)
            self.assertTrue(probe.rate_limited)

    def test_history_failure_in_one_order_does_not_hide_the_other(self):
        probe = Probe("http://127.0.0.1:8080")
        with (
            probe.session() as session,
            patch.object(
                probe,
                "request",
                side_effect=[
                    ProbeError("Expected HTTP 200, observed 500"),
                    {"messages": [], "has_more": False},
                ],
            ) as request,
        ):
            probe.pagination(session, str(UUID(int=1)))
        self.assertEqual(
            [call.kwargs["params"]["order"] for call in request.call_args_list], ["asc", "desc"]
        )
        self.assertFalse(probe.report["checks"]["pagination"]["asc"]["verified"])
        self.assertTrue(probe.report["checks"]["pagination"]["desc"]["verified"])

    def test_history_rejects_duplicate_messages_inside_second_page(self):
        probe = Probe("http://127.0.0.1:8080")
        folder = ROOT / "tests/fixtures/contract"
        first = json.loads((folder / "history_asc_page_1.json").read_text())
        following = json.loads((folder / "history_asc_page_2.json").read_text())
        following["messages"][1] = following["messages"][0]
        with (
            probe.session() as session,
            patch.object(probe, "request", side_effect=[first, following]),
        ):
            with self.assertRaises(ProbeError):
                probe.pagination_order(session, "/channels/synthetic/messages", "asc")

    def test_dm_counter_report_distinguishes_failed_history_fetch(self):
        probe = Probe("http://127.0.0.1:8080")
        dm_id = str(UUID(int=1))
        values = [
            {"id": dm_id},
            {"unread_count": 4, "last_read_message": None},
            {"id": str(UUID(int=2))},
            {"unread_count": 5, "last_read_message": None},
            ProbeError("Expected HTTP 200, observed 500"),
            {"unread_count": 5, "last_read_message": None},
        ]
        with (
            probe.session() as sender,
            probe.session() as recipient,
            patch.object(probe, "request", side_effect=values),
        ):
            probe.check_dm_counters(sender, recipient, str(UUID(int=3)))
        checks = probe.report["checks"]["dm_counters"]
        self.assertEqual(checks["after_message"] - checks["before_message"], 1)
        self.assertFalse(checks["history_fetch_succeeded"])
        self.assertTrue(checks["last_read_unchanged_after_history_request"])
        self.assertNotIn(dm_id, json.dumps(probe.report))
        self.assertTrue(probe.report["pending"])


if __name__ == "__main__":
    unittest.main()
