"""Synthetic channel settings endpoint, sharing the normal HTTP/WS fixture."""

from urllib.parse import urlsplit
import threading
from r8_mock import R8Mock
from history_mock import uuid


class ChannelPreferencesMock(R8Mock):
    def __init__(self):
        super().__init__(count=0)
        self.dms = []
        self.setting_status = 200
        self.setting_hold = None
        self.setting_started = threading.Event()
        self.setting_calls = []
        self.setting_invalid = False
        for channel in self.channels:
            channel["notification_settings"] = "only_mentions"

    def route(self, method, path, body):
        endpoint = urlsplit(path).path
        if method == "POST" and endpoint.endswith("/settings"):
            parts = endpoint.split("/")
            assert parts[-5] == "channels" and parts[-3:] == ["user", uuid(1), "settings"]
            channel = next(c for c in self.channels if c["id"] == parts[-4])
            assert set(body) == {"notification_settings"}
            assert body["notification_settings"] in ("all", "only_mentions", "off")
            self.setting_calls.append((channel["id"], body["notification_settings"]))
            self.setting_started.set()
            if self.setting_hold:
                assert self.setting_hold.wait(8), "Settings fixture was not released"
            if self.setting_status != 200:
                return self.setting_status, None
            channel["notification_settings"] = body["notification_settings"]
            return 200, dict(
                channel_id=uuid(999) if self.setting_invalid else channel["id"],
                user_id=uuid(1),
                notification_settings=body["notification_settings"],
            )
        return super().route(method, path, body)
