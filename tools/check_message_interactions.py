"""Check the C client against an isolated development DM using synthetic accounts.

Credentials stay in an ignored, private file; reports contain no passwords,
cookies, message contents or raw process diagnostics. Only this run's messages
are removed. Requests use the existing probe's TLS policy and paced transport.
"""

import argparse
from pathlib import Path
import json
import os
import secrets
import subprocess
import tempfile
from tools.check_backend import Probe, ProbeError, private_write
from tools.contract import ROOT


def run(args, probe):
    credentials = Path(args.credentials)
    state = (
        json.loads(credentials.read_text())
        if credentials.exists()
        else {"base_url": probe.base, "accounts": []}
    )
    if state["base_url"] != probe.base:
        raise ProbeError("Test accounts belong to a different instance")
    sessions = []
    channel = None
    created = []
    try:
        with probe.session() as anonymous:
            server = probe.request(anonymous, "get", "/server", 200)
            if not server.get("public"):
                raise ProbeError("This isolated runner requires a public development instance")
            while len(state["accounts"]) < 2:
                account = {
                    "username": "tuimsg" + secrets.token_hex(4),
                    "password": "Msg!" + secrets.token_urlsafe(24),
                }
                result = probe.request(anonymous, "post", "/auth/register", 201, json=account)
                account["id"] = result["id"]
                state["accounts"].append(account)
                private_write(credentials, state)
        for account in state["accounts"]:
            session = probe.session()
            sessions.append(session)
            probe.login(session, account, False)
        direct = probe.request(
            sessions[0], "post", "/dms", (200, 201), json={"user_id": state["accounts"][1]["id"]}
        )
        channel = direct["id"]
        original = probe.request(
            sessions[0],
            "post",
            "/messages",
            201,
            files={
                "channel_id": (None, channel),
                "content": (None, "Papo TUI isolated synthetic original"),
            },
        )
        created.append(original["id"])
        endpoint = f"/channels/{channel}/messages/{original['id']}/reactions"
        logical = "/channels/{channel_id}/messages/{message_id}/reactions"
        probe.request(sessions[1], "post", endpoint, 201, spec_path=logical, json={"unicode": "👍"})
        probe.request(sessions[1], "post", endpoint, 200, spec_path=logical, json={"unicode": "👍"})
        probe.request(sessions[1], "post", endpoint, 400, spec_path=logical, json={})
        probe.report["checks"]["reaction_idempotence_and_invalid_request"] = True
        with tempfile.TemporaryDirectory(
            prefix="papo-live-poster-", dir=ROOT / "build"
        ) as temporary:
            video = Path(temporary) / "synthetic.mp4"
            subprocess.run(
                [
                    args.ffmpeg,
                    "-v",
                    "error",
                    "-f",
                    "lavfi",
                    "-i",
                    "color=blue:s=32x24:r=10",
                    "-f",
                    "lavfi",
                    "-i",
                    "sine=frequency=440:sample_rate=44100",
                    "-t",
                    "0.5",
                    "-c:v",
                    "mpeg4",
                    "-c:a",
                    "aac",
                    "-threads",
                    "1",
                    "-y",
                    str(video),
                ],
                check=True,
                capture_output=True,
            )
            with video.open("rb") as attachment:
                message = probe.request(
                    sessions[0],
                    "post",
                    "/messages",
                    201,
                    data={"channel_id": channel, "content": "Synthetic video attachment"},
                    files={"attachments": ("synthetic.mp4", attachment, "video/mp4")},
                )
            created.append(message["id"])
            media_id = message["attachments"][0]["id"]
            env = dict(
                os.environ,
                PAPO_PROBE_USERNAME=state["accounts"][0]["username"],
                PAPO_PROBE_PASSWORD=state["accounts"][0]["password"],
                XDG_CONFIG_HOME=str(Path(temporary) / "config"),
                XDG_STATE_HOME=str(Path(temporary) / "state"),
                DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/papo-interaction-probe",
            )
            if args.ca_bundle:
                env["CURL_CA_BUNDLE"] = args.ca_bundle
            result = subprocess.run(
                [args.probe, probe.base, channel, original["id"], media_id],
                env=env,
                capture_output=True,
                text=True,
                encoding="utf-8",
                timeout=110,
            )
            try:
                checks = json.loads(result.stdout)
            except (ValueError, TypeError):
                raise ProbeError("C interaction probe produced no valid sanitized result") from None
            if checks.get("reply_id"):
                created.append(checks.pop("reply_id"))
            probe.report["checks"]["c_client"] = checks
            if result.returncode or not all(checks.values()):
                raise ProbeError("C interaction probe reported an incomplete interaction")
        snapshot = probe.request(
            sessions[0],
            "get",
            f"/channels/{channel}/messages",
            200,
            spec_path="/channels/{channel_id}/messages",
            params={"order": "desc"},
        )
        target = next(m for m in snapshot["messages"] if m["id"] == original["id"])
        probe.report["checks"]["remaining_peer_reaction"] = (
            target["reactions"][0]["count"] == 1 and not target["user_reactions"]
        )
    finally:
        for message in reversed(created):
            if sessions:
                try:
                    probe.request(
                        sessions[0],
                        "delete",
                        f"/messages/{message}",
                        204,
                        spec_path="/messages/{message_id}",
                    )
                except ProbeError:
                    probe.report["pending"].append("Could not remove an isolated synthetic message")
        for session in sessions:
            if channel:
                try:
                    probe.request(
                        session, "delete", f"/dms/{channel}", 204, spec_path="/dms/{dm_id}"
                    )
                except ProbeError:
                    probe.report["pending"].append("Could not close an isolated test DM")
            try:
                probe.request(session, "post", "/auth/logout", 204)
            except ProbeError:
                probe.report["pending"].append("Could not confirm test session logout")
            session.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True)
    parser.add_argument("--probe", default=str(ROOT / "build/interactions-probe"))
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument(
        "--credentials", default=str(ROOT / "build/integration/message-interactions-accounts.json")
    )
    parser.add_argument(
        "--report", default=str(ROOT / "build/integration/message-interactions.json")
    )
    parser.add_argument("--ca-bundle")
    args = parser.parse_args()
    probe = Probe(args.url, request_interval=0.3, ca_bundle=args.ca_bundle)
    try:
        run(args, probe)
    except (ProbeError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        probe.report["pending"].append(
            str(error)
            if isinstance(error, ProbeError)
            else "Local setup or development fixture failed"
        )
    private_write(Path(args.report), probe.report)
    print(
        json.dumps(
            {
                "checks": probe.report["checks"],
                "pending": probe.report["pending"],
                "contract_differences": len(probe.report["drift"]),
            }
        )
    )
    return 2 if probe.report["pending"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
