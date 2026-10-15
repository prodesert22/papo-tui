"""Emoji picker, authenticated assets and retained pictures in a real PTY."""
import base64
from datetime import datetime, timezone
import time
import unittest
from urllib.parse import urlsplit, parse_qs
import test_media_terminal as media_tests
from history_mock import HistoryMock, CHANNEL, uuid
from realtime_mock import RealtimeMock
from test_terminal import editor_region


class EmojiTerminal(unittest.TestCase):
    include_icons = True
    wait = media_tests.MediaTerminal.wait
    open = media_tests.MediaTerminal.open
    tearDown = media_tests.MediaTerminal.tearDown

    @classmethod
    def setUpClass(cls):
        media_tests.MediaTerminal.setUpClass()
        cls.png = media_tests.MediaTerminal.images['png']
        cls.gif = media_tests.MediaTerminal.animated_gif

    @classmethod
    def tearDownClass(cls): media_tests.MediaTerminal.tearDownClass()

    def until(self, t, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while not predicate() and time.monotonic() < deadline: t.read(.03)
        self.assertTrue(predicate(), '\n'.join(t.screen.display))

    def server(self, *, content='Mensagem 0001', count=1, animated=False, large=False, status=200, realtime=False):
        s = (RealtimeMock if realtime else HistoryMock)(count=count)
        for m in s.messages:
            m.update(content=content, created_at=datetime.now(timezone.utc).isoformat())
        parent = s.route
        blob = base64.b64encode(self.gif if animated else self.png + (b'x' * 120000 if large else b'')).decode()
        s.emojis = [{'id':uuid(900 + i),'name':f'gato{i}' if i else 'gato 界',
                     'format':'GIF' if animated else 'PNG','image_blob':blob,
                     'created_at':'2026-10-14T10:00:00.000000Z'} for i in range(26 if large else 1)]
        avatar = base64.b64encode(self.gif if animated else self.png + (b'x' * 900000 if large else b'')).decode()
        def route(method, path, body):
            url = urlsplit(path)
            if url.path.endswith('/emojis'):
                if status != 200: return status, None
                cursor = parse_qs(url.query).get('last_id', [''])[0]
                first = 25 if cursor else 0
                return 200, {'emojis':s.emojis[first:first+25], 'has_more':first+25<len(s.emojis)}
            if url.path.endswith('/users/profile_batch'):
                self.assertLessEqual(len(body['ids']),4)
                self.assertEqual(len(body['ids']),len(set(body['ids'])))
                return 200, {'profiles':[{'id':id,'username':'Autor','avatar_blob':avatar,
                                         'avatar_format':'GIF' if animated else 'PNG'} for id in body['ids']]}
            return parent(method,path,body)
        s.api_handler = route
        return s

    def test_keyboard_unicode_inserts_at_cursor_without_sending(self):
        with self.server() as s:
            t = self.open(s, protocol='kitty')
            try:
                t.send('antes depois\x1b[1;5D\x1bOS')
                self.wait(t,'Emojis · Esc voltar')
                t.send('family: man, woman, girl, boy')
                self.wait(t,'família')
                t.send('\n')
                self.until(t,lambda:'Emojis · Esc voltar' not in '\n'.join(t.screen.display))
                self.assertNotIn('Emojis · Esc voltar','\n'.join(t.screen.display))
                self.assertEqual(s.message_count,0)
                t.send('\n'); self.until(t,lambda:s.message_count == 1)
                self.assertEqual(s.sent_messages[0]['content'],'antes 👨‍👩‍👧‍👦depois')
            finally: self.assertTrue(t.close())

    def test_button_focus_custom_emoji_and_outside_click(self):
        with self.server() as s:
            t = self.open(s, sixel=False)
            try:
                t.send('rascunho\t'); self.wait(t,'Foco: Emojis')
                t.send('\n'); self.wait(t,'Emojis · Esc voltar')
                t.send('\t\x1bOC')
                self.wait(t,'1 emojis do servidor')
                t.send('\t\n')
                self.assertEqual(s.message_count,0)
                first,_ = editor_region(t)
                self.assertIn('rascunho:gato 界:',t.screen.display[first])
                t.click(first,t.screen.columns-7); self.wait(t,'Emojis · Esc voltar')
                reads = sum('/channels/' in r[1] and '/messages' in r[1] for r in s.requests)
                t.click(8,4); t.read(.2)
                self.assertNotIn('Emojis · Esc voltar','\n'.join(t.screen.display))
                self.assertEqual(sum('/channels/' in r[1] and '/messages' in r[1] for r in s.requests),reads)
                t.send('\n'); self.until(t,lambda:s.message_count==1)
                self.assertEqual(s.sent_messages[0]['content'],'rascunho:gato 界:')
            finally: self.assertTrue(t.close())

    def test_paging_large_base64_responses_and_no_repeat_reads(self):
        with self.server(content=':gato25:',large=True) as s:
            t = self.open(s,protocol='kitty')
            try:
                self.until(t,lambda:sum('/emojis?' in r[1] for r in s.requests)==2)
                self.until(t,lambda:bool(t.stream.frames))
                t.send('\x1bOS\t\x1bOC'); self.wait(t,'26 emojis do servidor')
                t.send('\x1b'); self.until(t,lambda:any('/users/profile_batch' in r[1] for r in s.requests)); t.read(.5)
                relevant=lambda:sum('/emojis?' in r[1] or '/users/profile_batch' in r[1] for r in s.requests)
                count=relevant(); t.send('a'); t.read(.3)
                self.assertEqual(relevant(),count)
                self.assertTrue(any('/users/profile_batch' in r[1] for r in s.requests))
                self.assertTrue(all(r[2]=='Auth='+s.session_cookie for r in s.requests if '/emojis?' in r[1] or '/users/profile_batch' in r[1]))
            finally: self.assertTrue(t.close())

    def test_repeated_unicode_shares_uploads_and_does_not_flash(self):
        with self.server(content='😄 '*90) as s:
            t = self.open(s,protocol='kitty')
            try:
                # The button uses the same icon but a different background.
                self.until(t,lambda:len(t.stream.placements)>65)
                by_image={}
                for placement in t.stream.placements:
                    by_image.setdefault(placement[b'i'],set()).add(placement[b'p'])
                self.assertGreater(max(map(len,by_image.values())),64)
                shared=max(by_image,key=lambda id:len(by_image[id]))
                pixels=next(p for h,p in t.stream.kitty_frames if h[b'i']==shared)
                self.assertTrue(any(r>g+15 and g>b+30 for r,g,b in zip(pixels[::4],pixels[1::4],pixels[2::4])), 'Unicode icon must contain color pixels')
                t.read(.3); uploads=len(t.stream.frames)
                t.send('a'); t.send('b'); t.read(.2)
                self.assertEqual(len(t.stream.frames),uploads)
            finally: self.assertTrue(t.close())

    def test_custom_and_avatar_gifs_animate_only_while_visible(self):
        with self.server(content=':gato 界:',animated=True) as s:
            t = self.open(s,protocol='kitty')
            try:
                self.until(t,lambda:len(t.stream.frames)>5)
                ids={header[b'i'] for header,_ in t.stream.kitty_frames}
                self.assertGreaterEqual(len(ids),3)
                requests=len(s.requests); t.read(.3)
                self.assertEqual(len(s.requests),requests)
                variants={}
                for header,pixels in t.stream.kitty_frames:
                    if header.get(b's')==b'16' and header.get(b'v')==b'16':
                        variants.setdefault(header[b'i'],set()).add(pixels)
                animated_ids={id for id,pixels in variants.items() if len(pixels)>1}
                self.assertTrue(animated_ids)
                t.click(8,4); self.wait(t,'Conteúdo do outro canal')
                before=len(t.stream.kitty_frames); t.read(.6)
                self.assertFalse(any(header[b'i'] in animated_ids for header,_ in t.stream.kitty_frames[before:]))
            finally: self.assertTrue(t.close())

    def test_search_resize_cancel_limit_and_error(self):
        with self.server(status=429) as s:
            t = self.open(s,sixel=False)
            try:
                t.send('\x1b[200~'+'x'*2048+'\x1b[201~\x1bOS')
                self.wait(t,'Emojis · Esc voltar')
                t.send('grinning face with smiling eyes\n'); self.wait(t,'excede o limite')
                t.resize(14,44); self.wait(t,'Emojis · Esc voltar')
                t.send('\x1b'); self.assertEqual(s.message_count,0)
                t.send('\x1bOS\x03\t\x1bOC'); self.wait(t,'Limite atingido')
                t.resize(30,100); self.wait(t,'Emojis · Esc voltar')
                t.send('\x1b'); self.assertEqual(s.message_count,0)
            finally: self.assertTrue(t.close())

    def test_profile_401_recovers_once_without_losing_read_only_post(self):
        with self.server() as s:
            route=s.api_handler; reads=[]
            def auth_once(method,path,body):
                if path.endswith('/users/profile_batch'):
                    reads.append(body)
                    if len(reads)==1:return 401,None
                return route(method,path,body)
            s.api_handler=auth_once
            t=self.open(s,protocol='kitty')
            try:
                self.until(t,lambda:any(h.get(b's')==b'48' and h.get(b'v')==b'48' for h,_ in t.stream.kitty_frames))
                self.assertEqual(len(reads),2);self.assertEqual(reads[0],reads[1])
                self.assertEqual(s.refresh_count,1);self.assertEqual(s.message_count,0)
            finally:self.assertTrue(t.close())

    def test_profile_errors_preserve_initials_and_wait_for_manual_retry(self):
        for status in (403,404,429,500):
            with self.subTest(status=status),self.server() as s:
                route=s.api_handler
                def fail_profile(method,path,body):
                    if path.endswith('/users/profile_batch'):return status,None
                    return route(method,path,body)
                s.api_handler=fail_profile
                t=self.open(s,protocol='kitty')
                try:
                    reads=lambda:sum('/users/profile_batch' in r[1] for r in s.requests)
                    self.until(t,lambda:reads()==1);t.read(.4)
                    self.assertEqual(reads(),1);self.wait(t,'Apelido')
                    self.assertNotIn('URL do servidor','\n'.join(t.screen.display))
                finally:self.assertTrue(t.close())

    def test_deleted_channel_closes_picker_and_cannot_send(self):
        with self.server(realtime=True) as s:
            t=self.open(s,protocol='kitty')
            try:
                self.until(t,lambda:bool(s.peers))
                t.send('rascunho preservado\x1bOS');self.wait(t,'Emojis · Esc voltar')
                s.emit({'type':'channel_delete','channel_id':CHANNEL})
                self.wait(t,'Canal removido')
                self.assertNotIn('Emojis · Esc voltar','\n'.join(t.screen.display))
                t.send('\n');t.read(.2);self.assertEqual(s.message_count,0)
            finally:self.assertTrue(t.close())

    def test_header_avatar_position_local_time_and_edited_marker(self):
        with self.server(content='Corpo usa largura inteira') as s:
            s.messages[0]['edited_at']=s.messages[0]['created_at']
            t=self.open(s,protocol='kitty',environment={'TZ':'UTC','LC_TIME':'C'})
            try:
                self.wait(t,'editado')
                self.until(t,lambda:any(h.get(b's')==b'48' and h.get(b'v')==b'48' for h,_ in t.stream.kitty_frames))
                avatar, pixels = next((h,p) for h,p in t.stream.kitty_frames
                                      if h.get(b's')==b'48' and h.get(b'v')==b'48')
                # The rectangular fixture fills the 48x48 avatar by cropping,
                # without adding a colored frame around its pixels.
                red = [(i % 48, i // 48) for i in range(48*48)
                       if pixels[i*4] > 230 and pixels[i*4+1] < 25 and pixels[i*4+2] < 25]
                self.assertEqual((min(x for x,y in red), max(x for x,y in red)), (0,47))
                self.assertEqual((min(y for x,y in red), max(y for x,y in red)), (0,47))
                self.assertEqual(len(red),48*48)
                rows=t.screen.display
                body=next(i for i,row in enumerate(rows) if 'Corpo usa largura inteira' in row)
                head=next(i for i,row in enumerate(rows) if 'editado' in row)
                placement = next(pos for p,pos in zip(t.stream.placements, t.stream.placement_positions)
                                 if p[b'i'] == avatar[b'i'])
                self.assertEqual(placement, (head,28))
                self.assertEqual(body-head,3)
                self.assertEqual(rows[body].index('Corpo'),28)
                self.assertGreaterEqual(rows[head].index('Apelido'),35)
                self.assertNotIn('Editada em:','\n'.join(rows))
                self.assertNotIn('2026-','\n'.join(rows))
                self.assertEqual(sum('/users/profile_batch' in r[1] for r in s.requests),1)
            finally: self.assertTrue(t.close())

    def test_avatar_fills_48_pixels_without_padding_across_focus_changes(self):
        environments = ({'TERM':'xterm-kitty','KITTY_WINDOW_ID':'1'},
                        {'TERM':'xterm-256color','KONSOLE_VERSION':'250000'})
        for environment in environments:
            with self.subTest(environment=environment), self.server() as s:
                t=self.open(s,protocol='kitty',environment=environment)
                try:
                    self.wait(t,'Apelido')
                    def fills_avatar():
                        frames=[p for h,p in t.stream.kitty_frames if h.get(b's')==b'48' and h.get(b'v')==b'48']
                        return bool(frames) and all(frames[-1][i*4]>230 and frames[-1][i*4+1]<25 and frames[-1][i*4+2]<25 for i in range(48*48))
                    self.until(t,fills_avatar)
                    t.send('\t');self.wait(t,'Foco: Mensagem')
                    self.until(t,fills_avatar)
                    t.send('\x1b[Z');self.wait(t,'Foco: Histórico')
                    self.until(t,fills_avatar)
                    self.assertEqual(sum('/users/profile_batch' in r[1] for r in s.requests),1)
                finally:self.assertTrue(t.close())

    def test_webhook_app_name_badge_and_shared_animated_avatar(self):
        with self.server(count=2) as s:
            reference = '/webhooks/' + uuid(900) + '/avatar'
            for m in s.messages:
                m.update(author_id=None, webhook_id=uuid(900),
                         external_author={'source':'webhook','id':uuid(900),'name':'Build Bot','avatar_url':reference})
            route=s.api_handler
            def avatar(method,path,body):
                if path.endswith(reference): return 200,self.gif,None,None,'image/gif'
                return route(method,path,body)
            s.api_handler=avatar
            t=self.open(s,protocol='kitty')
            try:
                self.wait(t,'Build Bot [APP]')
                self.assertNotIn('Autor indisponível','\n'.join(t.screen.display))
                def variants():
                    frames={}
                    for h,p in t.stream.kitty_frames:
                        if h.get(b's')==b'48' and h.get(b'v')==b'48':frames.setdefault(h[b'i'],set()).add(p)
                    return frames
                self.until(t,lambda:any(len(p)>1 for p in variants().values()))
                app_ids=set(variants())
                calls=[r for r in s.requests if r[1].endswith(reference)]
                self.assertEqual(len(calls),1);self.assertFalse(calls[0][2])
                self.assertEqual(s.summary_calls,0)
                self.assertFalse(any('/users/profile_batch' in r[1] for r in s.requests))
                t.send('x');t.read(.15)
                self.assertEqual(sum(r[1].endswith(reference) for r in s.requests),1)
                t.click(8,4);self.wait(t,'Conteúdo do outro canal')
                before=len(t.stream.kitty_frames);t.read(.3)
                self.assertFalse(any(h[b'i'] in app_ids for h,p in t.stream.kitty_frames[before:]))
            finally:self.assertTrue(t.close())

    def test_webhook_attribution_arrives_over_websocket(self):
        with self.server(realtime=True) as s:
            route=s.api_handler
            reference='/webhooks/'+uuid(900)+'/avatar'
            def avatar(method,path,body):
                if path.endswith(reference):return 200,self.png,None,None,'image/png'
                return route(method,path,body)
            s.api_handler=avatar
            t=self.open(s,protocol='kitty')
            try:
                self.wait(t,'Mensagem 0001');self.until(t,lambda:bool(s.peers))
                event=dict(s.message(2),type='message',author_id='',webhook_id=uuid(900),content='Evento do app',
                           created_at=datetime.now(timezone.utc).isoformat(),
                           external_author={'source':'discord','id':'external-id','name':'Deploy App','avatar_url':reference})
                s.emit(event);self.wait(t,'Deploy App [APP]');self.wait(t,'Evento do app')
                self.until(t,lambda:any(r[1].endswith(reference) for r in s.requests))
                self.assertFalse(next(r for r in s.requests if r[1].endswith(reference))[2])
                self.assertEqual(s.message_count,0)
            finally:self.assertTrue(t.close())

    def test_webhook_avatar_failure_keeps_name_badge_and_does_not_retry(self):
        for status in (401,403,404,429,500):
            with self.subTest(status=status),self.server() as s:
                reference='/webhooks/'+uuid(900)+'/avatar'
                s.messages[0].update(author_id=None,webhook_id=uuid(900),
                    external_author={'source':'webhook','id':uuid(900),'name':'Build Bot','avatar_url':reference})
                route=s.api_handler
                def fail(method,path,body):
                    if path.endswith(reference):return status,None
                    return route(method,path,body)
                s.api_handler=fail
                t=self.open(s,protocol='kitty')
                try:
                    self.wait(t,'Build Bot [APP]')
                    reads=lambda:sum(r[1].endswith(reference) for r in s.requests)
                    self.until(t,lambda:reads()==1);t.read(.25);t.send('x');t.read(.15)
                    self.assertEqual(reads(),1);self.assertEqual(s.refresh_count,0)
                    self.assertNotIn('Autor indisponível','\n'.join(t.screen.display))
                finally:self.assertTrue(t.close())

if __name__=='__main__': unittest.main()
