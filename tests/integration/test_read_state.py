"""Restart/concurrent-process read receipts, entirely offline and synthetic."""

import concurrent.futures
import fcntl
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

PROBE = sys.argv.pop(1)


def uuid(n):
    return f"00000000-0000-4000-8000-{n:012x}"


def cursor(n, stamp="2026-10-15T12:00:00.123456Z"):
    return dict(id=uuid(n), created_at=stamp)


class ReadState(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="papo-read-state-")
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.path = self.base / "papo-tui" / "read-cursors.json"
        self.environment = dict(os.environ, XDG_STATE_HOME=str(self.base))
        self.url, self.user = "https://first.example.test/papo", uuid(1)

    def run_probe(self, command, url=None, user=None, data=None):
        result = subprocess.run(
            [PROBE, command, url or self.url, user or self.user],
            input=json.dumps(data) if data is not None else "",
            env=self.environment,
            text=True,
            capture_output=True,
            timeout=10,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def save(self, data, **kwargs):
        return self.run_probe("save", data=data, **kwargs)["ok"]

    def load(self, **kwargs):
        return self.run_probe("load", **kwargs)

    def test_process_restart_and_private_metadata_only(self):
        self.assertEqual(self.load(), {})
        messages = {uuid(10): cursor(103)}
        self.assertTrue(self.save(messages))
        self.assertEqual(self.load(), messages)
        self.assertEqual(self.path.stat().st_mode & 0o7777, 0o600)
        self.assertEqual(self.path.parent.stat().st_mode & 0o7777, 0o700)
        stored = json.loads(self.path.read_text())
        context = stored["contexts"][0]
        self.assertEqual(set(context), {"url", "user_id", "messages"})
        self.assertEqual(set(context["messages"][uuid(10)]), {"id", "created_at"})
        self.assertFalse(list(self.path.parent.glob("*.tmp")))

    def test_same_uuids_are_isolated_by_instance_and_account(self):
        self.assertTrue(self.save({uuid(10): cursor(103)}))
        other_url, other_user = "https://second.example.test/papo", uuid(2)
        self.assertEqual(self.load(url=other_url), {})
        self.assertEqual(self.load(user=other_user), {})
        self.assertTrue(self.save({uuid(10): cursor(104)}, url=other_url))
        self.assertTrue(self.save({uuid(10): cursor(105)}, user=other_user))
        self.assertEqual(self.load(), {uuid(10): cursor(103)})
        self.assertEqual(self.load(url=other_url), {uuid(10): cursor(104)})
        self.assertEqual(self.load(user=other_user), {uuid(10): cursor(105)})

    def test_stale_process_and_timestamps_cannot_regress_cursor(self):
        self.assertTrue(self.save({uuid(10): cursor(103), uuid(11): cursor(201)}))
        self.assertTrue(self.save({uuid(10): cursor(102), uuid(12): cursor(301)}))
        self.assertEqual(self.load()[uuid(10)], cursor(103))
        newer = cursor(1, "2026-10-15T08:00:00.123457-04:00")
        self.assertTrue(self.save({uuid(10): newer}))
        self.assertEqual(self.load()[uuid(10)], newer)
        self.assertEqual(set(self.load()), {uuid(10), uuid(11), uuid(12)})

    def test_nonblocking_lock_and_concurrent_processes_merge(self):
        self.assertTrue(self.save({uuid(10): cursor(103)}))
        with (self.path.parent / "read-cursors.lock").open("r+") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertFalse(self.save({uuid(11): cursor(201)}))

        def write(n):
            for _ in range(50):
                if self.save({uuid(100 + n): cursor(1000 + n)}):
                    return True
            return False

        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as workers:
            self.assertTrue(all(workers.map(write, range(12))))
        self.assertEqual(len(self.load()), 13)

    def test_corrupt_insecure_and_oversized_files_are_not_trusted_or_overwritten(self):
        self.assertTrue(self.save({uuid(10): cursor(103)}))
        valid = self.path.read_bytes()
        for invalid in (b"{", b'{"version":99,"contexts":[]}', b" " * (4 * 1024 * 1024 + 1)):
            self.path.write_bytes(invalid)
            self.assertEqual(self.load(), {})
            self.assertFalse(self.save({uuid(10): cursor(104)}))
            self.assertEqual(self.path.read_bytes(), invalid)
        self.path.write_bytes(valid)
        self.path.chmod(0o644)
        self.assertEqual(self.load(), {})
        self.assertFalse(self.save({uuid(10): cursor(104)}))
        self.path.chmod(0o600)
        target = self.base / "outside.json"
        target.write_bytes(valid)
        target.chmod(0o600)
        self.path.unlink()
        self.path.symlink_to(target)
        self.assertEqual(self.load(), {})
        self.assertFalse(self.save({uuid(10): cursor(104)}))
        self.assertEqual(target.read_bytes(), valid)

    def test_invalid_scope_or_cursor_and_bounded_contexts(self):
        for url in ("https://user:password@example.test", "https://first.example.test/papo/"):
            self.assertFalse(self.save({uuid(10): cursor(103)}, url=url))
        self.assertFalse(self.save({uuid(10): cursor(103)}, user="invalid"))
        self.assertFalse(self.save({"invalid": cursor(103)}))
        self.assertFalse(self.save({uuid(10): dict(cursor(103), content="Must never persist")}))
        self.assertFalse(self.save({uuid(10): cursor(103, "invalid")}))
        self.assertFalse(self.save({uuid(n): cursor(n) for n in range(1001)}))
        for n in range(66):
            self.assertTrue(self.save({uuid(10): cursor(103)}, user=uuid(n + 1)))
        stored = json.loads(self.path.read_text())
        self.assertEqual(len(stored["contexts"]), 64)
        self.assertEqual(self.load(user=uuid(1)), {})
        self.assertEqual(self.load(user=uuid(66)), {uuid(10): cursor(103)})


if __name__ == "__main__":
    unittest.main()
