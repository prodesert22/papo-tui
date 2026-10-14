"""Native Konsole Sixel and Kitty fallback with synthetic HTTP fixtures.

Runs disposable emulator windows and stores screenshots separately from PTY
and actual-backend evidence. Does not touch existing terminal sessions.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
from tools.check_backend import private_write
from tools.check_r3 import ROOT
from tools.check_terminals import Emulator, wait_for

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, default=ROOT / 'build/papo-tui')
    p.add_argument('--output', type=Path, default=ROOT / 'build/media/terminals')
    args = p.parse_args(); work = args.output.resolve(); work.mkdir(parents=True, exist_ok=True, mode=0o700)
    previous = sys.argv[:]; sys.path.insert(0, str(ROOT / 'tests')); sys.argv = [sys.argv[0], str(args.binary.resolve()), 'ffmpeg']
    try:
        from test_media_terminal import MediaTerminal
        from history_mock import uuid
    finally: sys.argv = previous
    checks = {}; emulators = []
    MediaTerminal.setUpClass()
    try:
        helper = MediaTerminal()
        for name in ('Konsole', 'Kitty'):
            with helper.serve(MediaTerminal.images['png'], 'image/png') as server:
                server.messages[0]['embeds'] = [{'title': 'Papo · Sixel', 'description': 'Imagens e embeds no terminal', 'color': '#ffb347',
                    'author': {'name': 'Autor do embed'}, 'fields': [{'name': 'Primeiro', 'value': 'Campo A', 'inline': True},
                    {'name': 'Segundo', 'value': 'Campo B', 'inline': True}], 'footer': {'text': 'Rodapé do Papo'}}]
                emulator = Emulator(name, args.binary.resolve(), work, None); emulators.append(emulator)
                try:
                    emulator.send(server.url + '\tjosé\tSynthetic!123\n')
                    wait_for(lambda: 'geral' in emulator.screen()); emulator.send('\x1bOB\x1bOB\n')
                    wait_for(lambda: 'Rodapé do Papo' in emulator.screen()); emulator.send('\x1bOH')
                    wait_for(lambda: 'Fixture' in emulator.screen()); time.sleep(1)
                    calls = sum('/attachments/' in r[1] for r in server.requests)
                    checks[name.lower() + '_cards'] = True
                    checks[name.lower() + ('_sixel_download' if name == 'Konsole' else '_text_fallback')] = calls == 1 if name == 'Konsole' else calls == 0
                    window = emulator.window if name == 'Konsole' else subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(emulator.process.pid)], capture_output=True, text=True, check=True).stdout.splitlines()[0]
                    subprocess.run(['import', '-window', window, str(work / (name.lower() + '-media.png'))], capture_output=True, check=True, timeout=10)
                    emulator.send('\x1b[18~'); time.sleep(.6)
                    checks[name.lower() + '_media_modal'] = 'Esc: voltar' in emulator.screen()
                    subprocess.run(['import', '-window', window, str(work / (name.lower() + '-modal.png'))], capture_output=True, check=True, timeout=10)
                    emulator.send('\x1b'); emulator.resize_narrow(); time.sleep(.3); emulator.resize(False)
                    wait_for(lambda: 'Conversar em #' in emulator.screen())
                    emulator.send('Rascunho preservado'); wait_for(lambda: 'Rascunho preservado' in emulator.screen())
                    checks[name.lower() + '_resize_and_draft'] = True
                finally:
                    emulator.close(); checks[name.lower() + '_restores_terminal'] = emulator.state().get('restored', False)
        with helper.serve(MediaTerminal.video, 'video/mp4') as server:
            emulator = Emulator('Konsole', args.binary.resolve(), work, None); emulators.append(emulator)
            try:
                emulator.send(server.url + '\tjosé\tSynthetic!123\n')
                wait_for(lambda: 'geral' in emulator.screen()); emulator.send('\x1bOB\x1bOB\n')
                wait_for(lambda: 'Vídeo' in emulator.screen()); emulator.send('\x1b[18~')
                wait_for(lambda: 'Volume' in emulator.screen()); emulator.send(' ')
                wait_for(lambda: 'Pausado' in emulator.screen())
                checks['konsole_native_video_pause'] = True
                subprocess.run(['import', '-window', emulator.window, str(work / 'konsole-video.png')], capture_output=True, check=True, timeout=10)
                emulator.resize_narrow(); time.sleep(.4); emulator.resize(False)
                wait_for(lambda: 'Pausado' in emulator.screen())
                checks['konsole_native_video_resize_preserves_pause'] = True
                emulator.send('\x1b'); wait_for(lambda: 'Conversar em #' in emulator.screen())
            finally:
                emulator.close(); checks['konsole_native_video_restores_terminal'] = emulator.state().get('restored', False)
        report = {'passed': all(checks.values()), 'kind': 'native-terminal-with-synthetic-http-fixtures',
            'checks': checks, 'screenshots': sorted(path.name for path in work.glob('*.png'))}
        report['binary_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
        private_write(work / 'summary.json', report); print(json.dumps(report, ensure_ascii=False))
        return 0 if report['passed'] else 1
    except Exception:
        report = {'passed': False, 'kind': 'native-terminal-with-synthetic-http-fixtures', 'checks': checks,
                  'error': 'Native terminal validation failed; credentials omitted'}
        private_write(work / 'summary.json', report); print(json.dumps(report, ensure_ascii=False)); return 1
    finally:
        for emulator in emulators: emulator.close()
        MediaTerminal.tearDownClass()
if __name__ == '__main__': raise SystemExit(main())
