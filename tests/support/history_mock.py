"""Synthetic R5 data; never observations of a development backend."""

from urllib.parse import urlsplit, parse_qs
import threading
import time
from mock_server import MockServer


def uuid(n):
    return f"00000000-0000-4000-8000-{n:012x}"


STAMP = "2026-10-12T12:00:00.123456Z"
CHANNEL = uuid(10)
OTHER = uuid(11)
VOICE = uuid(12)


def assert_history_reads(test, server, expected):
    """Bound reads per opened channel, allowing cursor synchronization.

    expected maps channel UUIDs to (minimum, maximum). An unlisted channel
    fails even when its request fits inside the total request budget.
    """
    counts = {}
    for method, path, *_ in server.requests:
        path = urlsplit(path).path
        if method != "GET" or "/channels/" not in path or not path.endswith("/messages"):
            continue
        channel = path.split("/")[-2]
        counts[channel] = counts.get(channel, 0) + 1
    test.assertEqual(set(counts), set(expected))
    for channel, (minimum, maximum) in expected.items():
        test.assertGreaterEqual(counts[channel], minimum)
        test.assertLessEqual(counts[channel], maximum)


class HistoryMock(MockServer):
    def __init__(self, case="normal", count=250):
        super().__init__()
        self.case, self.count = case, count
        self.history_started = threading.Event()
        self.older_calls = 0
        self.newer_calls = 0
        self.summary_calls = 0
        self.api_handler = self.route
        self.channels = [
            {
                "id": OTHER,
                "name": "outro",
                "position": 3,
                "topic": "Outra conversa",
                "type": "text",
            },
            {"id": VOICE, "name": "voz", "position": 4, "topic": None, "type": "voice"},
            {"id": uuid(9), "name": "Categoria", "position": 1, "topic": None, "type": "category"},
            {
                "id": CHANNEL,
                "name": "geral",
                "position": 2,
                "topic": "Tópico remoto\x1b]52;c;INJECT\x07",
                "type": "text",
                "last_message": {"id": uuid(100 + count)},
                "last_read_message": None,
            },
        ]
        self.messages = [self.message(n) for n in range(1, count + 1)]
        self.channel_reads = []

    def find_message(self, channel_id, message_id):
        for message in self.messages + getattr(self, "sent_messages", []):
            if message["id"] == message_id and message["channel_id"] == channel_id:
                return message
        for channel in self.channels:
            last = channel.get("last_message") or {}
            if channel["id"] == channel_id and last.get("id") == message_id:
                return dict(last, created_at=last.get("created_at", STAMP))
        return None

    def read_targets(self):
        return self.channels

    def register_read(self, channel_id, message_id):
        # As the backend: listing never reads; PUT advances only forward.
        channel = next((c for c in self.read_targets() if c["id"] == channel_id), None)
        message = self.find_message(channel_id, message_id)
        if channel is None or message is None:
            return None
        previous = channel.get("last_read_message")
        stored = previous and self.find_message(channel_id, previous)
        old = (stored["created_at"], stored["id"]) if stored else ("", "")
        if old < (message["created_at"], message["id"]):
            channel["last_read_message"] = message["id"]
            channel["last_read_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        return message

    def message(self, n):
        return {
            "id": uuid(100 + n),
            "channel_id": CHANNEL,
            "author_id": uuid(20 + n % 3),
            "content": f"Mensagem {n:04d}",
            "created_at": STAMP,
            "edited_at": None,
            "reply_to": None,
            "attachments": [],
            "embeds": None,
            "reactions": None,
        }

    def route(self, method, path, body):
        url = urlsplit(path)
        if method == "PUT" and url.path.endswith("/read") and "/channels/" in url.path:
            channel = url.path.split("/")[-2]
            self.channel_reads.append((channel, body["message_id"]))
            return (204, None) if self.register_read(channel, body["message_id"]) else (404, None)
        if url.path.endswith("/voice/ice-servers") and method == "GET":
            # The contract allows an empty list; no STUN or TURN server is
            # reachable from the synthetic environment.
            return 200, {"ice_servers": []}
        if url.path.endswith("/channels") and method == "GET":
            if self.case == "channels-invalid":
                return 200, {"channels": [{"id": "../bad"}]}
            if self.case == "channels-empty":
                return 200, {"channels": []}
            for channel in self.channels:
                channel.setdefault("permissions", [])
            return 200, {"channels": self.channels}
        if url.path.endswith("/messages") and "/channels/" in url.path:
            self.history_started.set()
            channel = url.path.split("/")[-2]
            if channel == OTHER:
                messages = [
                    dict(self.message(1), channel_id=OTHER, content="Conteúdo do outro canal")
                ]
                messages += [m for m in self.sent_messages if m["channel_id"] == OTHER]
                return 200, {
                    "channel_id": OTHER,
                    "messages": list(reversed(messages)),
                    "has_more": False,
                }
            if channel not in (CHANNEL, VOICE):
                return 404, None
            source = (
                self.messages
                if channel == CHANNEL
                else [
                    dict(m, id=uuid(500 + n), channel_id=VOICE, content=f"Conversa de voz {n:04d}")
                    for n, m in enumerate(self.messages, 1)
                ]
            )
            if self.case == "slow":
                time.sleep(0.4)
            if self.case in ("forbidden", "removed", "limited", "failure"):
                return {"forbidden": 403, "removed": 404, "limited": 429, "failure": 500}[
                    self.case
                ], None
            query = parse_qs(url.query)
            assert query.get("order") in (["desc"], ["asc"])
            # Like the backend: 30 per page by default, 1 to 100 when requested.
            size = query.get("limit", ["30"])[0]
            if not size.isdigit() or not 1 <= int(size) <= 100:
                return 400, None
            size = int(size)
            cursor = query.get("last_id", [None])[0]
            if query.get("order") == ["asc"]:
                self.newer_calls += 1
                assert cursor and query["since"] == [STAMP]
                lower = int(cursor.split("-")[-1], 16) - (100 if channel == CHANNEL else 500)
                if self.case == "newer-error" and self.newer_calls == 1:
                    return 503, None
                if self.case == "forward-live" and self.newer_calls == 1:
                    self.messages.append(self.message(self.count + 1))
                if self.case == "overlap":
                    lower -= 1
                page = source[lower : lower + size]
                more = lower + size < len(source)
                if self.case == "empty-newer":
                    page, more = [], True
                if self.case == "repeat-newer":
                    page, more = source[max(0, lower - size) : lower], True
                return 200, {"channel_id": channel, "messages": page, "has_more": more}
            upper = self.count
            if cursor == "ffffffff-ffff-ffff-ffff-ffffffffffff":
                pass  # Every message shares STAMP: all were created up to since.
            elif cursor:
                self.older_calls += 1
                assert query["since"] == [STAMP]
                upper = int(cursor.split("-")[-1], 16) - (100 if channel == CHANNEL else 500) - 1
                if self.case == "overlap":
                    upper += 1
                if self.case == "older-error" and self.older_calls == 1:
                    return 503, None
            page = list(reversed(source[max(0, upper - size) : upper]))
            more = upper > size
            if not cursor and self.sent_messages:
                messages = source + [m for m in self.sent_messages if m["channel_id"] == channel]
                page = list(reversed(messages[-size:]))
                more = len(messages) > size
            if self.case == "empty-page" and cursor:
                page, more = [], True
            if self.case == "empty":
                page, more = [], False
            if self.case == "wrong-channel":
                page = [dict(page[0], channel_id=OTHER)]
            if self.case == "invalid-date":
                page = [dict(page[0], created_at="2026-02-30T00:00:00Z")]
            if self.case == "null-item":
                page = [None]
            if self.case == "too-many":
                page += [self.message(1)]
            if self.case == "repeat" and cursor:
                page, more = list(reversed(source[-size:])), True
            return 200, {"channel_id": channel, "messages": page, "has_more": more}
        if url.path.endswith("/users/user_summary_batch") and method == "POST":
            self.summary_calls += 1
            assert 1 <= len(body["ids"]) <= 1000
            assert len(body["ids"]) == len(set(body["ids"]))
            if self.case == "authors-error":
                return 503, None
            if self.case == "authors-401" and self.summary_calls == 1:
                return 401, None
            if self.case == "authors-twice":
                return 401, None
            return 200, [
                {
                    "id": id,
                    "username": self.username if id == uuid(1) else "Autor " + id[-1],
                    "nickname": "Apelido" if id == uuid(21) else None,
                }
                for id in body["ids"]
                if id != uuid(22)
            ]
        return None
