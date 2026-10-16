"""MVP event shapes from /ws prose; not a claim of formal OpenAPI WS schemas."""

from openapi_schema_validator import OAS30Validator, oas30_format_checker

from tools.contract import SPEC_URI, error_location


def message_property(name):
    return {"$ref": SPEC_URI + "#/components/schemas/Message/properties/" + name}


def uuid():
    return {"type": "string", "format": "uuid"}


def shape(event_type, properties, required):
    return {
        "type": "object",
        "properties": {"type": {"enum": [event_type]}, **properties},
        "required": ["type", *required],
    }


def schemas():
    fields = {
        name: message_property(name)
        for name in [
            "id",
            "channel_id",
            "author_id",
            "content",
            "created_at",
            "edited_at",
            "reply_to",
            "attachments",
        ]
    }
    values = {
        "message": shape(
            "message", fields, ["id", "channel_id", "author_id", "content", "created_at"]
        ),
        "message_edit": shape("message_edit", fields, ["id", "channel_id", "content", "edited_at"]),
        "message_delete": shape("message_delete", fields, ["id", "channel_id"]),
        "channel_create": shape(
            "channel_create",
            {
                "channel_id": uuid(),
                "name": {"type": "string"},
                "channel_type": {"enum": ["text", "category", "voice"]},
                "position": {"type": "integer"},
                "topic": {"type": "string", "nullable": True},
            },
            ["channel_id", "name", "channel_type", "position"],
        ),
        "channel_update": shape(
            "channel_update",
            {
                "channel_id": uuid(),
                "name": {"type": "string"},
                "position": {"type": "integer"},
                "topic": {"type": "string", "nullable": True},
            },
            ["channel_id", "name", "position"],
        ),
        "channel_delete": shape("channel_delete", {"channel_id": uuid()}, ["channel_id"]),
        "dm_update": shape(
            "dm_update",
            {"dm": {"$ref": SPEC_URI + "#/components/schemas/DirectConversation"}},
            ["dm"],
        ),
        "new_notification": shape(
            "new_notification",
            {
                "id": uuid(),
                "user_id": uuid(),
                "message_id": uuid(),
                "message_content": {"type": "string"},
            },
            ["id", "user_id", "message_id", "message_content"],
        ),
        "typing": shape(
            "typing",
            {"channel_id": uuid(), "user_id": uuid(), "is_typing": {"type": "boolean"}},
            ["channel_id"],
        ),
        "presence_activity": shape("presence_activity", {}, []),
        "heartbeat": shape("heartbeat", {}, []),
        "heartbeat_ack": shape("heartbeat_ack", {}, []),
        "error": shape(
            "error", {"message": {"type": "string"}, "code": {"type": "string"}}, ["message"]
        ),
    }
    presence = {
        "user_id": uuid(),
        "status": {"enum": ["online", "offline", "away", "busy"]},
        "status_message": {"type": "string", "nullable": True},
        "typing": {"type": "string", "nullable": True},
        "nickname": {"type": "string", "nullable": True},
    }
    # These optional fields use omitempty in the inspected backend DTOs.
    # The prose lists fields but does not define a formal required array.
    values["presence_update"] = shape("presence_update", presence, ["user_id", "status"])
    member = {
        "type": "object",
        "properties": {**presence, "user_voice": {"type": "array", "items": uuid()}},
        "required": ["user_id", "status", "user_voice"],
    }
    values["presence_sync"] = shape(
        "presence_sync", {"members": {"type": "array", "items": member}}, ["members"]
    )
    return values


def validate_event(contract, payload):
    if not isinstance(payload, dict) or not isinstance(payload.get("type"), str):
        return False, [{"path": "/type", "rule": "event_type"}]
    schema = schemas().get(payload["type"])
    if schema is None:
        return False, []  # Unknown event types can be ignored by the client.
    validator = OAS30Validator(
        schema, registry=contract.registry, format_checker=oas30_format_checker
    )
    errors = [error_location(error) for error in validator.iter_errors(payload)]
    return True, errors
