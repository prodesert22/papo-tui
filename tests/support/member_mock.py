"""Synthetic member APIs. No development-instance observations or credentials."""

import copy
import time
from urllib.parse import parse_qs, urlsplit

from history_mock import STAMP, uuid
from r8_mock import AUTHOR, R8Mock

STAFF, BLUE = uuid(401), uuid(402)


class MemberMock(R8Mock):
    def __init__(self, total=6):
        super().__init__(count=0)
        self.roles = [
            dict(id=STAFF, name="Equipe", color="#ff0000", permissions={}),
            dict(id=BLUE, name="Comunidade", color="#00aaff", permissions={}),
        ]
        self.members = [
            dict(id=uuid(1), username="josé", nickname="Você", roles=[]),
            dict(id=AUTHOR, username="bob", nickname="Bia", roles=[self.roles[0], self.roles[1]]),
            dict(id=uuid(3), username="banido", nickname="Zed", roles=[], banned=True),
            dict(id=uuid(4), username="alice", nickname="Álice", roles=[self.roles[1]]),
            dict(id=uuid(5), username="carol", nickname="Carol", roles=[self.roles[0]]),
            dict(id=uuid(6), username="dani", nickname="Dani", roles=[]),
        ]
        for n in range(6, total):
            self.members.append(dict(id=uuid(n + 1000), username=f"user{n:04}", roles=[]))
        for user in self.members:
            user.setdefault("banned", False)
            user.update(
                created_at=STAMP, status=None, status_message="Frase de status", typing=None
            )
        self.blocked = set()
        self.member_calls = []
        self.member_status = None
        self.member_delay = 0
        self.profile_status = None
        self.blocks_status = None
        self.summary_status = None
        self.avatar = ""
        self.banner = None
        self.description = "Descrição de perfil sintético."
        self.profile_roles = None
        self.reset_response = None

    def route(self, method, path, body):
        parsed = urlsplit(path)
        resource = parsed.path
        if resource.endswith("/users") and method == "GET":
            query = parse_qs(parsed.query)
            cursor = query.get("last_id", [""])[0]
            users = sorted(self.members, key=lambda u: u["id"])
            users = [u for u in users if u["id"] > cursor]
            return 200, dict(users=copy.deepcopy(users[:100]), has_more=len(users) > 100)
        if resource.endswith("/users/user_summary_batch") and method == "POST":
            if self.summary_status:
                return self.summary_status, None
            return 200, [
                copy.deepcopy(u) for id in body["ids"] for u in self.members if u["id"] == id
            ]
        if resource.endswith("/users/profile_batch") and method == "POST":
            return 200, dict(
                profiles=[
                    self.profile(u) for id in body["ids"] for u in self.members if u["id"] == id
                ]
            )
        if resource.endswith("/profile") and method == "GET":
            if self.profile_status:
                return self.profile_status, None
            user = next((u for u in self.members if u["id"] == resource.split("/")[-2]), None)
            return (200, self.profile(user)) if user else (404, None)
        if resource.endswith("/users/blocks") and method == "GET":
            if self.blocks_status:
                return self.blocks_status, None
            return 200, dict(
                users=[copy.deepcopy(u) for u in self.members if u["id"] in self.blocked]
            )
        if "/users/" in resource and resource.rsplit("/", 1)[-1] in ("block", "ban", "reset"):
            user_id, action = resource.split("/")[-2:]
            self.member_calls.append((method, user_id, action, copy.deepcopy(body)))
            if self.member_delay:
                time.sleep(self.member_delay)
            if self.member_status:
                return self.member_status, None
            user = next((u for u in self.members if u["id"] == user_id), None)
            if not user:
                return 404, None
            if action == "block":
                assert method in ("POST", "DELETE") and body is None
                if method == "POST":
                    self.blocked.add(user_id)
                else:
                    self.blocked.discard(user_id)
                return 204, None
            if action == "ban":
                assert method == "PUT" and set(body) == {"ban_state"}
                user["banned"] = body["ban_state"]
                return 200, dict(message="Updated")
            assert method == "POST" and body is None
            if self.reset_response is not None:
                return 200, copy.deepcopy(self.reset_response)
            return 200, dict(
                reset_url=self.url + "/passwordchange/synthetic-token",
                expires_at="2030-01-01T00:00:00Z",
            )
        return super().route(method, path, body)

    def profile(self, user):
        result = dict(
            copy.deepcopy(user),
            description=self.description,
            avatar_blob=self.avatar,
            avatar_format="PNG",
            banner_media=self.banner,
        )
        if self.profile_roles is not None:
            result["roles"] = self.profile_roles
        return result
