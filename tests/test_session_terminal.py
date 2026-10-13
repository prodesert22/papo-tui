"""R4 controls, masking and session recovery through a real pseudoterminal."""
import time
import unittest
from mock_server import MockServer
from test_terminal import Terminal

class SessionTerminal(unittest.TestCase):
    def wait(self, terminal, text):
        deadline = time.monotonic() + 5
        while text not in '\n'.join(terminal.screen.display) and time.monotonic() < deadline: terminal.read(0.03)
        self.assertIn(text, '\n'.join(terminal.screen.display))

    def button(self, terminal, label):
        for y, line in enumerate(terminal.screen.display):
            if label in line:
                terminal.click(y, line.index(label) + 2)
                return
        self.fail('Button absent: ' + label)

    def test_registration_mouse_then_explicit_login_and_logout(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                self.button(terminal, '[Cadastro]')
                self.wait(terminal, '[Criar conta]')
                terminal.send(server.url + '\tjosé\tSynthetic!123\tSynthetic!123\n')
                self.wait(terminal, 'Conta criada')
                self.assertFalse(any(r[1].endswith('/auth/login') for r in server.requests))
                self.assertEqual(terminal.screen.cursor.y, 12)
                terminal.send('Synthetic!123\n')
                terminal.return_to_account()
                self.wait(terminal, 'Login realizado')
                self.wait(terminal, '[Renovar]')
                self.button(terminal, '[Renovar]')
                deadline = time.monotonic() + 3
                while server.refresh_count == 0 and time.monotonic() < deadline: terminal.read(0.03)
                self.assertEqual(server.refresh_count, 1)
                self.wait(terminal, '[Renovar]')
                self.button(terminal, '[ Sair da conta ]')
                self.wait(terminal, 'Você saiu da conta')
                self.assertNotIn(b'Synthetic!123', terminal.output)
            finally: self.assertTrue(terminal.close())

    def test_confirmation_and_password_policy_fail_before_any_http(self):
        cases = (
            ('A!1', 'A!1', '8 caracteres', 'Senha (password)'),
            ('synthetic!123', 'synthetic!123', 'maiúscula', 'Senha (password)'),
            ('Synthetic123', 'Synthetic123', 'caractere especial', 'Senha (password)'),
            ('Synthetic!123', '', 'Confirme sua senha', 'Confirmar senha'),
            ('Synthetic!123', 'Other!123', 'não coincidem', 'Confirmar senha'),
        )
        for password, confirmation, message, field in cases:
            with self.subTest(message=message), MockServer() as server:
                terminal = Terminal()
                try:
                    terminal.resize(22, 44)
                    terminal.send('\x0e' + server.url + '\tjosé\t' + password + '\t' + confirmation + '\n')
                    self.wait(terminal, message.split()[-1])
                    # Feedback wraps at word boundaries into two footer rows.
                    for word in message.split(): self.assertIn(word, '\n'.join(terminal.screen.display))
                    self.assertEqual(terminal.screen.cursor.y, self.position(terminal, field)[0] + 1)
                    self.assertEqual(server.requests, [])
                    self.assertNotIn(password.encode(), terminal.output)
                    if confirmation: self.assertNotIn(confirmation.encode(), terminal.output)
                    error_row, error_column = self.position(terminal, field)
                    hint_row, hint_column = self.position(terminal, 'Senha: 8+')
                    self.assertNotEqual(terminal.screen.buffer[error_row][error_column].fg,
                                        terminal.screen.buffer[hint_row][hint_column].fg)
                finally: self.assertTrue(terminal.close())

    def test_correct_confirmation_after_mismatch_and_resize_then_explicit_login(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                terminal.send('\x0e' + server.url + '\tjosé\tSynthetic!123\tOther!123\n')
                self.wait(terminal, 'não coincidem')
                self.assertEqual(server.requests, [])
                terminal.resize(22, 44)
                terminal.resize(30, 100)
                terminal.send('\x15Synthetic!123\n')
                self.wait(terminal, 'Conta criada')
                self.assertNotIn('Confirmar senha', '\n'.join(terminal.screen.display))
                registration = next(r for r in server.requests if r[1].endswith('/auth/register'))
                self.assertEqual(registration[3], {'username': 'josé', 'password': 'Synthetic!123'})
                self.assertFalse(any(r[1].endswith('/auth/login') for r in server.requests))
                terminal.send('Synthetic!123\n')
                terminal.return_to_account()
                self.wait(terminal, 'Login realizado')
                self.assertNotIn(b'Synthetic!123', terminal.output)
                self.assertNotIn(b'Other!123', terminal.output)
            finally: self.assertTrue(terminal.close())

    def test_confirm_input_click_preserves_mask_and_mode_switch_clears_passwords(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                terminal.send('\x0e' + server.url + '\tjosé\tSynthetic!123')
                row, column = self.position(terminal, 'Confirmar senha')
                terminal.click(row + 1, column)
                terminal.send('Synthetic!123')
                self.assertEqual(terminal.screen.cursor.y, row + 1)
                self.assertNotIn(b'Synthetic!123', terminal.output)
                self.button(terminal, '[Login]')
                self.assertNotIn('Confirmar senha', '\n'.join(terminal.screen.display))
                self.button(terminal, '[Cadastro]')
                row, column = self.position(terminal, 'Confirmar senha')
                self.assertNotIn('*', terminal.screen.display[row + 1])
                password_row, _ = self.position(terminal, 'Senha (password)')
                self.assertNotIn('*', terminal.screen.display[password_row + 1])
                self.assertEqual(server.requests, [])
            finally: self.assertTrue(terminal.close())

    def test_password_policy_focus_and_username_conflict(self):
        with MockServer('register-conflict') as server:
            terminal = Terminal()
            try:
                terminal.send('\x0e' + server.url + '\tjosé\tbad\n')
                self.wait(terminal, '8 caracteres')
                self.assertEqual(terminal.screen.cursor.y, self.position(terminal, 'Senha (password)')[0] + 1)
                self.assertEqual(server.requests, [])
                terminal.send('\x15Synthetic!123\tSynthetic!123\n')
                self.wait(terminal, 'já está em uso')
                self.assertEqual(terminal.screen.cursor.y, self.position(terminal, 'Usuário (username)')[0] + 1)
                self.assertNotIn(b'Synthetic!123', terminal.output)
            finally: self.assertTrue(terminal.close())

    def test_private_registration_missing_and_expired_password_focus(self):
        for scenario, expected in (('private', 'Conta criada'), ('private-expired', 'Autorização privada')):
            with self.subTest(scenario=scenario), MockServer(scenario) as server:
                terminal = Terminal()
                try:
                    terminal.send('\x0e' + server.url + '\tjosé\tSynthetic!123\tSynthetic!123\n')
                    self.wait(terminal, 'Preencha a senha do servidor')
                    self.assertEqual(terminal.screen.cursor.y, self.position(terminal, 'Senha do servidor')[0] + 1)
                    self.assertIn('Senha do servidor *', '\n'.join(terminal.screen.display))
                    terminal.send('Server!123\n')
                    self.wait(terminal, expected)
                    if scenario == 'private-expired': self.assertEqual(terminal.screen.cursor.y, self.position(terminal, 'Senha do servidor')[0] + 1)
                    self.assertNotIn(b'Server!123', terminal.output)
                    self.assertNotIn(b'Synthetic!123', terminal.output)
                    self.assertEqual(sum(r[1].endswith('/auth/register') for r in server.requests), 1)
                finally: self.assertTrue(terminal.close())

    def test_refresh_revocation_returns_editable_login(self):
        with MockServer('refresh-revoked') as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + '\tjosé\tSynthetic!123\n')
                terminal.return_to_account()
                self.wait(terminal, 'Login realizado')
                terminal.send('\x12')
                self.wait(terminal, 'Entre novamente')
                self.assertEqual(terminal.screen.cursor.y, 12)
                terminal.send('Synthetic!123\n')
                terminal.return_to_account()
                self.wait(terminal, 'Login realizado')
                self.assertEqual(server.refresh_count, 1)
            finally: self.assertTrue(terminal.close())

    def test_remote_logout_failure_still_cleans_local_session(self):
        with MockServer('logout-fail') as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + '\tjosé\tSynthetic!123\n')
                terminal.return_to_account()
                self.wait(terminal, 'Login realizado')
                terminal.send('\n')
                self.wait(terminal, 'não confirmada')
                self.assertIn('[ Entrar ]', '\n'.join(terminal.screen.display))
                self.assertEqual(terminal.screen.cursor.y, 12)
                terminal.send('Synthetic!123\n')
                terminal.return_to_account()
                self.wait(terminal, 'Login realizado')
                login = [r for r in server.requests if r[1].endswith('/auth/login')]
                self.assertEqual([r[2] for r in login], [None, None])
            finally: self.assertTrue(terminal.close())

    def test_resize_mode_switch_and_paste_do_not_send_credentials(self):
        with MockServer() as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + '\tjosé\tSynthetic!123')
                terminal.send('\x0e')
                self.wait(terminal, '[Criar conta]')
                terminal.resize(22, 44)
                self.assertIn('[Login]', '\n'.join(terminal.screen.display))
                terminal.resize(24, 80)
                terminal.send('\t\x1b[200~Synthetic!123\n\x0e\x12\x1b[201~')
                self.assertEqual(server.requests, [])
                self.assertIn('[Criar conta]', '\n'.join(terminal.screen.display))
                self.assertNotIn(b'Synthetic!123', terminal.output)
                self.button(terminal, '[Login]')
                self.wait(terminal, '[ Entrar ]')
                self.assertEqual(server.requests, [])
            finally: self.assertTrue(terminal.close())

    def position(self, terminal, label):
        for row, line in enumerate(terminal.screen.display):
            if label in line: return row, line.index(label)
        self.fail('Control absent: ' + label)

    def test_login_and_registration_arrows_follow_button_rows(self):
        up, down, left, right = '\x1bOA', '\x1bOB', '\x1bOD', '\x1bOC'
        for registering in (False, True):
            for rows, columns in ((24, 80), (22, 44)):
                with self.subTest(registering=registering, columns=columns):
                    terminal = Terminal()
                    try:
                        if registering: terminal.send('\x0e')
                        terminal.resize(rows, columns)
                        primary = '[Criar conta]' if registering else '[ Entrar ]'
                        secondary = '[Login]' if registering else '[Cadastro]'
                        terminal.send(down * (4 if registering else 3))
                        password_row, _ = self.position(terminal, 'Senha do servidor')
                        self.assertEqual(terminal.screen.cursor.y, password_row + 1)
                        terminal.send(down)
                        row, column = self.position(terminal, primary)
                        highlight = terminal.screen.buffer[row][column].bg
                        def selected(label):
                            y, x = self.position(terminal, label)
                            self.assertEqual(terminal.screen.buffer[y][x].bg, highlight)
                            self.assertTrue(terminal.screen.cursor.hidden)
                        selected(primary)
                        profile_row, _ = self.position(terminal, '[Selecionar perfil]')
                        self.assertLess(profile_row, row)
                        terminal.send(left * 2)
                        selected(primary)
                        terminal.send(right * 3)
                        selected(secondary)
                        terminal.send(down)
                        selected('[Selecionar perfil]')
                        terminal.send(down + left + right)
                        selected('[Selecionar perfil]')
                        terminal.send(up)
                        selected(primary)
                        terminal.send(up)
                        self.assertEqual(terminal.screen.cursor.y, password_row + 1)
                        terminal.send('\t')
                        selected(primary)
                        terminal.send('\t')
                        selected(secondary)
                        terminal.send('\t')
                        selected('[Selecionar perfil]')
                        terminal.send('\x1b[Z')
                        selected(secondary)
                        terminal.send('\x1b[Z')
                        selected(primary)
                        terminal.send(up * 6)
                        url_row, _ = self.position(terminal, 'URL do servidor')
                        self.assertEqual(terminal.screen.cursor.y, url_row + 1)
                    finally: self.assertTrue(terminal.close())

    def test_profile_button_at_top_right_opens_modal_by_mouse(self):
        terminal = Terminal()
        try:
            profile_row, _ = self.position(terminal, '[Selecionar perfil]')
            primary_row, _ = self.position(terminal, '[ Entrar ]')
            self.assertLess(profile_row, primary_row)
            self.assertGreater(self.position(terminal, '[Selecionar perfil]')[1], self.position(terminal, '[ Entrar ]')[1])
            self.button(terminal, '[Selecionar perfil]')
            self.wait(terminal, 'Selecione um perfil')
            terminal.send('\x1b')
            self.wait(terminal, 'URL do servidor')
        finally: self.assertTrue(terminal.close())

    def test_registration_button_has_focus_without_colors(self):
        terminal = Terminal(terminal_type='vt100')
        try:
            terminal.send('\t' * 5)
            line = terminal.screen.display[18]
            column = line.index('[Cadastro]')
            self.assertTrue(terminal.screen.buffer[18][column].reverse)
            terminal.send('\n')
            self.wait(terminal, '[Criar conta]')
            self.assertTrue(terminal.screen.buffer[18][12].reverse is False)
        finally: self.assertTrue(terminal.close())

    def test_slow_refresh_remains_cancellable(self):
        with MockServer('refresh-slow') as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + '\tjosé\tSynthetic!123\n')
                terminal.return_to_account()
                self.wait(terminal, 'Login realizado')
                terminal.send('\x12\x1b')
                self.wait(terminal, 'cancelada')
                self.assertIn('[ Entrar ]', '\n'.join(terminal.screen.display))
            finally: self.assertTrue(terminal.close())

if __name__ == '__main__': unittest.main()
