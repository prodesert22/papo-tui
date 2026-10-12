"""Verify development-instance behavior; keep credentials in ignored local files."""
import argparse
from contextlib import ExitStack, closing
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import secrets
import subprocess
import time
from urllib.parse import urlsplit, urlunsplit, quote
from uuid import UUID

import requests
import websocket

from tools.contract import Contract, ROOT
from tools.ws_contract import validate_event


class ProbeError(Exception):
    """Only fixed, non-secret messages are allowed."""


def normalize(url):
    if "://" not in url:
        url = "http://" + url
    parsed = urlsplit(url)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ProbeError("Invalid base URL")
    if parsed.scheme == "http" and parsed.hostname not in ("localhost", "127.0.0.1", "::1"):
        raise ProbeError("HTTP is restricted to local development")
    return urlunsplit((parsed.scheme, parsed.netloc, parsed.path.rstrip("/"), "", ""))


def private_write(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    temporary = path.with_name(path.name + ".tmp")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "w") as output:
            json.dump(payload, output, indent=2)
            output.write("\n")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


class Probe:
    def __init__(self, base, request_interval=0, ca_bundle=None):
        self.base = normalize(base)
        self.request_interval = request_interval
        self.next_request = 0
        self.rate_limited = False
        self.ca_bundle = ca_bundle
        self.contract = Contract()
        self.report = {"base_url": self.base, "observed_at": datetime.now(timezone.utc).isoformat(),
                       "operations": [], "checks": {}, "pending": [], "drift": []}

    def session(self):
        session = requests.Session()
        session.trust_env = False  # local tests must not pass through ambient proxies
        if self.ca_bundle:
            session.verify = self.ca_bundle
        return session

    def request(self, session, method, path, expected, *, spec_path=None, **kwargs):
        if self.rate_limited:
            raise ProbeError("Backend rate limit is active; no more requests were sent")
        try:
            delay = self.next_request - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            self.next_request = time.monotonic() + self.request_interval
            cookie = self.cookie_header(session, path)
            if cookie:
                kwargs["headers"] = {**kwargs.get("headers", {}), "Cookie": cookie}
            with session.request(method, self.base + path, timeout=(5, 15),
                                 allow_redirects=False, stream=True, **kwargs) as response:
                body = bytearray()
                for chunk in response.iter_content(8192):
                    body.extend(chunk)
                    if len(body) > 1024 * 1024:
                        raise ProbeError("Response exceeded the probe's size limit")
                media = response.headers.get("Content-Type", "").split(";")[0].strip()
                payload = None
                if body:
                    try:
                        payload = json.loads(body) if media.endswith("json") else body.decode("utf-8")
                    except (ValueError, UnicodeError):
                        raise ProbeError("Response cannot be decoded") from None
                logical_path = spec_path or path
                item = {"method": method.upper(), "path": logical_path, "status": response.status_code,
                        "expected_status": expected, "request_id_present": bool(response.headers.get("X-Request-ID", "").strip()),
                        "content_type": media}
                if logical_path == "/channels/{channel_id}/messages":
                    params = kwargs.get("params", {})
                    item["history_query"] = {"order": params.get("order") if params.get("order") in ("asc", "desc") else "default",
                                             "has_cursor": "since" in params or "last_id" in params}
                try:
                    pointer = self.contract.response_schema(logical_path, method, response.status_code,
                                                             media or "application/json")
                    item["schema_errors"] = self.contract.errors(pointer, payload)
                except ValueError:
                    item["schema_errors"] = [{"path": "/", "rule": "undocumented_status_or_media"}]
                self.report["operations"].append(item)
                if response.status_code == 429:
                    self.rate_limited = True
                    raise ProbeError("Backend rate limit reached (HTTP 429); stop and wait before a new run")
                if not item["request_id_present"]:
                    self.report["drift"].append({"operation": len(self.report["operations"]), "rule": "missing_request_id"})
                if item["schema_errors"]:
                    self.report["drift"].append({"operation": len(self.report["operations"]), "rule": "response_schema"})
                accepted = expected if isinstance(expected, tuple) else (expected,)
                if response.status_code not in accepted:
                    raise ProbeError(f"Expected HTTP {expected}, observed {response.status_code} for {method.upper()} {logical_path}")
                return payload
        except requests.RequestException:
            # RequestException can contain request headers, URL credentials or bodies.
            raise ProbeError("HTTP connection failed") from None

    def login(self, session, account, private):
        if private:
            password = os.environ.get("PAPO_TEST_SERVER_PASSWORD")
            if not password:
                raise ProbeError("Set PAPO_TEST_SERVER_PASSWORD locally for a private instance")
            self.request(session, "post", "/auth/login_server", 204, json={"server_password": password})
        self.request(session, "post", "/auth/login", 200,
                     json={"username": account["username"], "password": account["password"]})
        whoami = self.request(session, "get", "/auth/whoami", 200)
        if not isinstance(whoami, dict) or not whoami.get("id"):
            raise ProbeError("Cannot confirm the authenticated account")
        account["id"] = whoami["id"]
        return whoami

    def cookie_header(self, session, path):
        jar = session.cookies.copy()
        if urlsplit(self.base).scheme == "http":
            # libcurl treats loopback as a secure cookie context. Match that only
            # for the local HTTP URLs allowed by normalize(), without changing
            # the original cookie flags or permitting redirects to other hosts.
            for item in jar:
                item.secure = False
        prepared = requests.Request("GET", self.base + path).prepare()
        return requests.cookies.get_cookie_header(jar, prepared) or ""

    def cookie(self, session):
        cookie = self.cookie_header(session, "/ws")
        if not any(part.strip().startswith("Auth=") for part in cookie.split(";")):
            raise ProbeError("No usable Auth cookie for WebSocket")
        return cookie

    def websocket(self, session):
        parsed = urlsplit(self.base)
        address = urlunsplit(("wss" if parsed.scheme == "https" else "ws", parsed.netloc, parsed.path + "/ws", "", ""))
        try:
            return websocket.create_connection(address, timeout=5, cookie=self.cookie(session),
                                               suppress_origin=True,  # native clients do not originate from a browser page
                                               redirect_limit=0, http_no_proxy=[parsed.hostname],
                                               sslopt={"ca_certs": self.ca_bundle} if self.ca_bundle else {})
        except websocket.WebSocketBadStatusException as error:
            if error.status_code == 429:
                self.rate_limited = True
            raise ProbeError(f"WebSocket handshake failed (HTTP {error.status_code})") from None
        except (OSError, websocket.WebSocketException):
            raise ProbeError("WebSocket handshake failed") from None

    def heartbeat(self, connection):
        try:
            connection.send(json.dumps({"type": "heartbeat"}))
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                connection.settimeout(max(0.01, deadline - time.monotonic()))
                raw = connection.recv()
                if not raw:
                    return "closed"
                if len(raw) > 1024 * 1024:
                    raise ProbeError("WebSocket event exceeded the probe's size limit")
                payload = json.loads(raw)
                if not isinstance(payload, dict) or not isinstance(payload.get("type"), str):
                    raise ProbeError("Malformed WebSocket event")
                known, errors = validate_event(self.contract, payload)
                event_type = payload["type"]
                safe_type = event_type if len(event_type) < 64 and all(c.isalnum() or c == "_" for c in event_type) else "unknown"
                self.report.setdefault("websocket_events", []).append({"type": safe_type, "known": known, "schema_errors": errors})
                if errors:
                    self.report["drift"].append({"event": len(self.report["websocket_events"]), "rule": "websocket_shape"})
                if payload["type"] == "heartbeat_ack":
                    return "ack"
            return "timeout"
        except websocket.WebSocketTimeoutException:
            return "timeout"
        except websocket.WebSocketConnectionClosedException:
            return "closed"
        except (ValueError, OSError, websocket.WebSocketException):
            raise ProbeError("WebSocket exchange failed") from None

    def pagination(self, session, channel_id):
        endpoint = "/channels/" + quote(channel_id, safe="") + "/messages"
        results = {}
        for order in ("asc", "desc"):
            try:
                results[order] = self.pagination_order(session, endpoint, order)
            except (ProbeError, ValueError, TypeError, KeyError, AttributeError) as error:
                if self.rate_limited:
                    raise
                reason = str(error) if isinstance(error, ProbeError) else "Invalid history envelope or cursor fields"
                self.report["pending"].append(f"History order={order}: {reason}")
                results[order] = {"verified": False}
        self.report["checks"]["pagination"] = results

    def pagination_order(self, session, endpoint, order):
        first = self.request(session, "get", endpoint, 200, spec_path="/channels/{channel_id}/messages", params={"order": order})
        data = first.get("messages", [])
        if not isinstance(data, list):
            raise ProbeError("Invalid history envelope")
        def key(message):
            return (datetime.fromisoformat(message["created_at"].replace("Z", "+00:00")), UUID(message["id"]).int)
        keys = [key(item) for item in data]
        if keys != sorted(keys, reverse=order == "desc") or len(set(keys)) != len(keys) or len(data) > 100:
            raise ProbeError("History ordering, uniqueness or page limit differs from the contract")
        result = {"verified": True, "first_page_count": len(data), "has_more": first.get("has_more", False)}
        if first.get("has_more") and data:
            last = data[-1]
            following = self.request(session, "get", endpoint, 200, spec_path="/channels/{channel_id}/messages",
                                     params={"order": order, "since": last["created_at"], "last_id": last["id"]})
            next_data = following.get("messages", [])
            next_keys = [key(item) for item in next_data]
            if next_keys != sorted(next_keys, reverse=order == "desc") or len(set(next_keys)) != len(next_keys) or len(next_data) > 100:
                raise ProbeError("Next history page ordering, uniqueness or limit differs from the contract")
            if not next_data or set(keys) & set(next_keys):
                raise ProbeError("Pagination repeated or skipped its next page")
            if any((item <= keys[-1]) if order == "asc" else (item >= keys[-1]) for item in next_keys):
                raise ProbeError("Cursor boundary differs from the requested order")
            result["second_page_count"] = len(next_data)
            result["cursor_unique"] = True
            result["second_page_has_more"] = following.get("has_more", False)
            result["total_unique_count"] = len(set(keys + next_keys))
            result["same_timestamp_across_pages"] = len({item[0] for item in keys + next_keys}) == 1
        else:
            self.report["pending"].append("History with more than 100 messages is still required")
        return result

    def prepare_history(self, session, state, credentials_path):
        channel_id = state.get("fixture_channel_id")
        if not channel_id:
            name = "papo-tui-r0-" + secrets.token_hex(4)
            before = len(self.report["operations"])
            try:
                channel = self.request(session, "post", "/channels", 201, json={"name": name, "type": "text"})
            except ProbeError:
                if len(self.report["operations"]) > before and self.report["operations"][-1]["status"] == 403:
                    self.report["pending"].append("An account with manage_channels is needed for an isolated history fixture")
                    return None
                raise
            channel_id = channel["id"]
            state["fixture_channel_id"] = channel_id
            state["fixture_message_ids"] = []
            private_write(credentials_path, state)
        while len(state.get("fixture_message_ids", [])) < 102:
            number = len(state["fixture_message_ids"]) + 1
            message = self.request(session, "post", "/messages", 201,
                                   files={"channel_id": (None, channel_id), "content": (None, f"Papo TUI R0 synthetic message {number}")})
            state["fixture_message_ids"].append(message["id"])
            private_write(credentials_path, state)
        self.report["checks"]["isolated_history_fixture"] = {"channel_created_by_probe": True, "message_count": 102}
        return channel_id

    def check_dm_counters(self, sender, recipient, peer_id):
        # Only the two synthetic development accounts participate. No message
        # is sent to existing channels or other people's conversations.
        dm = self.request(sender, "post", "/dms", (200, 201), json={"user_id": peer_id})
        endpoint = "/dms/" + quote(dm["id"], safe="")
        before = self.request(recipient, "get", endpoint, 200, spec_path="/dms/{dm_id}")
        self.request(sender, "post", "/messages", 201,
                     files={"channel_id": (None, dm["id"]), "content": (None, "Papo TUI R0 synthetic unread-counter check")})
        after = self.request(recipient, "get", endpoint, 200, spec_path="/dms/{dm_id}")
        self.report["checks"]["dm_counters"] = {
            "before_message": before["unread_count"], "after_message": after["unread_count"],
            "last_read_unchanged_after_message": before.get("last_read_message") == after.get("last_read_message")}
        history_ok = False
        try:
            self.request(recipient, "get", "/channels/" + quote(dm["id"], safe="") + "/messages", 200,
                         spec_path="/channels/{channel_id}/messages", params={"order": "desc"})
            history_ok = True
        except ProbeError as error:
            if self.rate_limited:
                raise
            self.report["pending"].append("DM history: " + str(error))
        fetched = self.request(recipient, "get", endpoint, 200, spec_path="/dms/{dm_id}")
        self.report["checks"]["dm_counters"].update({
            "history_fetch_succeeded": history_ok, "after_history_request": fetched["unread_count"],
            "last_read_unchanged_after_history_request": after.get("last_read_message") == fetched.get("last_read_message")})


def run(arguments, probe):
    credentials = Path(arguments.credentials)
    provided = bool(arguments.username)
    if provided:
        password = os.environ.get(arguments.password_env)
        if not password:
            raise ProbeError("Set the password environment variable locally")
        state = {"base_url": probe.base, "accounts": [{"username": arguments.username, "password": password}]}
    elif credentials.exists():
        state = json.loads(credentials.read_text())
        if state.get("base_url") != probe.base:
            raise ProbeError("The local credential file belongs to another instance")
    else:
        state = {"base_url": probe.base, "accounts": []}
    with probe.session() as anonymous:
        probe.request(anonymous, "get", "/health", 200)
        server = probe.request(anonymous, "get", "/server", 200)
        if not isinstance(server, dict) or not isinstance(server.get("public"), bool):
            raise ProbeError("Cannot identify the instance access policy")
        private = not server["public"]
        probe.report["checks"]["instance_public"] = not private
        probe.request(anonymous, "get", "/auth/whoami", 401)
        if arguments.create_test_accounts:
            if private:
                password = os.environ.get("PAPO_TEST_SERVER_PASSWORD")
                if not password:
                    raise ProbeError("Set PAPO_TEST_SERVER_PASSWORD locally before registering on a private instance")
                probe.request(anonymous, "post", "/auth/login_server", 204, json={"server_password": password})
            while len(state["accounts"]) < 2:
                account = {"username": "tuir0" + secrets.token_hex(4), "password": "R0!" + secrets.token_urlsafe(24)}
                response = probe.request(anonymous, "post", "/auth/register", 201, json=account)
                account["id"] = response["id"]
                state["accounts"].append(account)
                private_write(credentials, state)
    if len(state.get("accounts", [])) != (1 if provided else 2):
        raise ProbeError("Two development accounts are required; provide local credentials or --create-test-accounts")
    connections = [probe.session() for _ in state["accounts"]]
    try:
        identities = [probe.login(session, account, private) for session, account in zip(connections, state["accounts"])]
        if not provided and identities[0]["id"] == identities[1]["id"]:
            raise ProbeError("The development accounts must be different")
        probe.report["checks"]["authenticated_accounts"] = len(identities)
        if not provided:
            probe.report["checks"]["two_accounts_authenticated"] = True
        probe.report["checks"]["auth_cookie_flags"] = [
            {"secure": item.secure, "http_only": item.has_nonstandard_attr("HttpOnly"),
             "path": item.path, "same_site": item.get_nonstandard_attr("SameSite")}
            for item in connections[0].cookies if item.name == "Auth"]
        if not provided:
            private_write(credentials, state)
        if arguments.auth_probe:
            environment = dict(os.environ, PAPO_PROBE_USERNAME=state["accounts"][0]["username"],
                               PAPO_PROBE_PASSWORD=state["accounts"][0]["password"],
                               PAPO_PROBE_SERVER_PASSWORD=os.environ.get("PAPO_TEST_SERVER_PASSWORD", ""))
            process = subprocess.run([arguments.auth_probe, probe.base, "verify"], env=environment,
                                     capture_output=True, timeout=60)
            if process.returncode or b"ready=1" not in process.stdout or b"logout=1" not in process.stdout:
                raise ProbeError("The C client did not complete login/logout on the development instance")
            probe.report["checks"]["c_client_login_logout"] = True
        main = connections[0]
        channels = probe.request(main, "get", "/channels", 200)
        peer_channels = channels if provided else probe.request(connections[1], "get", "/channels", 200)
        shared = {item["id"] for item in peer_channels.get("channels", [])}
        accessible = [item for item in channels.get("channels", []) if item.get("type") == "text" and item.get("id") in shared]
        probe.report["checks"]["shared_text_channels"] = len(accessible)
        channel_id = state.get("fixture_channel_id")
        if arguments.prepare_history:
            history_state = state
            history_path = credentials
            if provided:
                history_path = Path(arguments.fixture_state)
                saved = json.loads(history_path.read_text()) if history_path.exists() else {"base_url": probe.base}
                history_state = {key: saved[key] for key in ("base_url", "fixture_channel_id", "fixture_message_ids") if key in saved}
                if history_state.get("base_url") != probe.base:
                    raise ProbeError("The fixture state belongs to another instance")
            admin_path = Path(arguments.admin_credentials)
            if admin_path.exists():
                admin = json.loads(admin_path.read_text())
                with probe.session() as setup:
                    probe.login(setup, admin, private)
                    try:
                        channel_id = probe.prepare_history(setup, history_state, history_path)
                    finally:
                        if not probe.rate_limited:
                            probe.request(setup, "post", "/auth/logout", 204)
            else:
                channel_id = probe.prepare_history(main, history_state, history_path)
        if not channel_id and accessible:
            channel_id = accessible[0]["id"]
        if channel_id:
            try:
                probe.pagination(main, channel_id)
            except ProbeError as error:
                probe.report["pending"].append(str(error))
        else:
            probe.report["pending"].append("A text channel accessible to both accounts is required")
        probe.request(main, "get", "/dms", 200)
        if arguments.check_dm_counters and not provided:
            try:
                probe.check_dm_counters(main, connections[1], identities[1]["id"])
            except ProbeError as error:
                if probe.rate_limited:
                    raise
                probe.report["pending"].append("DM counter verification: " + str(error))
        user_id = quote(identities[0]["id"], safe="")
        probe.request(main, "get", f"/users/{user_id}/notifications", 200, spec_path="/users/{user_id}/notifications")
        devices = probe.request(main, "get", "/auth/connected_devices", 200)
        probe.report["checks"]["active_connections_count"] = len(devices.get("connections", []))
        with ExitStack() as stack:
            connection = None
            try:
                connection = stack.enter_context(closing(probe.websocket(main)))
                if probe.heartbeat(connection) != "ack":
                    raise ProbeError("No heartbeat acknowledgment within the probe's 5-second observation window")
                probe.report["checks"]["websocket_cookie_handshake_and_heartbeat"] = True
            except ProbeError as error:
                if probe.rate_limited:
                    raise
                probe.report["pending"].append(str(error))
            old_cookie = probe.cookie(main)
            rotated = probe.request(main, "post", "/auth/refresh", 200)
            probe.report["checks"]["refresh_changes_cookie"] = old_cookie != probe.cookie(main)
            probe.request(main, "get", "/auth/whoami", 200)
            if connection is not None:
                after_refresh = probe.heartbeat(connection)
                probe.report["checks"]["websocket_after_refresh"] = after_refresh
                if after_refresh != "ack":
                    connection = stack.enter_context(closing(probe.websocket(main)))
                    probe.report["checks"]["new_websocket_after_refresh"] = probe.heartbeat(connection)
            connection_id = rotated.get("connection", {}).get("id")
            if not connection_id:
                raise ProbeError("Refresh did not identify the current session connection")
            probe.request(main, "post", "/auth/drop_connection", 200, json={"connection_id": connection_id})
            probe.request(main, "get", "/auth/whoami", 401)
            if connection is not None:
                probe.report["checks"]["websocket_after_revocation"] = probe.heartbeat(connection)
        probe.report["pending"].extend(["Production heartbeat interval and idle timeout require backend configuration/source confirmation",
                                         "Secure cookie behavior under HTTPS/WSS needs a TLS development instance",
                                         "Restricted-channel authorization requires an appropriate development role/account",
                                         "Exact same-timestamp cursor behavior is covered by synthetic fixtures, not seeded real timestamps"])
        if not private:
            probe.report["pending"].append("Private-instance login needs a separately configured private development instance")
    finally:
        for session in connections:
            if probe.rate_limited:
                probe.report["pending"].append("Remote logout was deferred because the backend rate limit is active")
                session.close()
                continue
            try:
                probe.request(session, "post", "/auth/logout", 204)
            except ProbeError:
                probe.report["pending"].append("Could not confirm remote logout for a probe session")
            session.close()


def main():
    parser = argparse.ArgumentParser(description="Check a Papo development instance without printing passwords or cookies")
    parser.add_argument("--url", required=True)
    parser.add_argument("--credentials", default=str(ROOT / "build/integration/accounts.json"))
    parser.add_argument("--report", default=str(ROOT / "build/integration/report.json"))
    parser.add_argument("--create-test-accounts", action="store_true", help="Register up to two synthetic development accounts")
    parser.add_argument("--prepare-history", action="store_true", help="Create an isolated channel and 102 synthetic messages, if permitted")
    parser.add_argument("--check-dm-counters", action="store_true", help="Send one synthetic message between the two development accounts to inspect unread counters")
    parser.add_argument("--admin-credentials", default=str(ROOT / "build/integration/admin.json"), help="Optional local account file with manage_channels; never printed")
    parser.add_argument("--request-interval", type=float, default=0.25, help="Probe pacing in seconds, not the server's documented rate limit")
    parser.add_argument("--auth-probe", help="Path to the built C auth-probe executable")
    parser.add_argument("--username", help="Use one provided account; its password is never written to disk")
    parser.add_argument("--password-env", default="PAPO_TEST_PASSWORD", help="Environment variable holding the provided account password")
    parser.add_argument("--fixture-state", default=str(ROOT / "build/integration/fixture-state.json"), help="IDs of resources created by the probe; no account credentials")
    parser.add_argument("--ca-bundle", help="Trusted CA file for HTTPS/WSS; certificate verification remains enabled")
    arguments = parser.parse_args()
    try:
        if arguments.request_interval < 0.25:
            raise ProbeError("Use a request interval of at least 0.25 seconds")
        if arguments.username and (arguments.create_test_accounts or arguments.check_dm_counters):
            raise ProbeError("Provided-account mode does not register accounts or test peer DMs")
        probe = Probe(normalize(arguments.url), arguments.request_interval, arguments.ca_bundle)
        try:
            run(arguments, probe)
        except ProbeError as error:
            probe.report["pending"].append(str(error))
        except (ValueError, TypeError, KeyError, OSError):
            probe.report["pending"].append("Unexpected payload or local configuration; inspect the sanitized report")
        private_write(Path(arguments.report), probe.report)
        print(f"Observed {len(probe.report['operations'])} HTTP operations; {len(probe.report['drift'])} contract divergences; {len(probe.report['pending'])} pending checks.")
        print("Sanitized report:", arguments.report)
        return 2 if probe.report["pending"] or probe.report["drift"] else 0
    except (ProbeError, OSError, ValueError) as error:
        print(str(error) if isinstance(error, ProbeError) else "Cannot read or write the local probe configuration")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
