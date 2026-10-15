"""Native Konsole Sixel and Kitty graphics with synthetic HTTP fixtures.

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
import tempfile
import time
from tools.check_backend import private_write
from tools.check_r3 import ROOT
from tools.check_terminals import Emulator, wait_for

def screenshot(emulator, path):
    window = emulator.window if emulator.name == 'Konsole' else str(json.loads(emulator.kitty('ls'))[0]['platform_window_id'])
    subprocess.run(['import', '-window', window, str(path)], capture_output=True, check=True, timeout=10)

def colored_pixels(emulator, path, color):
    screenshot(emulator, path)
    pixels = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'],
                            capture_output=True, check=True, timeout=10).stdout
    channel = 0 if color == 'red' else 2
    return sum(pixels[i + channel] > 220 and all(pixels[i + other] < 40 for other in range(3) if other != channel)
               for i in range(0, len(pixels), 3))

def red_geometry(emulator, path):
    """Measure the solid-red fixture actually composed by the emulator."""
    screenshot(emulator, path)
    width = int.from_bytes(path.read_bytes()[16:20], 'big')
    pixels = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'],
                            capture_output=True, check=True, timeout=10).stdout
    points = [i // 3 for i in range(0, len(pixels), 3)
              if pixels[i] > 220 and pixels[i + 1] < 40 and pixels[i + 2] < 40]
    if not points: return 0, 0, 0, 0
    left = min(p % width for p in points); top = min(points) // width
    return left, top, max(p % width for p in points) - left + 1, max(points) // width - top + 1

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', type=Path, default=ROOT / 'build/papo-tui')
    p.add_argument('--output', type=Path, default=ROOT / 'build/media/terminals')
    p.add_argument('--terminal', action='append', choices=('Kitty', 'Konsole'))
    args = p.parse_args(); work = args.output.resolve(); work.mkdir(parents=True, exist_ok=True, mode=0o700)
    previous = sys.argv[:]; sys.path.insert(0, str(ROOT / 'tests')); sys.argv = [sys.argv[0], str(args.binary.resolve()), 'ffmpeg']
    try:
        from test_media_terminal import MediaTerminal
        from history_mock import uuid
    finally: sys.argv = previous
    checks = {}; large_sizes = {}; emulators = []; directories = []; phase = 'setup'
    names = args.terminal or ('Konsole', 'Kitty')
    MediaTerminal.setUpClass()
    try:
        helper = MediaTerminal()
        for name in names:
            phase = name + ' image startup'
            with helper.serve(MediaTerminal.images['png'], 'image/png') as server:
                original_image = server.messages[0]['attachments'][0]
                server.messages[0]['attachments'] = [dict(original_image, id=uuid(901 + i)) for i in range(2)]
                server.messages[0]['embeds'] = [{'title': 'Papo · Mídia', 'description': 'Imagens e embeds no terminal', 'color': '#ffb347',
                    'author': {'name': 'Autor do embed'}, 'fields': [{'name': 'Primeiro', 'value': 'Campo A', 'inline': True},
                    {'name': 'Segundo', 'value': 'Campo B', 'inline': True}], 'footer': {'text': 'Rodapé do Papo'}}]
                directory = tempfile.TemporaryDirectory(prefix='papo-graphics-'); directories.append(directory)
                emulator = Emulator(name, args.binary.resolve(), Path(directory.name), None); emulators.append(emulator)
                try:
                    phase = name + ' image login'
                    emulator.send(server.url + '\tjosé\tSynthetic!123\n')
                    wait_for(lambda: 'geral' in emulator.screen()); emulator.send('\x1bOB\x1bOB\n')
                    phase = name + ' image card and viewport'
                    wait_for(lambda: 'Rodapé do Papo' in emulator.screen()); emulator.send('\x1bOH')
                    wait_for(lambda: 'Mensagem 0001' in emulator.screen()); time.sleep(1)
                    calls = sum('/attachments/' in r[1] for r in server.requests)
                    checks[name.lower() + '_cards'] = True
                    checks[name.lower() + '_authenticated_image_download'] = calls == 2
                    checks[name.lower() + '_native_image_pixels'] = colored_pixels(emulator, work / (name.lower() + '-media.png'), 'red') > 100
                    checks[name.lower() + '_shared_image_in_two_positions'] = colored_pixels(emulator, work / (name.lower() + '-shared.png'), 'red') >= 300
                    phase = name + ' retained image pixels'
                    for key in ('draft', '\x1b', '\n', '\x1b'):
                        emulator.send(key); time.sleep(.12)
                        wait_for(lambda: colored_pixels(emulator, work / (name.lower() + '-retained.png'), 'red') > 100)
                    checks[name.lower() + '_focus_preserves_image_pixels'] = True
                    phase = name + ' image modal'
                    emulator.send('\x1b[18~'); wait_for(lambda: 'Escolha uma mídia' in emulator.screen()); emulator.send('\n'); time.sleep(.6)
                    checks[name.lower() + '_media_modal'] = 'Esc: voltar' in emulator.screen()
                    checks[name.lower() + '_native_modal_image_pixels'] = colored_pixels(emulator, work / (name.lower() + '-modal.png'), 'red') > 100
                    emulator.send('\x1b'); emulator.send('\n\x03\x1b'); emulator.resize_narrow(); time.sleep(.3); emulator.resize(False)
                    wait_for(lambda: 'Conversar em #' in emulator.screen() and 'Canais' in emulator.screen() and
                             emulator.state().get('rows', 0) >= 25 and emulator.state().get('columns', 0) >= 80)
                    emulator.send('Rascunho preservado'); wait_for(lambda: 'Rascunho preservado' in emulator.screen())
                    checks[name.lower() + '_resize_and_draft'] = True
                    phase = name + ' image channel switch'
                    emulator.click(8, 4); wait_for(lambda: 'Conteúdo do outro canal' in emulator.screen())
                    wait_for(lambda: colored_pixels(emulator, work / (name.lower() + '-channel.png'), 'red') == 0)
                    checks[name.lower() + '_channel_switch_removes_image'] = True
                finally:
                    emulator.close(); checks[name.lower() + '_restores_terminal'] = emulator.state().get('restored', False)
        large = work / 'large-portrait.png'
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'color=red:s=1000x1500',
                        '-frames:v', '1', '-threads', '1', '-y', str(large)], check=True, capture_output=True)
        for name in names:
            phase = name + ' large image startup'
            with helper.serve(large.read_bytes(), 'image/png') as server:
                directory = tempfile.TemporaryDirectory(prefix='papo-native-large-'); directories.append(directory)
                emulator = Emulator(name, args.binary.resolve(), Path(directory.name), None); emulators.append(emulator)
                try:
                    emulator.send(server.url + '\tjosé\tSynthetic!123\n')
                    wait_for(lambda: 'geral' in emulator.screen()); emulator.send('\x1bOB\x1bOB\n')
                    wait_for(lambda: 'Conversar em #' in emulator.screen())
                    if name == 'Kitty':
                        emulator.kitty('resize-os-window', '--width', '220', '--height', '70', '--unit', 'cells')
                    else:
                        subprocess.run(['xdotool', 'windowsize', emulator.window, '2200', '1600'], check=True, timeout=10)
                    wait_for(lambda: emulator.state().get('rows', 0) >= 50)
                    phase = name + ' large history image'
                    sizes = {}; positions = {}
                    def measured(stage):
                        x, y, w, h = red_geometry(emulator, work / (name.lower() + '-large-' + stage + '.png'))
                        sizes[stage] = w, h; positions[stage] = x, y
                        if stage == 'modal' and x <= positions['history'][0] + 30: return False
                        if stage == 'properties' and abs(x - positions['modal'][0]) < 15: return False
                        return w > 640 and h > 360 and abs(h / w - 1.5) < .02
                    wait_for(lambda: measured('history'))
                    emulator.send('\x1b[18~'); wait_for(lambda: '[Info]' in emulator.screen())
                    phase = name + ' large modal image'
                    wait_for(lambda: measured('modal'))
                    emulator.send('i'); wait_for(lambda: 'Propriedades' in emulator.screen())
                    wait_for(lambda: measured('properties'))
                    large_sizes[name.lower()] = sizes
                    checks[name.lower() + '_large_history_and_modal_pixels'] = all(
                        abs(h / w - 1.5) < .02 and w <= 1000 and h <= 1500 for w, h in sizes.values())
                    checks[name.lower() + '_resize_uses_original_download'] = sum('/attachments/' in r[1] for r in server.requests) == 1
                finally:
                    emulator.close()
                    checks[name.lower() + '_large_image_restores_terminal'] = emulator.state().get('restored', False)
        for name in names:
            phase = name + ' video startup'
            with helper.serve(MediaTerminal.video, 'video/mp4') as server:
                directory = tempfile.TemporaryDirectory(prefix='papo-graphics-'); directories.append(directory)
                emulator = Emulator(name, args.binary.resolve(), Path(directory.name), None); emulators.append(emulator)
                try:
                    phase = name + ' video login'
                    emulator.send(server.url + '\tjosé\tSynthetic!123\n')
                    wait_for(lambda: 'geral' in emulator.screen()); emulator.send('\x1bOB\x1bOB\n')
                    wait_for(lambda: 'Vídeo' in emulator.screen()); emulator.send('\x1b[18~')
                    phase = name + ' video pixels'
                    wait_for(lambda: 'Volume' in emulator.screen())
                    wait_for(lambda: colored_pixels(emulator, work / (name.lower() + '-video.png'), 'blue') > 100)
                    checks[name.lower() + '_native_video_pixels'] = True
                    emulator.send(' '); wait_for(lambda: 'Pausado' in emulator.screen())
                    checks[name.lower() + '_native_video_pause'] = True
                    emulator.resize_narrow(); time.sleep(.4); emulator.resize(False)
                    wait_for(lambda: 'Pausado' in emulator.screen())
                    wait_for(lambda: colored_pixels(emulator, work / (name.lower() + '-video-resized.png'), 'blue') > 100)
                    checks[name.lower() + '_native_video_resize_preserves_pause'] = True
                    emulator.send('\x1b'); wait_for(lambda: 'Conversar em #' in emulator.screen())
                    wait_for(lambda: colored_pixels(emulator, work / (name.lower() + '-video-closed.png'), 'blue') == 0)
                    checks[name.lower() + '_video_close_removes_pixels'] = True
                finally:
                    emulator.close(); checks[name.lower() + '_native_video_restores_terminal'] = emulator.state().get('restored', False)
        report = {'passed': all(checks.values()), 'kind': 'native-terminal-with-synthetic-http-fixtures',
            'checks': checks, 'large_image_sizes': large_sizes, 'screenshots': sorted(path.name for path in work.glob('*.png'))}
        report['binary_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
        private_write(work / 'summary.json', report); print(json.dumps(report, ensure_ascii=False))
        return 0 if report['passed'] else 1
    except Exception as error:
        report = {'passed': False, 'kind': 'native-terminal-with-synthetic-http-fixtures', 'checks': checks,
                  'phase': phase, 'failure_class': type(error).__name__,
                  'error': 'Native terminal validation failed; credentials omitted'}
        private_write(work / 'summary.json', report); print(json.dumps(report, ensure_ascii=False)); return 1
    finally:
        for emulator in emulators: emulator.close()
        for directory in directories: directory.cleanup()
        MediaTerminal.tearDownClass()
if __name__ == '__main__': raise SystemExit(main())
