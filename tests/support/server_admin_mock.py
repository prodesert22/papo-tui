"""Synthetic server administration APIs. No development-instance observations or credentials."""

import base64
import copy
from datetime import datetime, timedelta, timezone
from urllib.parse import parse_qs, urlsplit

from history_mock import CHANNEL, STAMP, uuid
from profile_mock import ME, ProfileMock

BASE = datetime(2026, 10, 12, 12, 0, tzinfo=timezone.utc)
OTHER_OWNER = uuid(999)
MODERATOR = uuid(403)
EMOJI = uuid(601)
WEBHOOK = uuid(951)
TOKEN = "c0ffee" * 10 + "abcd"  # 64 hex-like characters, like the backend's
PIXEL = base64.b64encode(
    bytes.fromhex(
        "89504e470d0a1a0a0000000d4948445200000001000000010806000000"
        "1f15c4890000000d49444154789c6360000002000154a24f5d0000000049454e44ae426082"
    )
).decode()
ROLE_KEYS = (
    "manage_server",
    "manage_channels",
    "manage_roles",
    "ban_members",
    "pin_message",
    "everyone_message",
    "send_attachment",
    "manage_webhooks",
)
CHANNEL_KEYS = ("read_channel", "send_messages", "delete_messages", "connect_voice")


class ServerAdminMock(ProfileMock):
    """Applies administration mutations to synthetic state and records them.

    admin_status maps (method, path prefix) to an HTTP status, or to "drop" to
    apply the change and close the connection without a response."""

    def __init__(self, total=6):
        super().__init__(total)
        self.server_info = {
            "id": uuid(2),
            "name": self.name,
            "public": True,
            "owner_id": ME,
            "owner_username": "josé",
            "created_at": STAMP,
            "icon_blob": "",
            "icon_format": "",
            "channel_count": 4,
            "member_count": total,
            "role_count": 2,
        }
        self.admin_calls = []
        self.admin_status = {}
        self.emojis = [
            {
                "id": EMOJI,
                "name": "festa",
                "format": "PNG",
                "image_blob": PIXEL,
                "created_by": uuid(4),
                "created_at": STAMP,
            }
        ]
        self.webhooks = [
            {
                "id": WEBHOOK,
                "channel_id": CHANNEL,
                "name": "Deploys",
                "display_name": "Robô de deploy",
                "created_by": uuid(4),
                "created_at": STAMP,
            }
        ]
        self.logs = [
            {
                "id": uuid(700 + n),
                "actor_username": "josé" if n % 2 else "alice",
                "action": "role.create" if n % 3 else "user.ban",
                "entity_type": "role" if n % 3 else "user",
                "target_user_id": None if n % 3 else uuid(3),
                "metadata": {"name": f"Cargo {n}", "note": "texto\x1b]52;c;INJECT\x07"},
                "created_at": (BASE - timedelta(hours=n)).strftime("%Y-%m-%dT%H:%M:%SZ"),
            }
            for n in range(130)
        ]

    def set_owner(self, owner):
        self.owner_id = owner
        self.server_info["owner_id"] = owner

    def give(self, *names):
        """Gives the signed-in user a role with these permissions."""
        role = dict(
            id=MODERATOR,
            name="Moderação",
            color="#3ba55d",
            permissions={key: key in names for key in ROLE_KEYS},
        )
        self.roles = [r for r in self.roles if r["id"] != MODERATOR] + [role]
        self.user_roles = [dict(id=MODERATOR, name="Moderação", color="#3ba55d")]
        self.me()["roles"] = copy.deepcopy(self.user_roles)

    def outcome(self, method, resource):
        for (verb, prefix), status in self.admin_status.items():
            if verb == method and resource.startswith(prefix):
                return status
        return None

    def respond(self, method, resource, ok):
        status = self.outcome(method, resource)
        if status == "drop":
            return "drop"
        if status:
            return status, {"detail": "Recusado\x1b]52;c;x\x07 pelo teste"}
        return ok

    def route(self, method, path, body):
        parsed = urlsplit(path)
        resource = parsed.path
        if resource.startswith("/papo/"):
            resource = resource[len("/papo") :]
        query = parse_qs(parsed.query)
        if resource == "/server" and method == "GET":
            return 200, copy.deepcopy(self.server_info)
        if resource == "/server" and method == "PATCH":
            self.admin_calls.append((method, resource, copy.deepcopy(body)))
            status = self.outcome(method, resource)
            if status and status != "drop":
                return status, {"detail": "Recusado pelo teste"}
            assert set(body) <= {"name", "icon_blob", "icon_format", "password", "public"}, body
            for key in ("name", "public", "icon_blob", "icon_format"):
                if key in body:
                    self.server_info[key] = body[key]
            return self.respond(method, resource, (200, copy.deepcopy(self.server_info)))
        if resource == "/emojis" and method == "GET":
            ordered = sorted(self.emojis, key=lambda e: (e["created_at"], e["id"]))
            since, last = query.get("since", [""])[0], query.get("last_id", [""])[0]
            if since:
                ordered = [e for e in ordered if (e["created_at"], e["id"]) > (since, last)]
            return 200, {"emojis": copy.deepcopy(ordered[:25]), "has_more": len(ordered) > 25}
        if resource == "/admin/audit-logs" and method == "GET":
            assert query.get("order") == ["desc"], query
            logs = sorted(self.logs, key=lambda e: (e["created_at"], e["id"]), reverse=True)
            actors = {"josé": ME, "alice": uuid(4)}
            if "entity_type" in query:
                logs = [e for e in logs if e["entity_type"] == query["entity_type"][0]]
            if "action" in query:
                logs = [e for e in logs if e["action"] == query["action"][0]]
            if "actor_id" in query:
                logs = [e for e in logs if actors.get(e["actor_username"]) == query["actor_id"][0]]
            if "until" in query:
                until = datetime.fromisoformat(query["until"][0].replace("Z", "+00:00"))
                logs = [
                    e
                    for e in logs
                    if datetime.fromisoformat(e["created_at"].replace("Z", "+00:00")) <= until
                ]
            since, last = query.get("since", [""])[0], query.get("last_id", [""])[0]
            if since:
                logs = [e for e in logs if (e["created_at"], e["id"]) < (since, last)]
            self.admin_calls.append((method, path, None))
            status = self.outcome(method, resource)
            if status:
                return status, None
            return 200, {"logs": copy.deepcopy(logs[:100]), "has_more": len(logs) > 100}
        if resource == "/webhooks" and method == "GET":
            self.admin_calls.append((method, resource, None))
            status = self.outcome(method, resource)
            if status:
                return status, {"detail": "Recusado pelo teste"}
            return 200, {"webhooks": copy.deepcopy(self.webhooks)}
        if (resource == "/webhooks" and method == "POST") or (
            resource.startswith("/webhooks/")
            and resource.count("/") == 2
            and method in ("PATCH", "DELETE")
        ):
            self.admin_calls.append((method, resource, copy.deepcopy(body)))
            status = self.outcome(method, resource)
            if status and status != "drop":
                return status, {"detail": "Recusado\x1b]52;c;x\x07 pelo teste"}
            result = self.apply_webhook(method, resource, body)
            return "drop" if status == "drop" else result
        if method in ("POST", "PUT", "DELETE") and (
            resource.startswith("/roles")
            or resource.startswith("/emojis")
            or (resource.startswith("/channels") and self.channel_admin(resource))
            or (resource.startswith("/users/") and "/roles" in resource)
        ):
            self.admin_calls.append((method, resource, copy.deepcopy(body)))
            status = self.outcome(method, resource)
            if status and status != "drop":
                return status, {"detail": "Recusado\x1b]52;c;x\x07 pelo teste"}
            result = self.apply_admin(method, resource, body)
            return "drop" if status == "drop" else result
        return super().route(method, path, body)

    def apply_webhook(self, method, resource, body):
        """Mirrors the backend: only text and voice channels, the token only on creation."""
        if method == "POST":
            assert set(body) == {"channel_id", "name"}, body
            channel = next((c for c in self.channels if c["id"] == body["channel_id"]), None)
            if not channel:
                return 404, {"detail": "canal inexistente"}
            if not body["name"].strip() or len(body["name"]) > 80:
                return 400, {"detail": "parâmetro inválido"}
            if channel["type"] not in ("text", "voice"):
                return 403, {"detail": "operação não permitida"}
            webhook = dict(
                id=uuid(960 + len(self.webhooks)),
                channel_id=body["channel_id"],
                name=body["name"],
                created_by=ME,
                created_at=STAMP,
            )
            self.webhooks.insert(0, webhook)
            return 201, dict(copy.deepcopy(webhook), token=TOKEN)
        webhook = next((w for w in self.webhooks if w["id"] == resource.split("/")[2]), None)
        if not webhook:
            return 404, {"detail": "webhook inexistente"}
        if method == "DELETE":
            self.webhooks.remove(webhook)
            return 204, None
        assert set(body) <= {"display_name", "avatar", "avatar_format"}, body
        if "display_name" in body:
            if body["display_name"]:
                webhook["display_name"] = body["display_name"]
            else:
                webhook.pop("display_name", None)
        if "avatar" in body:
            webhook["avatar"] = bool(body["avatar"])
            if body["avatar"]:
                assert body.get("avatar_format") in ("GIF", "JPEG", "PNG", "WEBP"), body
        return 200, copy.deepcopy(webhook)

    @staticmethod
    def channel_admin(resource):
        parts = resource.strip("/").split("/")
        return len(parts) <= 2 or parts[2] in ("change_position", "permissions", "role")

    def apply_admin(self, method, resource, body):
        parts = resource.strip("/").split("/")
        if parts[0] == "roles":
            if method == "POST":
                assert set(body) <= {"name", "color", "permissions"} and body["name"], body
                role = dict(id=uuid(450 + len(self.roles)), color=None, permissions={})
                role.update(body)
                self.roles.append(role)
                return 201, copy.deepcopy(role)
            role = next((r for r in self.roles if r["id"] == parts[1]), None)
            if not role:
                return 404, None
            if method == "PUT":
                role.update(body)
                return 200, copy.deepcopy(role)
            self.roles.remove(role)
            return 204, None
        if parts[0] == "emojis":
            if method == "POST":
                assert set(body) == {"name", "image_blob", "format"}, body
                emoji = dict(
                    id=uuid(650 + len(self.emojis)), created_by=ME, created_at=STAMP, **body
                )
                self.emojis.append(emoji)
                return 201, copy.deepcopy(emoji)
            self.emojis = [e for e in self.emojis if e["id"] != parts[1]]
            return 204, None
        if parts[0] == "users":
            user = next((u for u in self.members if u["id"] == parts[1]), None)
            if not user:
                return 404, None
            if method == "POST":
                role = next(r for r in self.roles if r["id"] == body["role_id"])
                user["roles"].append(dict(id=role["id"], name=role["name"], color=role["color"]))
                return 201, dict(user_id=user["id"], role_id=role["id"], assigned_at=STAMP)
            user["roles"] = [r for r in user["roles"] if r["id"] != parts[3]]
            return 204, None
        if len(parts) == 1:
            assert method == "POST" and body["type"] in ("text", "voice", "category"), body
            channel = dict(
                id=uuid(800 + len(self.channels)),
                name=body["name"],
                type=body["type"],
                topic=body.get("topic"),
                position=len(self.channels) + 1,
                permissions=[],
            )
            self.channels.append(channel)
            return 201, copy.deepcopy(channel)
        channel = next((c for c in self.channels if c["id"] == parts[1]), None)
        if not channel:
            return 404, None
        if len(parts) == 2 and method == "PUT":
            assert set(body) <= {"name", "topic"} and body["name"], body
            channel.update(body)
            return 200, copy.deepcopy(channel)
        if len(parts) == 2:
            self.channels.remove(channel)
            return 204, None
        if parts[2] == "change_position":
            if channel["position"] != body["old_position"]:
                return 409, {"detail": "Posição desatualizada"}
            target = next(c for c in self.channels if c["position"] == body["new_position"])
            target["position"], channel["position"] = channel["position"], target["position"]
            return 200, copy.deepcopy(channel)
        entries = channel.setdefault("permissions", [])
        role = parts[3]
        entries[:] = [e for e in entries if e["role_id"] != role]
        if parts[2] == "permissions":
            assert set(body) == {"permissions"} and set(body["permissions"]) == set(CHANNEL_KEYS)
            name = next((r["name"] for r in self.roles if r["id"] == role), "")
            entries.append(dict(role_id=role, role_name=name, permissions=body["permissions"]))
            return 200, dict(
                channel_id=channel["id"], role_id=role, permissions=body["permissions"]
            )
        return 204, None


__all__ = [
    "CHANNEL",
    "EMOJI",
    "ME",
    "MODERATOR",
    "OTHER_OWNER",
    "PIXEL",
    "TOKEN",
    "WEBHOOK",
    "ServerAdminMock",
]
