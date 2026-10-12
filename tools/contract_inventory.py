"""Generate the MVP operation inventory directly from openapi.yml."""
import argparse
import hashlib
from pathlib import Path

from tools.contract import Contract, ROOT

OPERATIONS = {
    "/health": ["get"], "/server": ["get"],
    "/auth/register": ["post"], "/auth/login_server": ["post"],
    "/auth/login": ["post"], "/auth/whoami": ["get"],
    "/auth/refresh": ["post"], "/auth/logout": ["post"],
    "/auth/connected_devices": ["get"], "/auth/drop_connection": ["post"],
    "/channels": ["get", "post"], "/channels/{channel_id}": ["delete"],
    "/channels/{channel_id}/messages": ["get"], "/messages": ["post"],
    "/messages/{message_id}": ["put", "delete"], "/users": ["get"],
    "/users/profile_batch": ["post"], "/users/user_summary_batch": ["post"],
    "/dms": ["get", "post"], "/dms/{dm_id}": ["get", "delete"],
    "/users/{user_id}/notifications": ["get"],
    "/users/{user_id}/read_notification": ["put"],
    "/channels/{channel_id}/user/{user_id}/settings": ["post"], "/ws": ["get"],
}


def markdown(value):
    return str(value).replace("|", "\\|").replace("\n", " ").strip()


def schema_name(schema):
    if "$ref" in schema:
        return schema["$ref"].split("/")[-1]
    if schema.get("type") == "array":
        return "array de " + schema_name(schema.get("items", {}))
    return schema.get("type", "objeto")


def render():
    contract = Contract()
    digest = hashlib.sha256((ROOT / "openapi.yml").read_bytes()).hexdigest()
    lines = ["# Inventário do contrato do MVP", "",
             "Gerado por `python -m tools.contract_inventory`; editar a especificação ou o gerador, não esta tabela.", "",
             f"SHA-256 da especificação: `{digest}`.", "",
             "As operações de criação/remoção de canal e revogação são incluídas para preparar e verificar o ambiente de desenvolvimento. A inclusão aqui não significa que a TUI já implementa esses recursos.", ""]
    for path, methods in OPERATIONS.items():
        path_item = contract.spec["paths"][path]
        for method in methods:
            operation = path_item[method]
            public = operation.get("security", contract.spec.get("security")) == []
            lines.extend([f"## {method.upper()} `{path}`", "", markdown(operation["summary"]), "",
                          "Autenticação: " + ("público." if public else "cookie `Auth`."), ""])
            parameters = [contract.resolve(p) for p in path_item.get("parameters", []) + operation.get("parameters", [])]
            if parameters:
                lines.extend(["| Parâmetro | Local | Obrigatório | Tipo / valores |", "| --- | --- | --- | --- |"])
                for parameter in parameters:
                    schema = parameter["schema"]
                    detail = schema_name(schema)
                    if "enum" in schema:
                        detail += ": " + ", ".join(map(str, schema["enum"]))
                    if "default" in schema:
                        detail += "; padrão=" + str(schema["default"])
                    lines.append(f"| {parameter['name']} | {parameter['in']} | {'sim' if parameter.get('required') else 'não'} | {markdown(detail)} |")
                lines.append("")
            if "requestBody" in operation:
                body = contract.resolve(operation["requestBody"])
                for media, content in body["content"].items():
                    schema = content["schema"]
                    definition = contract.resolve(schema)
                    required = definition.get("required", [])
                    lines.append(f"Corpo: `{media}`, schema `{schema_name(schema)}`; campos obrigatórios: {', '.join(required) or 'nenhum especificado'}. "
                                 f"Corpo {'obrigatório' if body.get('required') else 'opcional'}.")
                    if definition.get("properties"):
                        lines.extend(["", "| Campo | Obrigatório | Tipo / restrições |", "| --- | --- | --- |"])
                        for name, property_schema in definition["properties"].items():
                            constraints = [schema_name(property_schema)]
                            resolved = contract.resolve(property_schema)
                            for key in ["format", "minLength", "maxLength", "minItems", "maxItems", "minimum", "maximum"]:
                                if key in resolved:
                                    constraints.append(f"{key}={resolved[key]}")
                            if "enum" in resolved:
                                constraints.append("enum=" + ", ".join(map(str, resolved["enum"])))
                            if resolved.get("nullable"):
                                constraints.append("nullable")
                            lines.append(f"| {name} | {'sim' if name in required else 'não'} | {markdown('; '.join(constraints))} |")
                lines.append("")
            lines.extend(["| Status | Forma da resposta | Significado |", "| --- | --- | --- |"])
            for status, response in operation["responses"].items():
                response = contract.resolve(response)
                forms = [media + (" / " + schema_name(content["schema"]) if "schema" in content else "")
                         for media, content in response.get("content", {}).items()]
                lines.append(f"| {status} | {markdown('; '.join(forms) or 'sem corpo documentado')} | {markdown(response['description'])} |")
            lines.append("")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--output", type=Path, default=ROOT / "build/contract/api-inventory.md")
    arguments = parser.parse_args()
    target = arguments.output
    content = render()
    if arguments.check:
        if not target.exists() or target.read_text() != content:
            raise SystemExit("Inventário desatualizado. Execute python -m tools.contract_inventory.")
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content)
    print("Inventário conferido." if arguments.check else "Inventário gerado.")


if __name__ == "__main__":
    main()
