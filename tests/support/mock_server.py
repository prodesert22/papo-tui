"""Controlled OpenAPI login fixtures. All accounts/cookies are synthetic."""

import json
from email.parser import BytesParser
from email.policy import default as email_policy
from datetime import datetime, timedelta, timezone
import base64
import hashlib
import threading
import time
import websocket
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class MockServer:
    def __init__(self, scenario="public", tls_context=None, secure_cookie=False, username="josé"):
        self.scenario = scenario
        self.username = username
        self.session_cookie = "user-fixture"
        self.refresh_count = 0
        self.api_count = 0
        self.message_count = 0
        self.sent_messages = []
        self.multipart_types = []
        self.message_received = threading.Event()
        self.name = "Comunidade"
        self.cookie_max_age = None
        self.redirect_url = "/different"
        self.requests = []
        self.api_handler = None  # Optional read-only R5 fixture routes.
        self.request_times = []
        self.ws_controls = []
        self.ws_messages = []
        self.ws_connected = threading.Event()
        self.websocket_hook = None  # Optional controlled realtime fixture.
        self.message_hook = None
        self.stopping = threading.Event()
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def handle(self):
                try:
                    super().handle()
                except OSError:
                    pass  # Expected during cancellation and TLS rejection.

            def log_message(self, *_args):
                pass

            def respond(self, status, body=None, cookie=None, retry_after=None, content_type=None):
                if status >= 400:
                    body = {
                        "type": "https://papo.invalid/errors/test",
                        "title": "Request rejected",
                        "status": status,
                        **(body or {}),
                    }
                data = (
                    body
                    if isinstance(body, bytes)
                    else json.dumps(body).encode()
                    if body is not None
                    else b""
                )
                self.send_response(status)
                self.send_header("Content-Type", content_type or "application/json")
                self.send_header("Content-Length", str(len(data)))
                if cookie:
                    self.send_header(
                        "Set-Cookie",
                        f"Auth={cookie}; Path=/; HttpOnly; SameSite=Strict"
                        + ("; Secure" if secure_cookie else "")
                        + (
                            f"; Max-Age={outer.cookie_max_age}"
                            if outer.cookie_max_age is not None
                            else ""
                        ),
                    )
                if retry_after is not None:
                    self.send_header("Retry-After", retry_after)
                self.end_headers()
                try:
                    self.wfile.write(data)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def do_GET(self):
                outer.request_times.append(time.monotonic())
                outer.requests.append(("GET", self.path, self.headers.get("Cookie"), None))
                if outer.api_handler:
                    response = outer.api_handler("GET", self.path, None)
                    if response is not None:
                        self.respond(*response)
                        return
                if self.path.endswith("/ws"):
                    self.serve_websocket()
                elif self.path.endswith("/health"):
                    if outer.scenario in ("slow-health", "timeout"):
                        outer.stopping.wait(0.6 if outer.scenario == "slow-health" else 16)
                    if outer.scenario == "no-health":
                        self.respond(404)
                    elif outer.scenario == "unavailable":
                        self.respond(503)
                    elif outer.scenario == "health-limited":
                        self.respond(429, retry_after="1")
                    else:
                        self.respond(200)
                elif self.path.endswith("/server"):
                    if outer.scenario.startswith("limited") and len(outer.requests) == 1:
                        if outer.scenario == "limited-date":
                            from email.utils import formatdate

                            retry_after = formatdate(time.time() + 2.5, usegmt=True)
                        else:
                            retry_after = "1" if outer.scenario == "limited" else "malformed"
                        self.respond(429, retry_after=retry_after)
                        return
                    if outer.scenario == "slow":
                        time.sleep(0.6)
                    if outer.scenario == "invalid":
                        self.respond(200, [])
                    elif outer.scenario == "oversized":
                        self.respond(200, {"name": "a" * (1024 * 1024), "public": True})
                    elif outer.scenario == "redirect":
                        self.send_response(302)
                        self.send_header("Location", outer.redirect_url)
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                    elif outer.scenario == "uninitialized":
                        self.respond(404)
                    elif outer.scenario == "missing-public":
                        self.respond(200, {"name": outer.name})
                    else:
                        self.respond(
                            200,
                            {
                                "id": "00000000-0000-4000-8000-000000000002",
                                "name": outer.name,
                                "public": not outer.scenario.startswith("private"),
                            },
                        )
                elif self.path.endswith("/auth/whoami"):
                    whoami_count = sum(r[1].endswith("/auth/whoami") for r in outer.requests)
                    if (
                        self.headers.get("Cookie") != "Auth=" + outer.session_cookie
                        or (outer.scenario == "ws-revoked" and whoami_count > 1)
                        or (outer.scenario == "refresh-unconfirmed" and outer.refresh_count)
                    ):
                        self.respond(401, {"title": "Unauthorized"})
                    else:
                        self.respond(
                            200,
                            {
                                "id": "wrong-id"
                                if outer.scenario == "refresh-identity" and outer.refresh_count
                                else "00000000-0000-4000-8000-000000000001",
                                "username": outer.username,
                                "connection_violation": outer.scenario == "refresh-violation"
                                and outer.refresh_count > 0,
                            },
                        )
                elif self.path.endswith("/fixture/read"):
                    outer.api_count += 1
                    if outer.scenario == "api-twice" or (
                        outer.scenario == "api-recover" and outer.api_count == 1
                    ):
                        self.respond(401)
                    elif outer.scenario == "api-permission":
                        self.respond(403)
                    elif outer.scenario == "api-ban":
                        self.respond(403, {"type": "https://papo.invalid/errors/banned"})
                    elif outer.scenario == "api-limited":
                        self.respond(429, retry_after="1")
                    elif outer.scenario == "api-violation":
                        self.respond(200, {"connection_violation": True})
                    else:
                        self.respond(200, {"fixture": True})
                elif self.path.split("?")[0].endswith("/dms"):
                    self.respond(200, {"dms": []})
                elif self.path.split("?")[0].endswith("/notifications"):
                    self.respond(200, {"notifications": [], "has_more": False})
                else:
                    self.respond(404, {"title": "Not found"})

            def mutation_fixture(self):
                outer.request_times.append(time.monotonic())
                data = self.rfile.read(int(self.headers.get("Content-Length", 0)))
                body = json.loads(data) if data else None
                outer.requests.append((self.command, self.path, self.headers.get("Cookie"), body))
                response = (
                    outer.api_handler(self.command, self.path, body) if outer.api_handler else None
                )
                self.respond(*(response if response is not None else (404, None)))

            do_PUT = mutation_fixture
            do_DELETE = mutation_fixture

            def do_POST(self):
                outer.request_times.append(time.monotonic())
                data = self.rfile.read(int(self.headers.get("Content-Length", 0)))
                if self.path.endswith("/messages"):
                    self.send_message_fixture(data)
                    return
                body = json.loads(data) if data else None
                cookie = self.headers.get("Cookie")
                outer.requests.append(("POST", self.path, cookie, body))
                if outer.api_handler:
                    response = outer.api_handler("POST", self.path, body)
                    if response is not None:
                        self.respond(*response)
                        return
                if self.path.endswith("/auth/login_server"):
                    if body == {"server_password": "Server!123"}:
                        self.respond(204, cookie="temporary-fixture")
                    else:
                        self.respond(401, {"title": "Bad server password"})
                elif self.path.endswith("/auth/register"):
                    if outer.scenario.startswith("private") and (
                        cookie != "Auth=temporary-fixture" or outer.scenario == "private-expired"
                    ):
                        self.respond(401, {"type": "https://papo.invalid/errors/unauthorized"})
                    elif outer.scenario == "register-conflict":
                        self.respond(409)
                    elif outer.scenario == "register-policy":
                        self.respond(400, {"detail": "Synthetic!123 must not appear"})
                    elif outer.scenario == "register-limited":
                        self.respond(429, retry_after="1")
                    else:
                        self.respond(
                            201,
                            {
                                "id": "00000000-0000-4000-8000-000000000001",
                                "username": body["username"],
                            },
                            cookie="untrusted-registration",
                        )
                elif self.path.endswith("/auth/refresh"):
                    outer.refresh_count += 1
                    if outer.scenario == "refresh-slow":
                        time.sleep(0.4)
                    if cookie != "Auth=" + outer.session_cookie or outer.scenario in (
                        "refresh-revoked",
                        "ws-revoked",
                    ):
                        self.respond(401)
                    elif outer.scenario == "refresh-ban":
                        self.respond(403, {"type": "https://papo.invalid/errors/banned"})
                    elif outer.scenario == "refresh-limited":
                        self.respond(429, retry_after="1")
                    else:
                        expires = (
                            datetime.now(timezone.utc)
                            + timedelta(seconds=3 if outer.scenario == "refresh-short" else 3600)
                        ).isoformat()
                        if outer.scenario == "refresh-expiry":
                            expires = "2026-02-30T12:00:00Z"
                        new_cookie = None
                        if outer.scenario != "refresh-no-cookie":
                            outer.session_cookie = "rotated-fixture-" + str(outer.refresh_count)
                            new_cookie = outer.session_cookie
                        self.respond(
                            200,
                            {"connection": {"id": "connection-fixture", "expires_at": expires}},
                            cookie=new_cookie,
                        )
                elif self.path.endswith("/auth/login"):
                    if outer.scenario.startswith("private") and (
                        cookie != "Auth=temporary-fixture" or outer.scenario == "private-expired"
                    ):
                        self.respond(401, {"type": "https://papo.invalid/errors/unauthorized"})
                    elif outer.scenario == "rejected":
                        self.respond(401, {"detail": "Synthetic!123 must not appear in UI"})
                    elif body != {"username": outer.username, "password": "Synthetic!123"}:
                        self.respond(401, {"title": "Bad credentials"})
                    else:
                        outer.session_cookie = "user-fixture"
                        self.respond(
                            200,
                            {
                                "user": {
                                    "id": "00000000-0000-4000-8000-000000000001",
                                    "username": outer.username,
                                },
                                "connection_violation": outer.scenario == "violation",
                            },
                            cookie=None if outer.scenario == "no-cookie" else "user-fixture",
                        )
                elif self.path.endswith("/auth/logout"):
                    self.respond(503 if outer.scenario == "logout-fail" else 204, cookie="")
                else:
                    self.respond(404)

            def send_message_fixture(self, data):
                cookie = self.headers.get("Cookie")
                content_type = self.headers.get("Content-Type", "")
                outer.multipart_types.append(content_type)
                try:
                    parsed = BytesParser(policy=email_policy).parsebytes(
                        ("Content-Type: " + content_type + "\r\nMIME-Version: 1.0\r\n\r\n").encode()
                        + data
                    )
                    body = {
                        part.get_param("name", header="content-disposition"): part.get_payload(
                            decode=True
                        ).decode("utf-8")
                        for part in parsed.iter_parts()
                    }
                except (ValueError, UnicodeError, TypeError):
                    self.respond(400)
                    return
                outer.requests.append(("POST", self.path, cookie, body))
                outer.message_count += 1
                outer.message_received.set()
                if not content_type.startswith("multipart/form-data;") or set(body) != {
                    "channel_id",
                    "content",
                }:
                    self.respond(400)
                    return
                if cookie != "Auth=" + outer.session_cookie:
                    self.respond(401)
                    return
                scenario = outer.scenario
                if scenario in ("send-delayed", "send-cancel"):
                    outer.stopping.wait(0.3)
                codes = {
                    "send-validation": 400,
                    "send-auth": 401,
                    "send-forbidden": 403,
                    "send-missing": 404,
                    "send-limited": 429,
                    "send-server": 500,
                }
                if scenario in codes:
                    self.respond(
                        codes[scenario], retry_after="1" if scenario == "send-limited" else None
                    )
                    return
                message = {
                    "id": f"00000000-0000-4000-8000-{outer.message_count:012x}",
                    "channel_id": body["channel_id"],
                    "author_id": "00000000-0000-4000-8000-000000000001",
                    "content": body["content"],
                    "created_at": datetime.now(timezone.utc).isoformat(),
                    "edited_at": None,
                    "reply_to": None,
                    "attachments": [],
                    "embeds": [],
                }
                outer.sent_messages.append(message)
                if outer.message_hook:
                    outer.message_hook(message)
                if scenario == "send-timeout":
                    outer.stopping.wait(16)
                if scenario == "send-drop" or (
                    scenario == "send-drop-once" and outer.message_count == 1
                ):
                    self.close_connection = True
                    return  # Created, but intentionally no HTTP response.
                if scenario == "send-invalid":
                    self.respond(201, {"id": message["id"]})
                elif scenario == "send-wrong-channel":
                    self.respond(201, {**message, "channel_id": "wrong-channel"})
                elif scenario == "send-wrong-author":
                    self.respond(201, {**message, "author_id": "wrong-account"})
                else:
                    self.respond(201, message)

            def serve_websocket(self):
                if outer.scenario == "ws-slow":
                    time.sleep(0.6)
                if (
                    outer.scenario == "ws-unauthorized"
                    or self.headers.get("Cookie") != "Auth=" + outer.session_cookie
                ):
                    self.respond(401)
                    return
                if outer.scenario == "ws-limited":
                    self.respond(429, retry_after="1")
                    return
                if outer.scenario == "ws-redirect":
                    self.send_response(302)
                    self.send_header("Location", "/different/ws")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                if outer.scenario == "ws-http-overflow":
                    self.respond(400, {"detail": "a" * (64 * 1024)})
                    return
                key = self.headers.get("Sec-WebSocket-Key", "")
                accept = base64.b64encode(
                    hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()
                ).decode()
                self.protocol_version = "HTTP/1.1"
                try:
                    self.send_response(101)
                    self.send_header("Upgrade", "websocket")
                    self.send_header("Connection", "Upgrade")
                    self.send_header("Sec-WebSocket-Accept", accept)
                    if outer.scenario == "ws-header-overflow":
                        for _ in range(20):
                            self.send_header("X-Fixture", "a" * 4096)
                    self.end_headers()
                except OSError:
                    return  # The client can cancel while the mock is delayed.
                connection = websocket.WebSocket(skip_utf8_validation=True)
                connection.sock = self.connection
                connection.connected = True
                outer.ws_connected.set()
                self.connection.settimeout(0.2)

                def send(data, opcode=websocket.ABNF.OPCODE_TEXT, fin=1):
                    frame = websocket.ABNF.create_frame(data, opcode, fin)
                    frame.mask_value = 0
                    connection.send_frame(frame)

                try:
                    if outer.websocket_hook:
                        outer.websocket_hook(connection, send)
                    if outer.scenario == "ws-invalid":
                        send('{"type":"heartbeat_ack","type":"presence_sync"}')
                    elif outer.scenario == "ws-invalid-utf8":
                        send(b'{"type":"\xff"}')
                    elif outer.scenario == "ws-binary":
                        send(b"{}", websocket.ABNF.OPCODE_BINARY)
                    elif outer.scenario == "ws-overflow":
                        send("a" * (128 * 1024 + 1))
                    elif outer.scenario == "ws-queue":
                        for _ in range(40):
                            send('{"type":"presence_sync","members":[]}')
                    elif outer.scenario == "ws-stall-fragment":
                        send('{"type":', fin=0)
                    elif outer.scenario == "ws-close":
                        send(b"\x03\xe8", websocket.ABNF.OPCODE_CLOSE)
                    elif outer.scenario == "ws-fragments":
                        # A contract-shaped message, fragmented inside UTF-8,
                        # with an interleaved control frame and a large frame.
                        message = {
                            "type": "message",
                            "id": "00000000-0000-4000-8000-000000000003",
                            "channel_id": "00000000-0000-4000-8000-000000000004",
                            "author_id": "00000000-0000-4000-8000-000000000001",
                            "created_at": "2026-10-12T12:00:00Z",
                            "content": "á" * 8192,
                        }
                        data = json.dumps(message, ensure_ascii=False).encode()
                        split = data.index("á".encode()) + 1
                        send(data[:split], fin=0)
                        send(b"probe", websocket.ABNF.OPCODE_PING)
                        send(data[split : split + 17], websocket.ABNF.OPCODE_CONT, fin=0)
                        send(data[split + 17 :], websocket.ABNF.OPCODE_CONT)
                    else:
                        send('{"type":"presence_sync","members":[]}')
                    if outer.scenario == "ws-disconnect":
                        return
                    while not outer.stopping.is_set():
                        try:
                            frame = connection.recv_frame()
                        except websocket.WebSocketTimeoutException:
                            continue
                        if frame.opcode == websocket.ABNF.OPCODE_CLOSE:
                            outer.ws_controls.append(("close", frame.data))
                            if outer.scenario != "ws-close":
                                send(frame.data, websocket.ABNF.OPCODE_CLOSE)
                            return
                        if frame.opcode == websocket.ABNF.OPCODE_PONG:
                            outer.ws_controls.append(("pong", frame.data))
                        elif frame.opcode == websocket.ABNF.OPCODE_TEXT:
                            event = json.loads(frame.data)
                            outer.ws_messages.append(event)
                            if event.get("type") == "heartbeat" and outer.scenario not in (
                                "ws-no-ack",
                                "ws-stall-fragment",
                                "ws-close",
                            ):
                                send('{"type":"heartbeat_ack"}')
                except (OSError, ValueError, websocket.WebSocketException):
                    pass
                finally:
                    connection.connected = False
                    outer.ws_connected.clear()

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.secure = tls_context is not None
        if tls_context:
            self.server.socket = tls_context.wrap_socket(self.server.socket, server_side=True)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def url(self):
        return f"{'https' if self.secure else 'http'}://127.0.0.1:{self.server.server_port}/papo"

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_args):
        self.stopping.set()
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
