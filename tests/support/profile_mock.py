"""Synthetic own-profile mutations. No development-instance observations or credentials."""

import base64
import copy
import hashlib

from history_mock import uuid
from member_mock import MemberMock

ME = uuid(1)


class ProfileMock(MemberMock):
    """Records PUT /users/{id}[/status|/avatar|/banner] and applies them to the member.

    put_status maps an endpoint ("", "status", "avatar", "banner") to an HTTP status,
    or to "drop" to close the connection without a response after applying it."""

    def __init__(self, total=6):
        super().__init__(total)
        self.profile_puts = []
        self.put_status = {}
        self.me()["description"] = "Descrição de perfil sintético."
        self.me()["typing"] = "digitando devagar"

    def me(self):
        return next(u for u in self.members if u["id"] == ME)

    def profile(self, user):
        result = super().profile(user)
        if user and "description" in user:
            result["description"] = user["description"]
        if user and user["id"] == ME:
            result["banner_media"] = self.banner
        return result

    def route(self, method, path, body):
        resource = path.split("?")[0]
        prefix = "/users/" + ME
        if (
            method == "PUT"
            and resource.endswith(prefix)
            or (method == "PUT" and prefix + "/" in resource)
        ):
            endpoint = resource.split(prefix, 1)[1].lstrip("/")
            if endpoint in ("", "status", "avatar", "banner"):
                self.profile_puts.append((endpoint, copy.deepcopy(body)))
                self.apply(endpoint, body)
                outcome = self.put_status.get(endpoint)
                if outcome == "drop":
                    return "drop"
                if outcome:
                    return outcome, {"detail": "Campo rejeitado\x1b]52;c;x\x07 pelo teste"}
                return 200, {"message": "Updated"}
        return super().route(method, path, body)

    def apply(self, endpoint, body):
        user = self.me()
        if endpoint == "":
            assert (
                {"nickname", "status", "description"}
                <= set(body)
                <= {
                    "nickname",
                    "status",
                    "description",
                    "typing",
                }
            ), body
            user["nickname"] = body["nickname"] or None
            user["status_message"] = body["status"]
            user["description"] = body["description"]
            if "typing" in body:
                user["typing"] = body["typing"]
        elif endpoint == "status":
            assert set(body) == {"status"} and body["status"] in ("away", "busy", None), body
            user["status"] = body["status"]
        elif endpoint == "avatar":
            assert set(body) == {"avatar", "avatar_format"}, body
            self.avatar = body["avatar"]
        else:
            assert set(body) == {"banner", "banner_format"}, body
            self.banner = (
                hashlib.sha256(base64.b64decode(body["banner"])).hexdigest()
                if body["banner"]
                else None
            )
