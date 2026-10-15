"""Mention picker and canonical wire text through the real TUI, synthetic peers."""
import json
import re
import subprocess
import time
import unittest
from urllib.parse import urlsplit, parse_qs
from history_mock import CHANNEL, OTHER, STAMP, uuid, assert_history_reads
from realtime_mock import RealtimeMock
from test_terminal import Terminal, BINARY

class MentionServer(RealtimeMock):
    def __init__(self, count=205):
        super().__init__(count=0)
        self.users = [dict(id=uuid(1000+i), username=('alice' if i==0 else 'bob' if i==1 else f'person{i:03}'),
                           nickname='Álice' if i==0 else None, created_at=STAMP) for i in range(count)]
        self.user_reads = 0
        self.reject_users = False
    def route(self, method, path, body):
        url = urlsplit(path)
        if method=='GET' and url.path.endswith('/users'):
            self.user_reads += 1
            if self.reject_users: return 429, None
            cursor = parse_qs(url.query).get('last_id', [None])[0]
            start = next((i+1 for i,u in enumerate(self.users) if u['id']==cursor),0)
            return 200, {'users': self.users[start:start+100], 'has_more':start+100<len(self.users)}
        if method=='POST' and url.path.endswith('/users/user_summary_batch'):
            found=[]
            for id in body['ids']:
                u=next((u for u in self.users if u['id']==id),None)
                found.append(u or dict(id=id,username=self.username if id==uuid(1) else 'Autor',nickname=None))
            return 200, found
        return super().route(method,path,body)

class Mentions(unittest.TestCase):
    def wait(self,t,text):
        deadline=time.monotonic()+8
        while text not in '\n'.join(t.screen.display) and time.monotonic()<deadline:t.read(.03)
        self.assertIn(text,'\n'.join(t.screen.display))
    def open(self,s):
        t=Terminal();t.send(s.url+'\tjosé\tSynthetic!123\n');self.wait(t,'geral')
        t.send('\x1bOB\x1bOB\n');self.wait(t,'Nenhuma mensagem neste canal')
        return t
    def test_tab_filters_and_sends_uuid_not_visible_name(self):
        with MentionServer() as s:
            t=self.open(s)
            try:
                t.send('Oi @ali');self.wait(t,'Menções');self.wait(t,'@alice')
                self.assertFalse(s.sent_messages)
                t.send('\t');self.wait(t,'@Álice')
                self.assertNotIn('Menções','\n'.join(t.screen.display))
                t.send('fim\n');self.wait(t,'Mensagem confirmada')
                self.assertEqual(s.sent_messages[0]['content'],f'Oi @mention(<@{uuid(1000)}>) fim')
                self.assertEqual(s.sent_messages[0]['channel_id'],CHANNEL)
                self.assertEqual(sum('/messages' in r[1] for r in s.requests if r[0]=='GET'),1)
            finally:self.assertTrue(t.close())

    def test_search_ignores_nickname_separators_and_keeps_the_original_name(self):
        nicknames = ['EA NA', 'EA-NA', 'EA -* NA', 'EA_.*-NA', 'EA ♥ NA', 'EA\u00a0—NA']
        with MentionServer(len(nicknames) + 1) as s:
            for n, nickname in enumerate(nicknames):
                s.users[n].update(username=f'candidate{n}', nickname=nickname)
            s.users[-1].update(username='other', nickname='EA NB')
            t = self.open(s)
            try:
                t.send('@ea'); self.wait(t, '@candidate0')
                t.send('na'); self.wait(t, '@candidate5')
                for n in range(len(nicknames)):
                    self.assertIn(f'@candidate{n}', '\n'.join(t.screen.display))
                self.assertNotIn('@other', '\n'.join(t.screen.display))
                self.assertIn('EA -* NA', '\n'.join(t.screen.display))
                t.send('\x15@ea-na'); self.wait(t, '@candidate5')
                t.send('\t'); self.wait(t, '@EA NA')
                t.send('\n'); self.wait(t, 'Mensagem confirmada')
                self.assertEqual(s.sent_messages[0]['content'], f'@mention(<@{uuid(1000)}>) ')
            finally: self.assertTrue(t.close())

    def test_popup_keeps_the_only_visible_cursor_in_the_editor(self):
        with MentionServer(2) as s:
            t = self.open(s)
            def editor_cursor(query):
                row = next(i for i, value in enumerate(t.screen.display) if '> Olá ' in value)
                column = t.screen.display[row].index(query) + len(query)
                self.assertFalse(t.screen.cursor.hidden)
                self.assertEqual((t.screen.cursor.y, t.screen.cursor.x), (row, column))
            try:
                t.send('Olá @'); self.wait(t, '@bob'); editor_cursor('@')
                t.send('\x1bOB'); editor_cursor('@')
                t.send('ali'); self.wait(t, '@alice'); editor_cursor('@ali')
                t.resize(14, 44); self.wait(t, '@alice'); editor_cursor('@ali')
                t.send('\x1b'); self.assertNotIn('Menções', '\n'.join(t.screen.display))
                editor_cursor('@ali')
            finally: self.assertTrue(t.close())

    def test_recent_channel_authors_rank_first_and_live_updates_keep_selection(self):
        with MentionServer(4) as s:
            t = self.open(s)
            def candidates():
                row = next(i for i, value in enumerate(t.screen.display) if 'Menções' in value)
                return [re.search(r'@(alice|bob|person[0-9]+)', t.screen.display[row+n]).group(1)
                        for n in range(1, 5)]
            try:
                messages = [dict(s.message(n), author_id=uuid(1000 + author), content=content,
                                 created_at=f'2026-10-12T12:00:00.{fraction}Z')
                            for n, author, content, fraction in (
                                (1, 0, 'Alice anterior', '123455'),
                                (2, 1, 'Bob recente', '123456'),
                                (3, 0, 'Alice atrasada', '123454'))]
                for message in messages: s.emit(dict(message, type='message'))
                self.wait(t, 'Bob recente')
                t.send('@'); self.wait(t, '@person003')
                self.assertEqual(candidates(), ['bob', 'alice', 'person002', 'person003'])
                t.send('\x1bOB')  # select Alice before a new writer arrives
                newest = dict(s.message(4), author_id=uuid(1003), content='Nova pessoa recente',
                              created_at='2026-10-12T12:00:00.123457Z')
                s.emit(dict(newest, type='message')); t.read(.3)
                self.assertEqual(candidates(), ['person003', 'bob', 'alice', 'person002'])
                t.send('\t\n'); self.wait(t, 'Mensagem confirmada')
                self.assertEqual(s.sent_messages[0]['content'], f'@mention(<@{uuid(1000)}>) ')
                assert_history_reads(self, s, {CHANNEL: (1, 5)})
            finally: self.assertTrue(t.close())

    def test_deleted_latest_message_and_channel_switch_change_recent_authors(self):
        with MentionServer(2) as s:
            t = self.open(s)
            def first_candidate():
                row = next(i for i, value in enumerate(t.screen.display) if 'Menções' in value)
                return t.screen.display[row+1]
            try:
                alice = dict(s.message(1), author_id=uuid(1000), content='Alice no geral')
                bob = dict(s.message(2), author_id=uuid(1001), content='Bob no geral')
                for message in (alice, bob): s.emit(dict(message, type='message'))
                self.wait(t, 'Bob no geral'); t.send('@'); self.wait(t, '@bob')
                self.assertIn('@bob', first_candidate())
                s.emit(dict(type='message_delete', id=bob['id'], channel_id=CHANNEL)); t.read(.3)
                self.assertIn('@alice', first_candidate())
                route = s.route
                other = dict(bob, channel_id=OTHER, content='Bob no outro canal')
                def other_channel(method, path, body):
                    if method=='GET' and urlsplit(path).path.endswith(f'/channels/{OTHER}/messages'):
                        return 200, dict(channel_id=OTHER, messages=[other], has_more=False)
                    return route(method, path, body)
                s.api_handler = other_channel
                t.click(8, 4); self.wait(t, 'Bob no outro canal')
                t.send('@'); self.wait(t, '@alice')
                self.assertIn('@bob', first_candidate())
                assert_history_reads(self, s, {CHANNEL: (1, 3), OTHER: (1, 1)})
                self.assertEqual(s.user_reads, 1)
            finally: self.assertTrue(t.close())
    def test_enter_selects_then_atomic_delete_and_multiple_mentions(self):
        with MentionServer(2) as s:
            t=self.open(s)
            try:
                t.send('@');self.wait(t,'@bob');t.send('\x1bOB\n');t.read(.1)
                self.assertFalse(s.sent_messages)
                self.assertIn('@bob','\n'.join(t.screen.display))
                t.send('\x7f\x7f');t.send('@ali');self.wait(t,'@alice');t.send('\t@bob')
                self.wait(t,'Menções');t.send('\t\n');self.wait(t,'Mensagem confirmada')
                self.assertEqual(s.sent_messages[0]['content'],f'@mention(<@{uuid(1000)}>) @mention(<@{uuid(1001)}>) ')
            finally:self.assertTrue(t.close())
    def test_pagination_mouse_scroll_and_click(self):
        with MentionServer() as s:
            t=self.open(s)
            try:
                t.send('@');self.wait(t,'Tab / Enter')
                self.assertEqual(s.user_reads,3)
                row=next(i for i,r in enumerate(t.screen.display) if 'Menções' in r)
                t.send(f'\x1b[<65;35;{row+3}M');t.read(.1)
                # Click the first displayed candidate after scrolling.
                label=t.screen.display[row+1]; name=re.search(r'@(alice|bob|person[0-9]+)',label).group(1)
                column=label.index('@')+1;t.click(row+1,column)
                self.assertNotIn('Menções','\n'.join(t.screen.display))
                t.send('\n');self.wait(t,'Mensagem confirmada')
                target=next(u for u in s.users if u['username']==name)
                self.assertIn(target['id'],s.sent_messages[0]['content'])
            finally:self.assertTrue(t.close())

    def test_pagination_preserves_the_initial_highlight_without_navigation(self):
        with MentionServer(102) as s:
            for n, user in enumerate(s.users): user['username'] = f'zzz{n:03}' if n < 100 else f'aaa{n:03}'
            route = s.route
            def delayed_page(method, path, body):
                if method == 'GET' and urlsplit(path).path.endswith('/users') and 'last_id=' in path:
                    time.sleep(.4)
                return route(method, path, body)
            s.api_handler = delayed_page
            t = self.open(s)
            try:
                t.send('@'); self.wait(t, '@zzz000'); self.wait(t, 'Tab / Enter')
                t.send('\t\n'); self.wait(t, 'Mensagem confirmada')
                self.assertEqual(s.user_reads, 2)
                self.assertEqual(s.sent_messages[0]['content'], f'@mention(<@{uuid(1000)}>) ')
            finally: self.assertTrue(t.close())
    def test_escape_empty_results_paste_and_email_do_not_send(self):
        with MentionServer(2) as s:
            t=self.open(s)
            try:
                t.send('a@host ');t.read(.2);self.assertNotIn('Menções','\n'.join(t.screen.display))
                t.send('@zzz');self.wait(t,'Nenhum usuário');t.send('\t\n');self.assertFalse(s.sent_messages)
                t.send('\x1b');t.read(.2);self.assertNotIn('Menções','\n'.join(t.screen.display))
                t.send('\x15\x1b[200~@alice\x1b[201~');t.read(.2)
                self.assertNotIn('Menções','\n'.join(t.screen.display));self.assertFalse(s.sent_messages)
            finally:self.assertTrue(t.close())
    def test_rate_failure_resize_retry_and_draft(self):
        with MentionServer(2) as s:
            s.reject_users=True;t=self.open(s)
            try:
                t.send('Meu @ali');self.wait(t,'Falha ao listar');t.resize(14,44)
                s.reject_users=False;t.send('\x12');self.wait(t,'@alice');t.send('\t')
                self.wait(t,'@Álice');t.send('\n');self.wait(t,'Mensagem confirmada')
                self.assertTrue(s.sent_messages[0]['content'].startswith('Meu @mention'))
            finally:self.assertTrue(t.close())
    def test_live_mentions_resolve_and_self_bar_survives_edits(self):
        with MentionServer(2) as s:
            t=self.open(s)
            try:
                m=dict(s.message(1),content=f'Olá @mention(<@{uuid(1)}>) e @mention(<@{uuid(1000)}>)')
                s.emit(dict(m,type='message'));self.wait(t,'@Álice');self.wait(t,'@josé')
                # Own mentions mark the left edge of the conversation.
                self.assertTrue(any(t.screen.buffer[y][26].data in ('│','x') for y in range(8,16)))
                s.emit(dict(type='message_edit',id=m['id'],channel_id=CHANNEL,content='Sem menção',edited_at=STAMP))
                self.wait(t,'Sem menção');self.assertNotIn('@josé','\n'.join(t.screen.display))
                assert_history_reads(self, s, {CHANNEL: (1, 2)})
            finally:self.assertTrue(t.close())
    def test_embed_mentions_resolve_highlight_and_follow_live_updates(self):
        with MentionServer(2) as s:
            t = self.open(s)
            token = f'@mention(<@{uuid(1000)}>)'
            embed = dict(title='Título ' + token, description='Descrição ' + token,
                         site_name='Site ' + token, color='#00b0f4',
                         author={'name': 'Autor ' + token}, footer={'text': 'Rodapé ' + token},
                         fields=[{'name': 'Campo ' + token, 'value': 'Valor ' + token, 'inline': True},
                                 {'name': 'Conta', 'value': f'@mention(<@{uuid(1)}>)', 'inline': True}])
            try:
                t.resize(45, 120)
                m = dict(s.message(1), content='', embeds=[embed])
                s.emit(dict(m, type='message'))
                self.wait(t, '@Álice'); self.wait(t, '@josé'); t.read(.2)
                screen = '\n'.join(t.screen.display)
                self.assertEqual(screen.count('@Álice'), 7)
                self.assertNotIn('@mention(', screen)
                self.assertEqual(s.user_reads, 0)
                mentions = [(y, x) for y, line in enumerate(t.screen.display)
                            for x in range(len(line)) if line.startswith('@Álice', x)]
                first_y, first_x = mentions[0]
                orange = t.screen.buffer[first_y][first_x].fg
                for y, x in mentions:
                    for k in range(len('@Álice')):
                        cell = t.screen.buffer[y][x + k]
                        self.assertEqual(cell.fg, orange)
                        self.assertTrue(cell.bold)
                    bar_x = t.screen.display[y].index('▎')
                    self.assertNotEqual(orange, t.screen.buffer[y][bar_x].fg)
                    self.assertEqual(t.screen.buffer[y][x].bg, t.screen.buffer[y][bar_x].bg)
                title_y = next(y for y, line in enumerate(t.screen.display) if 'Título ' in line)
                title_x = t.screen.display[title_y].index('Título ')
                self.assertTrue(t.screen.buffer[title_y][title_x].bold)
                self.assertTrue(any(t.screen.buffer[y][26].data in ('│', 'x') for y in range(8, 20)))
                s.emit(dict(type='message_embeds_update', channel_id=CHANNEL, message_id=m['id'],
                            embeds=[{'description': f'Atualizado @mention(<@{uuid(1001)}>)'}]))
                self.wait(t, 'Atualizado @bob')
                self.assertNotIn('@Álice', '\n'.join(t.screen.display))
                assert_history_reads(self, s, {CHANNEL: (1, 2)})
            finally: self.assertTrue(t.close())

    def test_picker_keeps_websocket_and_draft_alive(self):
        with MentionServer(2) as s:
            t=self.open(s)
            try:
                t.send('Rascunho @al');self.wait(t,'@alice')
                message=dict(s.message(1),content='Recebida com cartão aberto')
                s.emit(dict(message,type='message'));t.read(.4)
                self.assertIn('Menções','\n'.join(t.screen.display))
                t.send('\x1b');self.wait(t,message['content'])
                self.assertIn('Rascunho @al','\n'.join(t.screen.display))
                self.assertFalse(s.sent_messages)
                self.assertEqual(sum('/messages' in r[1] for r in s.requests if r[0]=='GET'),1)
            finally:self.assertTrue(t.close())

    def test_duplicate_nicknames_select_the_highlighted_identity(self):
        with MentionServer(2) as s:
            for user in s.users: user['nickname'] = 'Mesmo apelido'
            t = self.open(s)
            try:
                t.send('@Mesmo'); self.wait(t, '@bob')
                t.send('\x1bOB\t\n'); self.wait(t, 'Mensagem confirmada')
                self.assertEqual(s.sent_messages[0]['content'], f'@mention(<@{uuid(1001)}>) ')
            finally: self.assertTrue(t.close())

    def test_wire_limit_refuses_selection_without_truncating_the_draft(self):
        with MentionServer(2) as s:
            t = self.open(s)
            try:
                t.send('\x1b[200~' + 'a'*2030 + ' \x1b[201~')
                t.send('@ali'); self.wait(t, '@alice'); t.send('\t')
                self.wait(t, 'Menção excede o limite de texto')
                self.assertIn('@ali', '\n'.join(t.screen.display))
                self.assertIn('Menções', '\n'.join(t.screen.display))
                self.assertFalse(s.sent_messages)
            finally: self.assertTrue(t.close())

    def test_missing_identity_is_cached_without_repeated_requests(self):
        with MentionServer(2) as s:
            route = s.route
            missing = uuid(99999)
            def omit(method, path, body):
                result = route(method, path, body)
                if path.endswith('/users/user_summary_batch'):
                    return 200, [user for user in result[1] if user['id'] != missing]
                return result
            s.api_handler = omit
            t = self.open(s)
            try:
                for n in (1, 2):
                    message = dict(s.message(n), content=f'@mention(<@{missing}>) chegada {n}')
                    if n == 2:
                        message.update(content='', embeds=[{'description': f'@mention(<@{missing}>) chegada {n}'}])
                    s.emit(dict(message, type='message')); self.wait(t, f'chegada {n}')
                    t.read(.3)
                self.assertIn('@usuário indisponível', '\n'.join(t.screen.display))
                batches = [request[3]['ids'] for request in s.requests if 'summary_batch' in request[1]]
                self.assertEqual(sum(missing in batch for batch in batches), 1)
            finally: self.assertTrue(t.close())

    def test_directory_unauthorized_rotates_once_and_returns_to_login(self):
        with MentionServer(2) as s:
            route = s.route
            def unauthorized(method, path, body):
                if method == 'GET' and urlsplit(path).path.endswith('/users'):
                    s.user_reads += 1
                    return 401, None
                return route(method, path, body)
            s.api_handler = unauthorized
            t = self.open(s)
            try:
                t.send('Rascunho @'); self.wait(t, '[ Entrar ]')
                self.assertEqual(s.user_reads, 2)
                self.assertEqual(s.refresh_count, 1)
                self.assertNotIn('Menções', '\n'.join(t.screen.display))
                self.assertFalse(s.sent_messages)
            finally: self.assertTrue(t.close())

    def test_directory_invalid_page_preserves_draft_and_can_retry(self):
        with MentionServer(2) as s:
            route = s.route
            s.api_handler = lambda method, path, body: (200, {'users': [None], 'has_more': True}) if (
                method == 'GET' and urlsplit(path).path.endswith('/users')) else route(method, path, body)
            t = self.open(s)
            try:
                t.send('Texto @ali'); self.wait(t, 'Falha ao listar')
                s.api_handler = route; t.send('\x12'); self.wait(t, '@alice')
                t.send('\t\n'); self.wait(t, 'Mensagem confirmada')
                self.assertEqual(s.sent_messages[0]['content'], f'Texto @mention(<@{uuid(1000)}>) ')
            finally: self.assertTrue(t.close())

    def test_same_uuid_names_and_drafts_are_isolated_between_servers(self):
        with MentionServer(2) as first, MentionServer(2) as second:
            second.name = 'Trabalho'
            second.users[0]['nickname'] = 'Outro nome'
            t = self.open(first)
            def click(label):
                row = next(i for i, value in enumerate(t.screen.display) if label in value)
                t.click(row, t.screen.display[row].index(label) + 1)
            try:
                t.send('@ali'); self.wait(t, '@alice'); t.send('\t'); self.wait(t, '@Álice')
                click('[+ Adicionar]'); self.wait(t, 'URL do servidor')
                t.send(second.url + '\tjosé\tSynthetic!123\n'); self.wait(t, '[Trabalho]')
                self.wait(t, 'geral')
                t.send('\x1bOB\x1bOB\n'); self.wait(t, 'Nenhuma mensagem neste canal')
                t.send('@ali'); self.wait(t, '@alice'); t.send('\t'); self.wait(t, '@Outro nome')
                self.assertNotIn('@Álice', '\n'.join(t.screen.display))
                click('Comunidade'); self.wait(t, '[Comunidade]')
                self.wait(t, 'geral')
                t.send('\x1bOB\x1bOB\n'); self.wait(t, 'Nenhuma mensagem neste canal')
                t.send('@ali'); self.wait(t, '@alice'); self.wait(t, '@Álice')
                self.assertNotIn('Outro nome', '\n'.join(t.screen.display))
                self.assertFalse(first.sent_messages or second.sent_messages)
            finally: self.assertTrue(t.close())

    def test_demo_removed_before_terminal_or_network(self):
        result=subprocess.run([BINARY,'--demo'],capture_output=True,text=True,timeout=5)
        self.assertEqual(result.returncode,1)
        self.assertNotIn('--demo',subprocess.check_output([BINARY,'--help'],text=True))

if __name__=='__main__':unittest.main()
