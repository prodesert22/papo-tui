"""Controlled OpenAPI login fixtures. All accounts/cookies are synthetic."""
import json
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
        self.name = "Comunidade"
        self.requests = []
        self.request_times = []
        self.ws_controls = []
        self.ws_messages = []
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

            def respond(self, status, body=None, cookie=None, retry_after=None):
                if status >= 400:
                    body = {"type": "https://papo.invalid/errors/test", "title": "Request rejected", "status": status, **(body or {})}
                data = json.dumps(body).encode() if body is not None else b""
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                if cookie:
                    self.send_header("Set-Cookie", f"Auth={cookie}; Path=/; HttpOnly; SameSite=Strict" + ("; Secure" if secure_cookie else ""))
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
                if self.path.endswith("/ws"):
                    self.serve_websocket()
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
                        self.send_header("Location", "/different")
                        self.send_header("Content-Length", "0")
                        self.end_headers()
                    else:
                        self.respond(200, {"id": "00000000-0000-4000-8000-000000000002", "name": outer.name, "public": outer.scenario != "private"})
                elif self.path.endswith("/auth/whoami"):
                    whoami_count = sum(r[1].endswith('/auth/whoami') for r in outer.requests)
                    if self.headers.get("Cookie") != "Auth=user-fixture" or (outer.scenario == "ws-revoked" and whoami_count > 1):
                        self.respond(401, {"title": "Unauthorized"})
                    else:
                        self.respond(200, {"id": "00000000-0000-4000-8000-000000000001", "username": outer.username})
                else:
                    self.respond(404, {"title": "Not found"})

            def do_POST(self):
                outer.request_times.append(time.monotonic())
                data = self.rfile.read(int(self.headers.get("Content-Length", 0)))
                body = json.loads(data) if data else None
                cookie = self.headers.get("Cookie")
                outer.requests.append(("POST", self.path, cookie, body))
                if self.path.endswith("/auth/login_server"):
                    if body == {"server_password": "Server!123"}:
                        self.respond(204, cookie="temporary-fixture")
                    else:
                        self.respond(401, {"title": "Bad server password"})
                elif self.path.endswith("/auth/login"):
                    if outer.scenario == "private" and cookie != "Auth=temporary-fixture":
                        self.respond(401)
                    elif outer.scenario == "rejected":
                        self.respond(401, {"detail": "Synthetic!123 must not appear in UI"})
                    elif body != {"username": outer.username, "password": "Synthetic!123"}:
                        self.respond(401, {"title": "Bad credentials"})
                    else:
                        self.respond(200, {"user": {"id": "00000000-0000-4000-8000-000000000001", "username": outer.username}, "connection_violation": outer.scenario == "violation"},
                                     cookie=None if outer.scenario == "no-cookie" else "user-fixture")
                elif self.path.endswith("/auth/logout"):
                    self.respond(204, cookie="")
                else:
                    self.respond(404)

            def serve_websocket(self):
                if outer.scenario == "ws-slow":
                    time.sleep(0.6)
                if outer.scenario == "ws-unauthorized" or self.headers.get("Cookie") != "Auth=user-fixture":
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
                    self.respond(400, {"detail": 'a' * (64 * 1024)})
                    return
                key = self.headers.get("Sec-WebSocket-Key", "")
                accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
                self.protocol_version = "HTTP/1.1"
                try:
                    self.send_response(101)
                    self.send_header("Upgrade", "websocket")
                    self.send_header("Connection", "Upgrade")
                    self.send_header("Sec-WebSocket-Accept", accept)
                    if outer.scenario == 'ws-header-overflow':
                        for _ in range(20):
                            self.send_header('X-Fixture', 'a' * 4096)
                    self.end_headers()
                except OSError:
                    return  # The client can cancel while the mock is delayed.
                connection = websocket.WebSocket(skip_utf8_validation=True)
                connection.sock = self.connection
                connection.connected = True
                self.connection.settimeout(0.2)

                def send(data, opcode=websocket.ABNF.OPCODE_TEXT, fin=1):
                    frame = websocket.ABNF.create_frame(data, opcode, fin)
                    frame.mask_value = 0
                    connection.send_frame(frame)

                try:
                    if outer.scenario == "ws-invalid":
                        send('{"type":"heartbeat_ack","type":"presence_sync"}')
                    elif outer.scenario == "ws-invalid-utf8":
                        send(b'{"type":"\xff"}')
                    elif outer.scenario == "ws-binary":
                        send(b'{}', websocket.ABNF.OPCODE_BINARY)
                    elif outer.scenario == "ws-overflow":
                        send('a' * (128 * 1024 + 1))
                    elif outer.scenario == "ws-queue":
                        for _ in range(40):
                            send('{"type":"presence_sync","members":[]}')
                    elif outer.scenario == "ws-stall-fragment":
                        send('{"type":', fin=0)
                    elif outer.scenario == "ws-close":
                        send(b'\x03\xe8', websocket.ABNF.OPCODE_CLOSE)
                    elif outer.scenario == "ws-fragments":
                        # A contract-shaped message, fragmented inside UTF-8,
                        # with an interleaved control frame and a large frame.
                        message = {"type": "message", "id": "00000000-0000-4000-8000-000000000003",
                                   "channel_id": "00000000-0000-4000-8000-000000000004",
                                   "author_id": "00000000-0000-4000-8000-000000000001",
                                   "created_at": "2026-10-12T12:00:00Z", "content": "á" * 8192}
                        data = json.dumps(message, ensure_ascii=False).encode()
                        split = data.index('á'.encode()) + 1
                        send(data[:split], fin=0)
                        send(b'probe', websocket.ABNF.OPCODE_PING)
                        send(data[split:split + 17], websocket.ABNF.OPCODE_CONT, fin=0)
                        send(data[split + 17:], websocket.ABNF.OPCODE_CONT)
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
                            if event.get("type") == "heartbeat" and outer.scenario not in ("ws-no-ack", "ws-stall-fragment", "ws-close"):
                                send('{"type":"heartbeat_ack"}')
                except (OSError, ValueError, websocket.WebSocketException):
                    pass
                finally:
                    connection.connected = False

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
