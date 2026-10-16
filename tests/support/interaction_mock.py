"""Documented message/reaction behavior with deterministic, synthetic data."""

import copy
import time
from urllib.parse import urlsplit, parse_qs
from realtime_mock import RealtimeMock
from history_mock import OTHER, STAMP, uuid, HistoryMock


class InteractionMock(RealtimeMock):
    def __init__(self, count=3):
        super().__init__(count)
        self.mutation_status = None
        self.mutation_delay = 0
        self.mutation_malformed = False
        self.mutation_broadcast = True
        self.mutation_calls = []
        self.reaction_status = None
        self.reactor_status = None
        self.reaction_delay = 0
        self.lookup_status = None
        self.rows = []
        self.reaction_calls = []
        self.next_reaction = 10000

    def seed(self, message, emoji, users):
        for user in users:
            self.next_reaction += 1
            self.rows.append(
                dict(
                    emoji,
                    id=uuid(self.next_reaction),
                    message_id=message,
                    user_id=user,
                    created_at=STAMP,
                )
            )
        self.summaries(message)

    def summaries(self, message):
        for m in self.messages:
            if m["id"] != message:
                continue
            groups = {}
            mine = []
            for row in self.rows:
                if row["message_id"] != message:
                    continue
                key = row.get("unicode") or row.get("emoji_id")
                group = groups.setdefault(
                    key, {**{k: row.get(k) for k in ("unicode", "emoji_id")}, "count": 0}
                )
                group["count"] += 1
                if row["user_id"] == uuid(1):
                    mine.append({k: row.get(k) for k in ("id", "unicode", "emoji_id")})
            m["reactions"] = list(groups.values())
            m["user_reactions"] = mine

    def route(self, method, path, body):
        parsed = urlsplit(path)
        if f"/channels/{OTHER}/messages" in parsed.path:
            return HistoryMock.route(self, method, path, body)
        if (
            "/messages/" in parsed.path
            and "/channels/" not in parsed.path
            and method in ("PUT", "DELETE")
        ):
            message = parsed.path.rsplit("/", 1)[-1]
            self.mutation_calls.append((method, message, copy.deepcopy(body)))
            if self.mutation_delay:
                time.sleep(self.mutation_delay)
            if self.mutation_status:
                return self.mutation_status, None
            target = next(
                (m for m in self.messages + self.sent_messages if m["id"] == message), None
            )
            if target is None:
                return 404, None
            event = dict(id=message, channel_id=target["channel_id"])
            if method == "PUT":
                assert set(body) == {"content"}
                if target["author_id"] != uuid(1):
                    return 403, None
                target.update(content=body["content"], edited_at="2026-10-16T12:00:00Z")
                event.update(
                    type="message_edit", content=body["content"], edited_at=target["edited_at"]
                )
                response = 200, copy.deepcopy(target)
            else:
                assert body is None
                self.messages = [m for m in self.messages if m["id"] != message]
                self.sent_messages = [m for m in self.sent_messages if m["id"] != message]
                event.update(type="message_delete")
                response = 204, None
            if self.mutation_broadcast:
                self.emit(event)
            return (200, {"id": "invalid"}) if self.mutation_malformed else response
        if parsed.path.endswith("/reactions"):
            message = parsed.path.split("/")[-2]
            if method == "GET":
                if self.reactor_status:
                    return self.reactor_status, None
                rows = sorted(
                    [r for r in self.rows if r["message_id"] == message],
                    key=lambda r: (r["created_at"], r["id"]),
                    reverse=True,
                )
                query = parse_qs(parsed.query)
                if "last_id" in query:
                    cursor = (query["since"][0], query["last_id"][0])
                    rows = [r for r in rows if (r["created_at"], r["id"]) < cursor]
                groups = {}
                for row in rows[:100]:
                    key = row.get("unicode") or row.get("emoji_id")
                    group = groups.setdefault(
                        key,
                        {
                            "emoji_id": row.get("emoji_id"),
                            "unicode": row.get("unicode"),
                            "count": 0,
                            "users": [],
                        },
                    )
                    group["users"].append({k: row[k] for k in ("id", "user_id", "created_at")})
                    group["count"] += 1
                return 200, {
                    "message_id": message,
                    "reactions": list(reversed(list(groups.values()))),
                    "has_more": len(rows) > 100,
                }
            self.reaction_calls.append((method, message, copy.deepcopy(body)))
            if self.reaction_delay:
                time.sleep(self.reaction_delay)
            if self.reaction_status:
                return self.reaction_status, None
            assert set(body) in ({"unicode"}, {"emoji_id"})
            own = [
                r
                for r in self.rows
                if r["message_id"] == message
                and r["user_id"] == uuid(1)
                and all(r.get(k) == v for k, v in body.items())
            ]
            if method == "POST" and not own:
                self.seed(message, body, [uuid(1)])
            elif method == "DELETE":
                self.rows = [r for r in self.rows if r not in own]
                self.summaries(message)
            count = sum(
                r["message_id"] == message and all(r.get(k) == v for k, v in body.items())
                for r in self.rows
            )
            # Deliberately broadcast before the mutation response to catch double increments.
            self.emit(
                dict(
                    type="react_update",
                    message_id=message,
                    emoji_id=body.get("emoji_id"),
                    unicode=body.get("unicode"),
                    count=count,
                )
            )
            if method == "DELETE":
                return 204, None
            return 201, dict(body, message_id=message, user_id=uuid(1), created_at=STAMP)
        if parsed.path.endswith("/messages") and self.lookup_status and "last_id=" in parsed.query:
            return self.lookup_status, None
        return super().route(method, path, body)
