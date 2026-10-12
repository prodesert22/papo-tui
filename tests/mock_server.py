"""Controlled OpenAPI login fixtures. All accounts/cookies are synthetic."""
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class MockServer:
    def __init__(self, scenario="public"):
        self.scenario = scenario
        self.requests = []
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def respond(self, status, body=None, cookie=None):
                if status >= 400:
                    body = {"type": "https://papo.invalid/errors/test", "title": "Request rejected", "status": status, **(body or {})}
                data = json.dumps(body).encode() if body is not None else b""
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                if cookie:
                    self.send_header("Set-Cookie", f"Auth={cookie}; Path=/; HttpOnly; SameSite=Strict")
                self.end_headers()
                try:
                    self.wfile.write(data)
                except (BrokenPipeError, ConnectionResetError):
                    pass

            def do_GET(self):
                outer.requests.append(("GET", self.path, self.headers.get("Cookie"), None))
                if self.path.endswith("/server"):
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
                        self.respond(200, {"id": "00000000-0000-4000-8000-000000000002", "name": "Comunidade", "public": outer.scenario != "private"})
                elif self.path.endswith("/auth/whoami"):
                    if self.headers.get("Cookie") != "Auth=user-fixture":
                        self.respond(401, {"title": "Unauthorized"})
                    else:
                        self.respond(200, {"id": "00000000-0000-4000-8000-000000000001", "username": "josé"})
                else:
                    self.respond(404, {"title": "Not found"})

            def do_POST(self):
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
                    elif body != {"username": "josé", "password": "Synthetic!123"}:
                        self.respond(401, {"title": "Bad credentials"})
                    else:
                        self.respond(200, {"user": {"id": "00000000-0000-4000-8000-000000000001", "username": "josé"}, "connection_violation": outer.scenario == "violation"},
                                     cookie=None if outer.scenario == "no-cookie" else "user-fixture")
                elif self.path.endswith("/auth/logout"):
                    self.respond(204, cookie="")
                else:
                    self.respond(404)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def url(self):
        return f"http://127.0.0.1:{self.server.server_port}/papo"

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_args):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
