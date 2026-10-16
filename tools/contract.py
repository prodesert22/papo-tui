"""Use OpenAPI 3.0 validation, including nullable and local references."""

from functools import lru_cache
from pathlib import Path

from ruamel.yaml import YAML
from openapi_schema_validator import OAS30Validator, oas30_format_checker
from referencing import Registry, Resource
from referencing.jsonschema import DRAFT4

ROOT = Path(__file__).resolve().parents[1]
SPEC_URI = "urn:papo:openapi"


def error_location(error):
    item = {"path": "/" + "/".join(map(str, error.absolute_path)), "rule": error.validator}
    if error.validator == "required" and isinstance(error.instance, dict):
        item["missing"] = [name for name in error.validator_value if name not in error.instance]
    return item


def escape_pointer(part):
    return str(part).replace("~", "~0").replace("/", "~1")


class Contract:
    def __init__(self):
        loader = YAML(typ="safe", pure=True)
        loader.version = (1, 2)  # OpenAPI YAML: enum value "off" is not a boolean.
        self.spec = loader.load((ROOT / "openapi.yml").read_text())
        resource = Resource.from_contents(self.spec, default_specification=DRAFT4)
        self.registry = Registry().with_resource(SPEC_URI, resource)

    def resolve(self, node):
        while isinstance(node, dict) and "$ref" in node:
            reference = node["$ref"]
            if not reference.startswith("#/"):
                raise ValueError("Only local OpenAPI references are allowed")
            node = self.spec
            for part in reference[2:].split("/"):
                node = node[part.replace("~1", "/").replace("~0", "~")]
        return node

    @lru_cache(maxsize=128)
    def validator(self, pointer):
        return OAS30Validator(
            {"$ref": SPEC_URI + pointer},
            registry=self.registry,
            format_checker=oas30_format_checker,
        )

    def request_schema(self, path, method, media="application/json"):
        operation = self.spec["paths"][path][method.lower()]
        body = operation.get("requestBody")
        if not body:
            return None
        pointer = body.get("$ref") or f"#/paths/{escape_pointer(path)}/{method.lower()}/requestBody"
        body = self.resolve(body)
        if media not in body["content"]:
            raise ValueError("Request media type is not documented")
        return pointer + f"/content/{escape_pointer(media)}/schema"

    def response_schema(self, path, method, status, media="application/json"):
        operation = self.spec["paths"][path][method.lower()]
        response = operation["responses"].get(str(status))
        if response is None:
            raise ValueError("Response status is not documented")
        pointer = (
            response.get("$ref")
            or f"#/paths/{escape_pointer(path)}/{method.lower()}/responses/{status}"
        )
        response = self.resolve(response)
        if "content" not in response:
            return None
        if media not in response["content"]:
            raise ValueError("Response media type is not documented")
        if "schema" not in response["content"][media]:
            return None
        return pointer + f"/content/{escape_pointer(media)}/schema"

    def errors(self, pointer, payload):
        if pointer is None:
            return []
        # ValidationError messages can embed secret values. Return locations only.
        errors = [error_location(error) for error in self.validator(pointer).iter_errors(payload)]
        return list({(item["path"], item["rule"]): item for item in errors}.values())
