"""Exercise R1 on dedicated Kitty/Konsole windows and an isolated tmux server.

Uses synthetic HTTP/WS fixtures, native emulator screen reads and an observer
on each real PTY. It never attaches to the user's existing terminal sessions.
Requires a graphical X11/XWayland session, Kitty, Konsole, qdbus6 and xdotool.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import time
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from mock_server import MockServer


def run(arguments, *, text=None, checked=True):
    return subprocess.run(arguments, input=text, capture_output=True, text=True,
                          check=checked, timeout=10).stdout


def wait_for(predicate, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except (OSError, ValueError, subprocess.SubprocessError):
            pass
        time.sleep(0.08)
    raise RuntimeError('Terminal validation did not reach the expected state')


class Emulator:
    def __init__(self, name, binary, directory, tmux):
        self.name, self.tmux = name, tmux
        self.directory = directory
        self.state_path = directory / (name.lower().replace('+', '-') + '-state.json')
        self.state_path.unlink(missing_ok=True)
        self.socket = None
        self.process = None
        self.configuration = None
        self.stage = 'startup'
        self.tmux_server = 'papo-r1-' + str(os.getpid())
        observer = [sys.executable, str(ROOT / 'tools/terminal_observer.py'),
                    '--report', str(self.state_path), str(binary)]
        env = dict(os.environ, LANG='C.UTF-8', LC_ALL='C.UTF-8')
        if name == 'Konsole':
            env['QT_QPA_PLATFORM'] = 'xcb'
            # A separate configuration enables native input only in this
            # disposable instance; the user's Konsole settings are untouched.
            self.configuration = tempfile.TemporaryDirectory(prefix='konsole-config-', dir=directory)
            env['XDG_CONFIG_HOME'] = self.configuration.name
            env['XDG_STATE_HOME'] = self.configuration.name
            env['XDG_CACHE_HOME'] = self.configuration.name
            (Path(self.configuration.name) / 'konsolerc').write_text(
                '[KonsoleWindow]\nEnableSecuritySensitiveDBusAPI=true\n')
            command = ['konsole', '--separate', '--builtin-profile', '--hold', '--hide-menubar',
                       '--hide-toolbars', '--hide-tabbar', '-p', 'LocalTabTitleFormat=Papo-R1-Konsole', '-e', *observer]
        else:
            socket_prefix = directory / ('kitty-' + str(os.getpid()) + ('-tmux' if '+' in name else ''))
            self.socket_prefix = socket_prefix
            if '+' in name:
                observer = [str(tmux), '-L', self.tmux_server, '-f', '/dev/null',
                            'new-session', '-s', 'r1', shlex.join(observer)]
            command = ['kitty', '--listen-on', 'unix:' + str(socket_prefix), '--hold',
                       '--override', 'allow_remote_control=yes', '--override', 'linux_display_server=x11',
                       '--override', 'remember_window_size=no', '--override', 'initial_window_width=80c',
                       '--override', 'initial_window_height=24c', '--title', 'Papo-R1-' + name, *observer]
        self.process = subprocess.Popen(command, env=env, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.DEVNULL, start_new_session=True)
        try:
            if name == 'Konsole':
                self.service = 'org.kde.konsole-' + str(self.process.pid)
                self.window = wait_for(lambda: run(['xdotool', 'search', '--onlyvisible', '--pid',
                                                   str(self.process.pid)]).strip().splitlines()[0])
                self.resize(False)
            else:
                self.socket = wait_for(lambda: next(iter(directory.glob(socket_prefix.name + '*')), None))
            wait_for(lambda: self.state().get('running'))
            wait_for(lambda: 'URL do servidor' in self.screen())
        except Exception:
            self.close()
            raise

    def state(self):
        return json.loads(self.state_path.read_text())

    def kitty(self, *arguments, text=None):
        return run(['kitty', '@', '--to', 'unix:' + str(self.socket), *arguments], text=text)

    def screen(self):
        if self.name == 'Konsole':
            return run(['qdbus6', self.service, '/Sessions/1', 'org.kde.konsole.Session.getAllDisplayedText'])
        return self.kitty('get-text')

    def send(self, text):
        if self.name != 'Konsole':
            self.kitty('send-text', '--stdin', text=text)
            return
        run(['qdbus6', self.service, '/Sessions/1', 'org.kde.konsole.Session.sendText', text])

    def resize(self, small):
        if self.name == 'Konsole':
            # Konsole publishes pixel increments rather than cell size hints.
            run(['xdotool', 'windowsize', self.window, '400' if small else '1100', '240' if small else '700'])
        else:
            self.kitty('resize-os-window', '--width', '40' if small else '100',
                       '--height', '16' if small else '30', '--unit', 'cells')

    def close(self):
        try:
            state = self.state()
            if state.get('running'):
                os.kill(state['pid'], signal.SIGTERM)
                wait_for(lambda: not self.state().get('running'), 5)
        except (OSError, ValueError, RuntimeError):
            pass
        if '+' in self.name:
            run([str(self.tmux), '-L', self.tmux_server, 'kill-server'], checked=False)
        if self.process:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if self.name != 'Konsole':
            for path in self.directory.glob(self.socket_prefix.name + '*'):
                path.unlink(missing_ok=True)
        if self.configuration:
            self.configuration.cleanup()


def exercise(emulator, server):
    checks = {}
    sizes = [emulator.state()]
    emulator.stage = 'unicode_input'
    value = 'ação界語'
    emulator.send('\t' + value)
    wait_for(lambda: value in emulator.screen())
    emulator.stage = 'small_resize'
    emulator.resize(True)
    wait_for(lambda: emulator.state()['rows'] < 22 or emulator.state()['columns'] < 44)
    wait_for(lambda: 'Amplie o terminal' in emulator.screen())
    sizes.append(emulator.state())
    emulator.stage = 'restore_resize'
    emulator.resize(False)
    wait_for(lambda: emulator.state()['rows'] >= 22 and emulator.state()['columns'] >= 44)
    screen = wait_for(lambda: emulator.screen() if value in emulator.screen() else None)
    sizes.append(emulator.state())
    checks['resize_preserves_fields'] = True
    if 'Usuário' not in screen or '[ Entrar ]' not in screen:
        raise RuntimeError('The form did not return after resize')
    emulator.stage = 'paste'
    emulator.send('\x1b[Z\x15' + server.url + '\t\x15' + server.username + '\t\x1b[200~Synthetic!123\n\x1b[201~')
    time.sleep(0.2)
    checks['paste_does_not_submit'] = not server.requests
    before_submit = emulator.screen()
    checks['password_hidden'] = 'Synthetic!123' not in before_submit and '*' * 13 in before_submit
    emulator.send('\n')
    emulator.stage = 'websocket'
    screen = wait_for(lambda: emulator.screen() if 'Tempo real conectado' in emulator.screen() else None)
    wait_for(lambda: ('pong', b'probe') in server.ws_controls)
    checks['wide_text'] = 'Comunidade 界語' in screen and 'Usuário' in screen
    emulator.resize(True)
    wait_for(lambda: 'Amplie o terminal' in emulator.screen())
    emulator.resize(False)
    screen = wait_for(lambda: emulator.screen() if 'Comunidade 界語' in emulator.screen() else None)
    checks['resize_preserves_unicode'] = True
    checks['authenticated_ws_and_fragmented_event'] = True
    checks['password_hidden'] = checks['password_hidden'] and 'Synthetic!123' not in screen
    if not all(checks.values()):
        raise RuntimeError('A terminal interaction check failed')
    (emulator.directory / (emulator.name.lower().replace('+', '-') + '-screen.txt')).write_text(screen)
    emulator.stage = 'exit'
    if emulator.name == 'Konsole':
        os.kill(emulator.state()['pid'], signal.SIGINT)
        checks['exit'] = 'SIGINT'
    else:
        emulator.send('\x03' if '+' in emulator.name else '\x11')
        checks['exit'] = 'Ctrl+C' if '+' in emulator.name else 'Ctrl+Q'
    wait_for(lambda: not emulator.state().get('running'))
    state = emulator.state()
    checks['terminal_restored'] = state.get('restored') is True and state.get('returncode') == 0
    return {'checks': checks, 'pty_sizes': [{'rows': s['rows'], 'columns': s['columns']} for s in sizes],
            'term': state['term'], 'passed': checks['terminal_restored']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/papo-tui')
    parser.add_argument('--tmux', type=Path, default=ROOT / 'build/r1-tools/usr/bin/tmux')
    parser.add_argument('--directory', type=Path, default=ROOT / 'build/r1/terminals')
    arguments = parser.parse_args()
    directory = arguments.directory.resolve()
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    report = {'observed_at': datetime.now(timezone.utc).isoformat(), 'source': 'synthetic_fixtures', 'terminals': {}}
    for name in ('Kitty', 'Konsole', 'Kitty+tmux'):
        emulator = None
        try:
            with MockServer('ws-fragments') as server:
                server.name = 'Comunidade 界語'
                emulator = Emulator(name, arguments.binary.resolve(), directory, arguments.tmux.resolve())
                report['terminals'][name] = exercise(emulator, server)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            report['terminals'][name] = {'passed': False, 'failure_kind': type(error).__name__}
            if emulator:
                report['terminals'][name]['failure_stage'] = emulator.stage
                try:
                    screen = emulator.screen().replace('Synthetic!123', '[redacted]').replace('Server!123', '[redacted]')
                    (directory / (name.lower().replace('+', '-') + '-failure.txt')).write_text(screen)
                except (OSError, subprocess.SubprocessError):
                    pass
        finally:
            if emulator:
                emulator.close()
        print(name + ': ' + ('passed' if report['terminals'][name]['passed'] else 'failed'))
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if all(item['passed'] for item in report['terminals'].values()) else 2


if __name__ == '__main__':
    raise SystemExit(main())
