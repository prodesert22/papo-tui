"""Produce deterministic synthetic examples, never copy live conversations."""

import json
from uuid import UUID

from tools.contract import ROOT

USER = str(UUID(int=1))
PEER = str(UUID(int=2))
CHANNEL = str(UUID(int=3))
DM = str(UUID(int=4))
DATE = "2026-10-12T12:00:00Z"


def messages():
    return [
        {
            "id": str(UUID(int=1000 + number)),
            "channel_id": CHANNEL,
            "author_id": USER if number % 2 else PEER,
            "content": f"Mensagem sintética {number}",
            "created_at": DATE,
            "edited_at": None,
            "reply_to": None,
            "attachments": [],
            "embeds": [],
            "reactions": [],
            "user_reactions": [],
        }
        for number in range(1, 103)
    ]


def build_cases():
    cases = []

    def add(
        name,
        path,
        method,
        payload,
        status=200,
        *,
        request=False,
        media="application/json",
        valid=True,
        purpose="Exemplo sintético conforme contrato",
    ):
        cases.append(
            {
                "name": name,
                "path": path,
                "method": method,
                "status": status,
                "kind": "request" if request else "response",
                "media": media,
                "valid": valid,
                "purpose": purpose,
                "payload": payload,
            }
        )

    user = {
        "id": USER,
        "username": "fixture_alice",
        "nickname": None,
        "roles": [],
        "created_at": DATE,
    }
    peer = dict(user, id=PEER, username="fixture_bob")
    channel = {
        "id": CHANNEL,
        "name": "fixture-geral",
        "type": "text",
        "position": 1,
        "topic": None,
        "permissions": [],
        "created_at": DATE,
        "last_message": None,
        "last_read_message": None,
        "last_read_at": None,
        "notification_settings": "only_mentions",
    }
    direct = {
        "id": DM,
        "user": peer,
        "created_at": DATE,
        "last_message": None,
        "last_read_message": None,
        "last_read_at": None,
        "unread_count": 0,
    }
    add("health", "/health", "get", "OK", media="text/plain")
    add(
        "server_public",
        "/server",
        "get",
        {"id": str(UUID(int=5)), "name": "Fixture", "public": True, "owner_id": USER},
    )
    add(
        "server_private",
        "/server",
        "get",
        {"id": str(UUID(int=5)), "name": "Fixture privada", "public": False},
    )
    add(
        "register_request",
        "/auth/register",
        "post",
        {"username": "fixture_alice", "password": "Synthetic!123"},
        request=True,
    )
    add(
        "register_response",
        "/auth/register",
        "post",
        {"id": USER, "username": "fixture_alice", "created_at": DATE},
        201,
    )
    add(
        "login_request",
        "/auth/login",
        "post",
        {"username": "fixture_alice", "password": "Synthetic!123"},
        request=True,
    )
    add(
        "login_missing_password",
        "/auth/login",
        "post",
        {"username": "fixture_alice"},
        request=True,
        valid=False,
        purpose="Rejeitar ausência de campo obrigatório",
    )
    add(
        "login_response",
        "/auth/login",
        "post",
        {"user": {"id": USER, "username": "fixture_alice"}, "connection_violation": False},
    )
    add(
        "private_request",
        "/auth/login_server",
        "post",
        {"server_password": "SyntheticServer!123"},
        request=True,
    )
    add("private_response", "/auth/login_server", "post", None, 204)
    add(
        "whoami",
        "/auth/whoami",
        "get",
        {**user, "settings": {"version": 1, "config": {"theme": "dark"}}},
    )
    connection = {"id": str(UUID(int=6)), "created_at": DATE, "expires_at": "2026-10-13T12:00:00Z"}
    add("refresh", "/auth/refresh", "post", {"connection": connection})
    add("devices", "/auth/connected_devices", "get", {"connections": [connection]})
    add(
        "drop_request",
        "/auth/drop_connection",
        "post",
        {"connection_id": connection["id"]},
        request=True,
    )
    add("drop_response", "/auth/drop_connection", "post", {"dropped": 1})
    add("logout", "/auth/logout", "post", None, 204)
    add(
        "channels",
        "/channels",
        "get",
        {
            "channels": [
                channel,
                dict(
                    channel,
                    id=str(UUID(int=7)),
                    type="category",
                    name="Fixture categoria",
                    position=2,
                ),
            ]
        },
    )
    data = messages()
    for order in ("asc", "desc"):
        ordered = data if order == "asc" else list(reversed(data))
        add(
            f"history_{order}_page_1",
            "/channels/{channel_id}/messages",
            "get",
            {"channel_id": CHANNEL, "messages": ordered[:100], "has_more": True},
            purpose="100 mensagens com timestamp igual; cursor deve incluir last_id",
        )
        add(
            f"history_{order}_page_2",
            "/channels/{channel_id}/messages",
            "get",
            {"channel_id": CHANNEL, "messages": ordered[100:], "has_more": False},
        )
    add(
        "history_empty",
        "/channels/{channel_id}/messages",
        "get",
        {"channel_id": CHANNEL, "messages": [], "has_more": False},
    )
    add(
        "message_request",
        "/messages",
        "post",
        {"channel_id": CHANNEL, "content": "Fixture"},
        request=True,
        media="multipart/form-data",
    )
    add("message_created", "/messages", "post", data[0], 201)
    add(
        "message_reply_request",
        "/messages",
        "post",
        {"channel_id": CHANNEL, "content": "Synthetic reply", "reply_to": data[0]["id"]},
        request=True,
        media="multipart/form-data",
    )
    add("message_reply_created", "/messages", "post", dict(data[1], reply_to=data[0]["id"]), 201)
    reaction_path = "/channels/{channel_id}/messages/{message_id}/reactions"
    for label, emoji in (
        ("unicode", {"unicode": "👍"}),
        ("custom", {"emoji_id": str(UUID(int=50))}),
    ):
        add(f"reaction_{label}_request", reaction_path, "post", emoji, request=True)
        add(
            f"reaction_{label}_created",
            reaction_path,
            "post",
            dict(emoji, message_id=data[0]["id"], user_id=USER, created_at=DATE),
            201,
        )
        add(f"reaction_{label}_remove_request", reaction_path, "delete", emoji, request=True)
    add("reaction_removed", reaction_path, "delete", None, 204)
    add(
        "reaction_users",
        reaction_path,
        "get",
        {
            "message_id": data[0]["id"],
            "has_more": False,
            "reactions": [
                {
                    "emoji_id": None,
                    "unicode": "👍",
                    "count": 2,
                    "users": [
                        {"id": str(UUID(int=60)), "user_id": USER, "created_at": DATE},
                        {"id": str(UUID(int=59)), "user_id": PEER, "created_at": DATE},
                    ],
                }
            ],
        },
    )
    add(
        "message_with_reactions",
        "/messages",
        "post",
        dict(
            data[0],
            reactions=[{"emoji_id": None, "unicode": "👍", "count": 2}],
            user_reactions=[{"id": str(UUID(int=60)), "emoji_id": None, "unicode": "👍"}],
        ),
        201,
    )
    add("message_null_content", "/messages", "post", dict(data[0], content=None), 201)
    add(
        "message_documented_null_reactions",
        "/messages",
        "post",
        dict(data[0], reactions=None),
        201,
        valid=False,
        purpose="Divergência na spec: descrição permite null, schema de reactions não declara nullable",
    )
    add("message_update", "/messages/{message_id}", "put", {"content": "Editada"}, request=True)
    add(
        "message_edited",
        "/messages/{message_id}",
        "put",
        dict(data[0], content="Editada", edited_at=DATE),
    )
    add("message_deleted", "/messages/{message_id}", "delete", None, 204)
    add("users", "/users", "get", {"users": [user, peer], "has_more": False})
    add("summary_request", "/users/user_summary_batch", "post", {"ids": [USER, PEER]}, request=True)
    add("summary_response", "/users/user_summary_batch", "post", [user, peer])
    add("profiles_request", "/users/profile_batch", "post", {"ids": [USER, PEER]}, request=True)
    add("profiles_response", "/users/profile_batch", "post", {"profiles": [user, peer]})
    add("dms", "/dms", "get", {"dms": [direct]})
    add("dm_request", "/dms", "post", {"user_id": PEER}, request=True)
    add("dm_created", "/dms", "post", direct, 201)
    add("dm_reopened", "/dms", "post", direct, 200)
    add("dm_get", "/dms/{dm_id}", "get", direct)
    add("dm_closed", "/dms/{dm_id}", "delete", None, 204)
    notification = {
        "id": str(UUID(int=8)),
        "message_id": data[0]["id"],
        "channel_id": CHANNEL,
        "author_id": None,
        "message_content": "Fixture",
        "read": False,
        "created_at": DATE,
    }
    add(
        "notifications",
        "/users/{user_id}/notifications",
        "get",
        {"notifications": [notification], "has_more": False},
    )
    add(
        "notifications_read_request",
        "/users/{user_id}/read_notification",
        "put",
        {"notification_ids": [notification["id"]]},
        request=True,
    )
    add("notifications_read_response", "/users/{user_id}/read_notification", "put", {"updated": 1})
    add(
        "notifications_read_empty",
        "/users/{user_id}/read_notification",
        "put",
        {"notification_ids": []},
        request=True,
        valid=False,
    )
    add(
        "channel_settings_request",
        "/channels/{channel_id}/user/{user_id}/settings",
        "post",
        {"notification_settings": "off"},
        request=True,
    )
    for status, path, method in [
        (400, "/auth/login", "post"),
        (401, "/auth/whoami", "get"),
        (403, "/channels/{channel_id}/messages", "get"),
        (404, "/server", "get"),
        (409, "/auth/register", "post"),
        (500, "/channels", "get"),
    ]:
        problem = {
            "type": "https://papo.invalid/errors/fixture",
            "title": "Erro sintético",
            "status": status,
            "detail": "Dados sintéticos para verificar tratamento de falhas.",
        }
        add(f"problem_{status}", path, method, problem, status, media="application/problem+json")
    data = messages()
    pin_path = "/channels/{channel_id}/messages/{message_id}/pin"
    pinned = dict(channel_id=CHANNEL, message_id=data[0]["id"], pinned_by=USER, pinned_at=DATE)
    add("message_pin_created", pin_path, "post", pinned, 201)
    add("message_pin_existing", pin_path, "post", dict(pinned, pinned_by=None), 200)
    add("message_unpin", pin_path, "delete", None, 204)
    add(
        "pinned_messages",
        "/channels/{channel_id}/pinned",
        "get",
        dict(channel_id=CHANNEL, pinned=data[:3]),
    )
    add(
        "pinned_messages_empty",
        "/channels/{channel_id}/pinned",
        "get",
        dict(channel_id=CHANNEL, pinned=[]),
    )
    add(
        "search_request",
        "/search",
        "post",
        dict(
            text="sintética",
            author=USER,
            channel_id=CHANNEL,
            mention=PEER,
            has="link",
            order="asc",
            date_start="2026-10-12",
            date_end="2026-10-13",
            contains_attachment=False,
        ),
        request=True,
    )
    add(
        "search_invalid_order",
        "/search",
        "post",
        dict(text="fixture", order="invalid"),
        request=True,
        valid=False,
    )
    add("search_empty", "/search", "post", dict(results=[], has_more=False))
    for order in ("asc", "desc"):
        results = [
            dict(
                type="message",
                id=m["id"],
                content=m["content"],
                channel_id=CHANNEL,
                channel_name="fixture-geral",
                author_id=m["author_id"],
                author_username="fixture_alice",
                created_at=DATE,
            )
            for m in data
        ]
        if order == "desc":
            results.reverse()
        for number, page in enumerate((results[:100], results[100:]), 1):
            add(
                f"search_{order}_page_{number}",
                "/search",
                "post",
                dict(results=page, has_more=number == 1),
            )
    # Server administration, as sent and read by Configurações do servidor.
    role_flags = {
        "manage_server": False,
        "manage_channels": False,
        "manage_roles": True,
        "ban_members": False,
        "pin_message": True,
        "everyone_message": False,
        "send_attachment": True,
    }
    role = {
        "id": str(UUID(int=20)),
        "name": "Fixture equipe",
        "color": "#3BA55D",
        "permissions": role_flags,
        "created_at": DATE,
    }
    server = {
        "id": str(UUID(int=5)),
        "name": "Fixture",
        "icon_blob": "",
        "icon_format": "",
        "owner_id": USER,
        "public": True,
        "owner_username": "fixture_alice",
        "created_at": DATE,
        "channel_count": 1,
        "member_count": 2,
        "role_count": 1,
    }
    add("server_patch_name", "/server", "patch", {"name": "Fixture nova"}, request=True)
    add(
        "server_patch_private",
        "/server",
        "patch",
        {"public": False, "password": "Synthetic!123"},
        request=True,
    )
    add(
        "server_patch_icon_remove",
        "/server",
        "patch",
        {"icon_blob": "", "icon_format": ""},
        request=True,
    )
    add("server_patch_response", "/server", "patch", server)
    add(
        "roles_list",
        "/roles",
        "get",
        {"roles": [role, dict(role, id=str(UUID(int=21)), color=None)]},
    )
    role_request = {"name": "Fixture equipe", "color": "#3BA55D", "permissions": role_flags}
    add("role_create_request", "/roles", "post", role_request, request=True)
    add("role_create_response", "/roles", "post", role, 201)
    add("role_update_request", "/roles/{role_id}", "put", role_request, request=True)
    add("role_update_response", "/roles/{role_id}", "put", role)
    add(
        "channel_create_request",
        "/channels",
        "post",
        {"name": "fixture-avisos", "type": "voice", "topic": "Avisos"},
        request=True,
    )
    add("channel_create_response", "/channels", "post", channel, 201)
    add(
        "channel_update_request",
        "/channels/{channel_id}",
        "put",
        {"name": "fixture-geral", "topic": ""},
        request=True,
    )
    add(
        "channel_position_request",
        "/channels/{channel_id}/change_position",
        "put",
        {"old_position": 2, "new_position": 1},
        request=True,
    )
    channel_flags = {
        "read_channel": True,
        "send_messages": True,
        "delete_messages": False,
        "connect_voice": False,
    }
    add(
        "channel_role_request",
        "/channels/{channel_id}/permissions/{role_id}",
        "put",
        {"permissions": channel_flags},
        request=True,
    )
    add(
        "emoji_create_request",
        "/emojis",
        "post",
        {"name": "fixture_aceno", "image_blob": "iVBORw0KGgo=", "format": "PNG"},
        request=True,
    )
    add(
        "emojis_page",
        "/emojis",
        "get",
        {
            "emojis": [
                {
                    "id": str(UUID(int=30)),
                    "name": "fixture_festa",
                    "format": "PNG",
                    "image_blob": "iVBORw0KGgo=",
                    "created_by": PEER,
                    "created_at": DATE,
                }
            ],
            "has_more": False,
        },
    )
    add(
        "user_role_request",
        "/users/{user_id}/roles",
        "post",
        {"role_id": role["id"]},
        request=True,
    )
    add(
        "audit_logs_page",
        "/admin/audit-logs",
        "get",
        {
            "logs": [
                {
                    "id": str(UUID(int=40)),
                    "actor_username": "fixture_alice",
                    "action": "role.create",
                    "entity_type": "role",
                    "target_user_id": None,
                    "metadata": {"name": "Fixture equipe"},
                    "created_at": DATE,
                },
                {
                    "id": str(UUID(int=41)),
                    "actor_username": "fixture_alice",
                    "action": "user.ban",
                    "entity_type": "user",
                    "target_user_id": PEER,
                    "metadata": {},
                    "created_at": DATE,
                },
            ],
            "has_more": True,
        },
    )
    return cases


def build_events():
    data = messages()
    direct = {
        "id": DM,
        "user": {"id": PEER, "username": "fixture_bob"},
        "created_at": DATE,
        "unread_count": 0,
    }
    return [
        {
            "type": "message",
            **{
                key: data[0][key]
                for key in [
                    "id",
                    "channel_id",
                    "author_id",
                    "content",
                    "created_at",
                    "reply_to",
                    "attachments",
                ]
            },
        },
        {
            "type": "message_edit",
            "id": data[0]["id"],
            "channel_id": CHANNEL,
            "content": "Editada",
            "edited_at": DATE,
        },
        {"type": "message_delete", "id": data[0]["id"], "channel_id": CHANNEL},
        {"type": "message_pin", "message_id": data[0]["id"], "is_pinned": True},
        {"type": "message_pin", "message_id": data[0]["id"], "is_pinned": False},
        {
            "type": "channel_create",
            "channel_id": CHANNEL,
            "name": "fixture-geral",
            "channel_type": "text",
            "position": 1,
            "topic": None,
        },
        {
            "type": "channel_update",
            "channel_id": CHANNEL,
            "name": "fixture-geral",
            "position": 1,
            "topic": None,
        },
        {"type": "channel_delete", "channel_id": CHANNEL},
        {"type": "dm_update", "dm": direct},
        {
            "type": "new_notification",
            "id": str(UUID(int=8)),
            "user_id": PEER,
            "message_id": data[0]["id"],
            "message_content": "Fixture",
        },
        {
            "type": "presence_sync",
            "members": [
                {"user_id": PEER, "status": "online", "status_message": None, "user_voice": []}
            ],
        },
        {
            "type": "presence_update",
            "user_id": PEER,
            "status": "away",
            "status_message": None,
            "typing": None,
            "nickname": None,
        },
        {"type": "typing", "channel_id": CHANNEL},
        {"type": "typing", "channel_id": CHANNEL, "user_id": PEER, "is_typing": True},
        {
            "type": "react_update",
            "message_id": data[0]["id"],
            "emoji_id": None,
            "unicode": "👍",
            "count": 2,
        },
        {"type": "presence_activity"},
        {"type": "heartbeat"},
        {"type": "heartbeat_ack"},
        {"type": "error", "message": "Erro sintético", "code": "fixture_error"},
        {"type": "future_event", "payload": {"fixture": True}},
    ]


def main():
    folder = ROOT / "tests/fixtures/contract"
    folder.mkdir(parents=True, exist_ok=True)
    manifest = []
    for case in build_cases():
        payload = case.pop("payload")
        case["file"] = case["name"] + ".json"
        (folder / case["file"]).write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n")
        manifest.append(case)
    (folder / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    (folder / "websocket_events.json").write_text(
        json.dumps(build_events(), ensure_ascii=False, indent=2) + "\n"
    )
    print(f"{len(manifest)} fixtures REST e {len(build_events())} eventos sintéticos gerados.")


if __name__ == "__main__":
    main()
