"""Synthetic search/pin routes. Text matching here is fixture substring matching,
not an assertion about the backend's full-text tokenizer or ranking.
"""

import copy
import threading
import time
from urllib.parse import parse_qs, urlsplit

from r8_mock import R8Mock, DM
from history_mock import CHANNEL, OTHER, STAMP, uuid


class DiscoveryMock(R8Mock):
    def __init__(self, count=205):
        super().__init__(count=1)
        self.count = count
        self.messages = [self.message(i) for i in range(1, count + 1)]
        self.channels[3]["last_message"] = self.summary(self.messages[-1])
        self.pin_ids = {CHANNEL: [m["id"] for m in self.messages[:3]], DM: []}
        self.search_calls = []
        self.pin_calls = []
        self.search_status = self.pin_status = self.list_status = None
        self.search_delay = self.list_delay = self.pin_delay = 0
        self.pin_started = threading.Event()
        self.search_started = threading.Event()
        self.list_started = threading.Event()
        self.bad_search = self.bad_pins = False
        self.mutation_malformed = False
        self.search_once_401 = False
        self.repeat_cursor = False

    def route(self, method, path, body):
        parsed = urlsplit(path)
        if parsed.path.endswith("/search") and method == "POST":
            assert isinstance(body, dict)
            query = parse_qs(parsed.query)
            self.search_calls.append((copy.deepcopy(body), query))
            self.search_started.set()
            if self.search_delay:
                time.sleep(self.search_delay)
            if self.search_once_401 and len(self.search_calls) == 1:
                return 401, None
            if self.search_status:
                return self.search_status, None
            if self.bad_search:
                return 200, dict(results=[dict(id="bad")], has_more=False)
            rows = self.messages + self.dm_messages
            channel = body.get("channel_id")
            rows = (
                [m for m in rows if m["channel_id"] == channel]
                if channel
                else [m for m in rows if m["channel_id"] != DM]
            )
            if body.get("text"):
                rows = [
                    m
                    for m in rows
                    if body["text"].casefold() in (m.get("content") or "").casefold()
                ]
            if body.get("author"):
                rows = [m for m in rows if m["author_id"] == body["author"]]
            if body.get("mention"):
                token = "@mention(<@" + body["mention"] + ">)"
                rows = [m for m in rows if token in (m.get("content") or "")]
            if body.get("has"):
                rows = [
                    m
                    for m in rows
                    if "http://" in (m.get("content") or "")
                    or "https://" in (m.get("content") or "")
                ]
            if "contains_attachment" in body:
                rows = [
                    m for m in rows if bool(m.get("attachments")) == body["contains_attachment"]
                ]
            if body.get("date_start"):
                rows = [m for m in rows if m["created_at"][:10] >= body["date_start"]]
            if body.get("date_end"):
                rows = [m for m in rows if m["created_at"][:10] <= body["date_end"]]
            asc = body.get("order") == "asc"
            rows = sorted(rows, key=lambda m: (m["created_at"], m["id"]), reverse=not asc)
            if query and not self.repeat_cursor:
                assert set(query) == {"since", "last_id"}
                cursor = query["since"][0], query["last_id"][0]
                rows = [
                    m
                    for m in rows
                    if (
                        (m["created_at"], m["id"]) > cursor
                        if asc
                        else (m["created_at"], m["id"]) < cursor
                    )
                ]
            results = [
                dict(
                    type="message",
                    id=m["id"],
                    channel_id=m["channel_id"],
                    channel_name="geral" if m["channel_id"] == CHANNEL else "DM",
                    author_id=m["author_id"],
                    author_username="alice" if m["author_id"] else None,
                    content=m.get("content"),
                    created_at=m["created_at"],
                )
                for m in rows[:100]
            ]
            return 200, dict(results=results, has_more=len(rows) > 100)
        if parsed.path.endswith("/pinned") and method == "GET":
            channel = parsed.path.split("/")[-2]
            self.list_started.set()
            if self.list_delay:
                time.sleep(self.list_delay)
            if self.list_status:
                return self.list_status, None
            if self.bad_pins:
                return 200, dict(channel_id=OTHER, pinned=[])
            rows = self.messages + self.dm_messages
            return 200, dict(
                channel_id=channel,
                pinned=copy.deepcopy(
                    [
                        m
                        for pin in self.pin_ids.get(channel, [])
                        for m in rows
                        if m["id"] == pin and m["channel_id"] == channel
                    ]
                ),
            )
        if parsed.path.endswith("/pin") and method in ("POST", "DELETE"):
            channel, message = parsed.path.split("/")[-4], parsed.path.split("/")[-2]
            self.pin_calls.append((method, channel, message, body))
            assert body is None  # Both pin operations have an empty request body.
            self.pin_started.set()
            if self.pin_delay:
                time.sleep(self.pin_delay)
            if self.pin_status:
                return self.pin_status, None
            if not any(
                m["id"] == message and m["channel_id"] == channel
                for m in self.messages + self.dm_messages
            ):
                return 404, None
            pins = self.pin_ids.setdefault(channel, [])
            if method == "DELETE":
                if message not in pins:
                    return 404, None
                pins.remove(message)
                response = 204, None
            else:
                if message not in pins and len(pins) >= 100:
                    return 409, None
                existed = message in pins
                if not existed:
                    pins.append(message)
                response = (
                    (200 if existed else 201),
                    dict(
                        channel_id=channel, message_id=message, pinned_by=uuid(1), pinned_at=STAMP
                    ),
                )
            self.emit(dict(type="message_pin", message_id=message, is_pinned=method == "POST"))
            return (201, dict(message_id="invalid")) if self.mutation_malformed else response
        return super().route(method, path, body)
