"""R8 in dedicated Kitty, Konsole and tmux windows, using synthetic fixtures.

The shared observer isolates the application's desktop bus and configuration;
this does not send notifications to the personal desktop daemon.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys
from tools.check_terminals import Emulator, ROOT, wait_for

sys.path.insert(0, str(ROOT / 'tests'))
from r8_mock import R8Mock, uuid


def exercise(terminal, server):
    checks = {}
    def see(text):
        return wait_for(lambda: text in terminal.screen())

    terminal.resize(False)
    terminal.stage = 'login'
    terminal.send(server.url + '\tjosé\tSynthetic!123\n')
    see('[Canais]'); see('[DMs]'); see('Categoria'); see('Tempo real conectado')
    terminal.stage = 'keyboard_buttons'
    terminal.send('\t\x1bOC\n'); see('Bia ·')
    checks['adjacent_buttons_keyboard'] = True
    checks['no_history_prefetch'] = not any('/messages' in r[1] for r in server.requests)
    terminal.stage = 'mouse_buttons'
    terminal.click(5, 4); see('Categoria')
    terminal.click(5, 13); see('Bia ·')
    checks['adjacent_buttons_mouse'] = True
    terminal.send('\x1bOB\n'); see('Mensagem direta')
    terminal.send('rascunho DM\x1b'); terminal.click(5, 4); see('Categoria')
    checks['draft_on_list_switch'] = 'rascunho DM' in terminal.screen()
    terminal.click(5, 13); terminal.send('\x1bOB\x04'); see('Nenhuma DM')
    terminal.stage = 'recipient_picker'
    terminal.send('\x1b[21~'); see('Nova DM'); see('Bia (@bob)')
    terminal.send('bia\n'); see('Mensagem direta'); see('rascunho DM')
    checks['close_discover_and_reopen'] = server.opens == 1
    terminal.stage = 'notifications'
    terminal.send('\x1b[20~'); see('Notificações'); see('Aviso 0003')
    checks['listing_does_not_read'] = not server.marked
    terminal.send('\n'); see('Detalhes da notificação')
    wait_for(lambda: bool(server.marked))
    checks['only_viewed_notification_read'] = server.marked == [[uuid(4003)]]
    terminal.send('\x1b\x1b\x1bOP'); see('Ajuda'); terminal.send('\x1b')
    checks['keyboard_help'] = True
    terminal.stage = 'narrow_resize'
    terminal.resize_narrow()
    wait_for(lambda: terminal.state()['columns'] < 80)
    # The modal remains reachable regardless of which narrow panel is visible.
    terminal.send('\x1b[20~'); see('Notificações')
    checks['narrow_center'] = True
    terminal.send('\x1b'); terminal.resize(False); see('rascunho DM')
    terminal.stage = 'disconnect_footer'
    server.disconnect(); see('Desconectado')
    screen = terminal.screen()
    (terminal.directory / (terminal.name.lower().replace('+', '-') + '-disconnect.txt')).write_text(screen)
    checks['disconnect_visible'] = True
    terminal.send('\x12'); see('Tempo real conectado')
    checks['reconnect'] = True
    terminal.stage = 'exit'
    terminal.send('\x11')
    state = wait_for(lambda: terminal.state() if not terminal.state().get('running') else None)
    checks['terminal_restored'] = state.get('restored') is True and state.get('returncode') == 0
    return dict(checks=checks, term=state['term'], passed=all(checks.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/papo-tui')
    parser.add_argument('--tmux', type=Path, required=True)
    parser.add_argument('--directory', type=Path, default=ROOT / 'build/r8/terminals')
    args = parser.parse_args()
    directory = args.directory.resolve(); directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    report = dict(observed_at=datetime.now(timezone.utc).isoformat(), source='synthetic_fixtures', terminals={})
    for name in ('Kitty', 'Konsole', 'Kitty+tmux'):
        terminal = None
        try:
            with R8Mock(count=3) as server:
                terminal = Emulator(name, args.binary.resolve(), directory, args.tmux.resolve())
                report['terminals'][name] = exercise(terminal, server)
        except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
            result = dict(passed=False, failure_kind=type(error).__name__)
            if terminal:
                result['failure_stage'] = terminal.stage
                try:
                    screen = terminal.screen().replace('Synthetic!123', '[redacted]')
                    (directory / (name.lower().replace('+', '-') + '-failure.txt')).write_text(screen)
                except (OSError, subprocess.SubprocessError):
                    pass
            report['terminals'][name] = result
        finally:
            if terminal: terminal.close()
        print(name + ': ' + ('passed' if report['terminals'][name]['passed'] else 'failed'))
    (directory / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if all(item['passed'] for item in report['terminals'].values()) else 2


if __name__ == '__main__':
    raise SystemExit(main())
