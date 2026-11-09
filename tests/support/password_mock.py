"""Synthetic password endpoints. No development-instance observations or credentials.

The behavior mirrors the backend source: POST /users/{me}/reset marks the account,
PUT /users/{me}/password then requires that mark (409 without it) and revokes every
session of the account, and POST /auth/password_reset consumes a single-use token
without a session (410 once used or unknown)."""

from profile_mock import ME, ProfileMock

TOKEN = "Zm9vYmFyLXN5bnRoZXRpYy10b2tlbi1fLTAxMjM0NTY3"


class PasswordMock(ProfileMock):
    """password_status / prepare_status / reset_status override a response; "drop" closes
    the connection without one after applying the change."""

    def __init__(self, total=6):
        super().__init__(total)
        self.reset_flag = False
        self.password_calls = []
        self.password_status = None
        self.prepare_status = None
        self.reset_status = None
        self.tokens = {TOKEN}

    def route(self, method, path, body):
        resource = path.split("?")[0]
        if method == "POST" and resource.endswith(f"/users/{ME}/reset"):
            self.password_calls.append(("reset", body))
            if self.prepare_status:
                return self.prepare_status, {"detail": "Preparo recusado pelo teste"}
            self.reset_flag = True
            return 200, {"response": "User password is set to reset"}
        if method == "PUT" and resource.endswith(f"/users/{ME}/password"):
            self.password_calls.append(("password", body))
            if self.password_status and self.password_status != "drop":
                return self.password_status, {"detail": "Senha recusada\x1b]52;c;x\x07 pelo teste"}
            if not self.reset_flag:
                return 409, {"type": "https://papo.invalid/errors/no-reset-password"}
            self.reset_flag = False
            self.session_cookie = "revoked-by-password-change"
            return "drop" if self.password_status == "drop" else (200, {"response": "Updated"})
        if method == "POST" and resource.endswith("/auth/password_reset"):
            self.password_calls.append(("link", body))
            if self.reset_status:
                return self.reset_status, {"detail": "Link recusado\x1b[2J pelo teste"}
            if body.get("token") not in self.tokens:
                return 410, {"type": "https://papo.invalid/errors/reset-link-invalid"}
            self.tokens.discard(body["token"])
            return 200, {"response": "Password updated successfully"}
        return super().route(method, path, body)
