"""Compare cold identical media and keyboard latency against a baseline binary.

Linux development tool. Uses synthetic HTTP fixtures and a PTY Kitty responder. Native emulator/backend
checks are separate. Reports only counters and timings, never cookies/passwords.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import select
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/papo-tui')
    parser.add_argument('--instrumentation', type=Path, default=ROOT / 'build/libmedia-metrics.so')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/performance/comparison.json')
    args = parser.parse_args()
    previous = sys.argv[:]
    sys.path.insert(0, str(ROOT / 'tests'))
    sys.argv = [sys.argv[0], str(args.binary), 'ffmpeg']
    try:
        from test_media_terminal import GraphicsStream, MediaTerminal
        import test_terminal
        from history_mock import HistoryMock, uuid
    finally: sys.argv = previous
    args.output.parent.mkdir(parents=True, exist_ok=True)
    MediaTerminal.setUpClass()
    report = {'kind': 'synthetic-http-and-pty-protocol-responder', 'samples': 40, 'http_delay_ms': 80}
    def wait(t, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while not predicate() and time.monotonic() < deadline: t.read(.005)
        if not predicate(): raise AssertionError('Controlled scenario did not finish')
    def run(binary, *, instrument=False):
        test_terminal.BINARY = str(binary.resolve())
        with tempfile.TemporaryDirectory(prefix='papo-performance-') as directory, HistoryMock(count=100) as server:
            metrics = Path(directory) / 'metrics.json'
            original = server.route
            lock = threading.Lock()
            active = peak = requests = 0
            def route(method, path, body):
                nonlocal active, peak, requests
                if '/attachments/' in path:
                    with lock: active += 1; peak = max(peak, active); requests += 1
                    time.sleep(.08)
                    with lock: active -= 1
                    return 200, MediaTerminal.images['png'], None, None, 'image/png'
                return original(method, path, body)
            server.api_handler = route
            server.messages[-1]['attachments'] = [dict(id=uuid(901 + i), mime_type='image/png',
                original_file_name='Synthetic image', size_bytes=len(MediaTerminal.images['png'])) for i in range(4)]
            environment = {'TMPDIR': directory}
            if instrument:
                environment.update(LD_PRELOAD=str(args.instrumentation.resolve()), PAPO_MEDIA_METRICS=str(metrics))
            t = test_terminal.Terminal(environment=environment)
            stream = GraphicsStream(t.screen); stream.feed(bytes(t.output)); t.stream = stream
            try:
                import re
                query = re.search(rb'\x1b_Gi=(\d+),s=1,v=1,a=q', t.output)
                assert query
                t.send('\x1b_Gi=' + query[1].decode() + ';OK\x1b\\\x1b[?62;22c\x1b[6;16;8t')
                t.resize(64, 80)
                t.send(server.url + '\tjosé\tSynthetic!123\n')
                wait(t, lambda: 'geral' in '\n'.join(t.screen.display))
                load_started = time.monotonic()
                os.write(t.master, b'\x1bOB\x1bOB\n')
                wait(t, lambda: 'Conversar em #' in '\n'.join(t.screen.display))
                loaded_at = None
                latency, loading_latency = [], []
                def observe_loaded():
                    nonlocal loaded_at
                    if loaded_at is None and len({(p[b'i'], p[b'p']) for p in stream.placements}) >= 4:
                        loaded_at = time.monotonic()
                # Publish keyboard input while the cold media is downloading
                # and processing. Avoid Terminal.send's fixed 100 ms wait.
                for n in range(40):
                    observe_loaded(); during_load = loaded_at is None
                    started = time.monotonic(); os.write(t.master, b'x')
                    wait(t, lambda: 'x' * (n + 1) in '\n'.join(t.screen.display), timeout=2)
                    sample = (time.monotonic() - started) * 1000
                    latency.append(sample)
                    if during_load: loading_latency.append(sample)
                    observe_loaded()
                wait(t, lambda: len({(p[b'i'], p[b'p']) for p in stream.placements}) >= 4)
                observe_loaded(); loaded = (loaded_at - load_started) * 1000
                status = Path(f'/proc/{t.process.pid}/status').read_text()
                rss = int(next(line.split()[1] for line in status.splitlines() if line.startswith('VmHWM:')))
                media_files = [p for p in Path(directory).rglob('*') if p.is_file()]
                result = {'media_temp_files': len(media_files), 'media_file_bytes': sum(p.stat().st_size for p in media_files), 'requests': requests, 'peak_downloads': peak, 'loaded_ms': round(loaded, 2),
                    'keyboard_p95_ms': round(sorted(latency)[math.ceil(.95 * len(latency)) - 1], 2),
                    'keyboard_max_ms': round(max(latency), 2), 'loading_keyboard_samples': len(loading_latency),
                    'loading_keyboard_p95_ms': round(sorted(loading_latency)[math.ceil(.95 * len(loading_latency)) - 1], 2), 'peak_rss_kib': rss,
                    'kitty_image_ids': len({h[b'i'] for h, _ in stream.kitty_frames}),
                    'kitty_positions': len({(p[b'i'], p[b'p']) for p in stream.placements})}
            finally: assert t.close()
            if instrument:
                result.update(json.loads(metrics.read_text()))
            result['binary_sha256'] = hashlib.sha256(binary.read_bytes()).hexdigest()
            return result
    try:
        report['before'] = run(args.baseline)
        report['after'] = run(args.binary)
        # Preloading FFmpeg for counters would defeat lazy loading in the RAM
        # and keyboard measurements. Collect only counters in a separate pass.
        for label, binary in (('before', args.baseline), ('after', args.binary)):
            counters = run(binary, instrument=True)
            for key in ('avcodec_open2_calls', 'graphic_encodes', 'graphic_payload_bytes'):
                report[label][key] = counters[key]
        report['instrumentation'] = 'separate pass; RSS and timings measured without LD_PRELOAD'
        report['passed'] = report['after']['keyboard_p95_ms'] < 100 and report['after']['loading_keyboard_p95_ms'] < 100 and report['after']['peak_downloads'] == 4
        args.output.write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report))
        return 0 if report['passed'] else 1
    finally: MediaTerminal.tearDownClass()

if __name__ == '__main__': raise SystemExit(main())
