"""Synthetic R8 fixtures; no development backend observations."""

from urllib.parse import urlsplit, parse_qs
import time
from realtime_mock import RealtimeMock
from history_mock import uuid, CHANNEL, OTHER, STAMP

DM = uuid(3000)
AUTHOR = uuid(2)


def dm():
    return dict(
        id=DM,
        user=dict(id=AUTHOR, username="bob", nickname="Bia"),
        created_at=STAMP,
        last_message=None,
        last_read_message=None,
        last_read_at=None,
        unread_count=2,
    )


def notification(n):
    return dict(
        id=uuid(4000 + n),
        message_id=uuid(5000 + n),
        channel_id=CHANNEL,
        author_id=AUTHOR,
        message_content=f"Aviso {n:04d}",
        read=False,
        created_at=STAMP,
    )


class R8Mock(RealtimeMock):
    def __init__(self, count=1, case="normal"):
        super().__init__(count=3)
        self.notifications = [notification(n) for n in range(1, count + 1)]
        self.dms = [dm()]
        self.dm_messages = [
            dict(self.message(1), channel_id=DM, author_id=AUTHOR, content="Mensagem direta")
        ]
        self.dms[0]["last_message"] = self.summary(self.dm_messages[0])
        self.r8_case = case
        self.marked = []
        self.opens = 0
        self.read_calls = 0

    @staticmethod
    def summary(message):
        return {key: message[key] for key in ("id", "created_at")}

    def other_rows(self):
        return [dict(self.message(1), channel_id=OTHER, content="Conteúdo do outro canal")]

    def find_message(self, channel_id, message_id):
        for message in self.dm_messages + self.other_rows():
            if message["id"] == message_id and message["channel_id"] == channel_id:
                return message
        for n in self.notifications:
            if n["message_id"] == message_id and n["channel_id"] == channel_id:
                return dict(id=message_id, created_at=n["created_at"])
        for channel in self.dms:
            last = channel.get("last_message") or {}
            if channel["id"] == channel_id and last.get("id") == message_id:
                return last
        return super().find_message(channel_id, message_id)

    def read_targets(self):
        return self.channels + self.dms

    def register_read(self, channel_id, message_id):
        message = super().register_read(channel_id, message_id)
        if message is None:
            return None
        if channel_id == DM:
            self.dms[0]["unread_count"] = 0
        # The backend also marks this channel's notifications up to the message.
        for n in self.notifications:
            if n["channel_id"] == channel_id:
                target = self.find_message(channel_id, n["message_id"]) or n
                if (target["created_at"], n["message_id"]) <= (
                    message["created_at"],
                    message["id"],
                ):
                    n["read"] = True
        return message

    def emit(self, event, **kwargs):
        if event.get("type") == "message":
            channel = next(
                (c for c in self.channels + self.dms if c["id"] == event["channel_id"]), None
            )
            if channel is not None:
                channel["last_message"] = self.summary(event)
        return super().emit(event, **kwargs)

    def route(self, method, path, body):
        u = urlsplit(path)
        if u.path.endswith("/notifications") and method == "GET":
            if self.r8_case == "slow-notifications":
                time.sleep(0.3)
            q = parse_qs(u.query)
            asc = q.get("order") == ["asc"]
            rows = sorted(self.notifications, key=lambda n: n["id"], reverse=not asc)
            if "last_id" in q:
                assert q["since"] == [STAMP]
                cursor = q["last_id"][0]
                rows = [n for n in rows if (n["id"] > cursor if asc else n["id"] < cursor)]
            return 200, dict(notifications=rows[:100], has_more=len(rows) > 100)
        if u.path.endswith("/read_notification") and method == "PUT":
            self.read_calls += 1
            assert u.path.endswith("/users/" + uuid(1) + "/read_notification")
            ids = body["notification_ids"]
            assert 1 <= len(ids) <= 1000 and len(set(ids)) == len(ids)
            self.marked.append(ids)
            if self.r8_case == "read-401" and self.read_calls == 1:
                return 401, None
            if self.r8_case == "read-429":
                return 429, None
            updated = 0
            for n in self.notifications:
                if n["id"] in ids:
                    n["read"] = True
                    updated += 1
            return (200, dict(updated=updated)) if updated else (404, None)
        if u.path.endswith("/dms"):
            if method == "GET":
                return 200, dict(dms=self.dms)
            if method == "POST":
                self.opens += 1
                if self.r8_case == "blocked":
                    return 403, dict(type="https://papo.invalid/errors/dm-blocked")
                if self.r8_case == "open-401" and self.opens == 1:
                    return 401, None
                if not self.dms:
                    self.dms = [dm()]
                return 200, self.dms[0]
        if "/dms/" in u.path:
            if method == "DELETE":
                self.dms = []
                return 204, None
            if method == "GET":
                return (200, self.dms[0]) if self.dms else (404, None)
        if u.path.endswith("/channels/" + DM + "/messages"):
            return 200, dict(
                channel_id=DM, messages=list(reversed(self.dm_messages)), has_more=False
            )
        if u.path.endswith("/channels/" + OTHER + "/messages"):
            return 200, dict(channel_id=OTHER, messages=self.other_rows(), has_more=False)
        if u.path.endswith("/users") and method == "GET":
            return 200, dict(
                users=[
                    dict(id=uuid(1), username="josé", created_at=STAMP),
                    dict(id=AUTHOR, username="bob", nickname="Bia", created_at=STAMP),
                    dict(id=uuid(3), username="banido", banned=True, created_at=STAMP),
                ],
                has_more=False,
            )
        return super().route(method, path, body)
