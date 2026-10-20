from test_context import argument
import json
import os
import select
import subprocess
import time

PROBE = argument(0)


class Observer:
    def __init__(self, url):
        self.p = subprocess.Popen(
            [PROBE, url],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=dict(os.environ, LC_ALL="C.UTF-8", NO_PROXY="*"),
        )

    def command(self, value="snapshot"):
        self.p.stdin.write(value + "\n")
        self.p.stdin.flush()
        if not select.select([self.p.stdout], [], [], 5)[0]:
            raise AssertionError("Observer timeout")
        line = self.p.stdout.readline()
        if not line:
            raise AssertionError(self.p.stderr.read())
        return json.loads(line)

    def wait(self, predicate):
        until = time.monotonic() + 6
        while time.monotonic() < until:
            result = self.command()
            if predicate(result):
                return result
            time.sleep(0.02)
        raise AssertionError(result)

    def close(self):
        if self.p.poll() is None:
            self.p.stdin.write("quit\n")
            self.p.stdin.flush()
        self.p.wait(timeout=6)
        if self.p.returncode:
            raise AssertionError(self.p.stderr.read())
        self.p.stdin.close()
        self.p.stdout.close()
        self.p.stderr.close()
