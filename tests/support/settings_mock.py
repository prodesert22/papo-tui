"""Synthetic account settings. No development-instance observations or credentials.

The behavior mirrors the backend source: GET /auth/whoami carries settings.{version,
config}; PUT /users/settings replaces the whole config, decoding it like Go's
encoding/json into fixed fields, so an omitted switch is stored as false and an empty or
unknown theme, fontSize or messageDensity is refused with 400. version is the schema
version (1), not a concurrency token: the last write wins."""

import copy

from password_mock import PasswordMock
from profile_mock import ME

DEFAULT = {
    "theme": "system",
    "notifications": {"enabled": True, "messagePreview": True, "sound": True, "mentions": True},
    "display": {
        "fontSize": "medium",
        "messageDensity": "normal",
        "showTimestamps": True,
        "showAvatars": True,
    },
}
ENUMS = {
    ("theme",): ("dark", "light", "system"),
    ("display", "fontSize"): ("small", "medium", "huge"),
    ("display", "messageDensity"): ("compact", "normal", "comfortable"),
}
SWITCHES = {
    "notifications": ("enabled", "messagePreview", "sound", "mentions"),
    "display": ("showTimestamps", "showAvatars"),
}


def stored(config):
    """What the backend keeps for config, or None when it answers 400."""
    if not isinstance(config, dict):
        return None
    result = {"theme": config.get("theme", "")}
    for section, keys in SWITCHES.items():
        given = config.get(section, {})
        if not isinstance(given, dict):
            return None
        result[section] = {}
        for key in keys:
            value = given.get(key, False)
            if not isinstance(value, bool):
                return None
            result[section][key] = value
    display = config.get("display", {})
    result["display"]["fontSize"] = display.get("fontSize", "")
    result["display"]["messageDensity"] = display.get("messageDensity", "")
    for path, allowed in ENUMS.items():
        value = result
        for key in path:
            value = value[key]
        if value not in allowed:
            return None
    return result


def settings(config=None, **changes):
    """A whoami "settings" object: DEFAULT with section updates applied."""
    value = copy.deepcopy(DEFAULT if config is None else config)
    for key, update in changes.items():
        if isinstance(update, dict):
            value[key].update(update)
        else:
            value[key] = update
    return {"version": 1, "config": value}


class SettingsMock(PasswordMock):
    """settings_status overrides the PUT reply; "drop" stores it and closes the
    connection without one. settings_reply replaces the 200 body."""

    def __init__(self, total=6):
        super().__init__(total)
        self.user_settings = settings()
        self.settings_calls = []
        self.settings_status = None
        self.settings_reply = None

    def route(self, method, path, body):
        if method == "PUT" and path.split("?")[0].endswith("/users/settings"):
            self.settings_calls.append(body)
            if self.settings_status and self.settings_status != "drop":
                return self.settings_status, {
                    "detail": "Configurações recusadas\x1b]52;c;x\x07 pelo teste"
                }
            config = stored((body or {}).get("config"))
            if config is None:
                return 400, {"title": "Parâmetro inválido", "detail": "campo ausente ou inválido"}
            self.user_settings = {"version": 1, "config": config}
            if self.settings_status == "drop":
                return "drop"
            return 200, self.settings_reply or {
                "user_id": ME,
                "version": 1,
                "config": config,
                "updated_at": "2026-11-09T12:00:00Z",
            }
        return super().route(method, path, body)
