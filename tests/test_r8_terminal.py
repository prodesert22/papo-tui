"""Full R8 UI with synthetic HTTP/WS fixtures."""
import json
import time
import unittest
from r8_mock import R8Mock, DM, AUTHOR, uuid
from test_terminal import Terminal

class R8Terminal(unittest.TestCase):
    def wait(self,t,text):
        end=time.monotonic()+7
        while text not in '\n'.join(t.screen.display) and time.monotonic()<end: t.read(.03)
        self.assertIn(text,'\n'.join(t.screen.display))
    def login(self,server):
        t=Terminal(); self.addCleanup(t.close)
        t.send(server.url+'\tjosé\tSynthetic!123\n'); self.wait(t,'[Canais]  [DMs]'); self.wait(t,'Categoria')
        return t
    def test_adjacent_buttons_keyboard_mouse_and_no_history_on_switch(self):
        with R8Mock() as server:
            t=self.login(server)
            t.send('\t\x1bOC\n')
            self.wait(t,'Bia ·')
            self.assertFalse(any('/messages' in r[1] for r in server.requests))
            t.click(5,4); t.read(.2)
            self.assertIn('Categoria','\n'.join(t.screen.display))
            t.click(5,13); self.wait(t,'Bia ·')
            self.assertFalse(any('/messages' in r[1] for r in server.requests))
            t.send('\x1bOB\n'); self.wait(t,'Mensagem direta')
            t.send('rascunho DM\x1b'); t.click(5,4)
            self.wait(t,'Categoria'); self.assertIn('rascunho DM','\n'.join(t.screen.display))
            self.assertIn('Mensagem direta','\n'.join(t.screen.display))
    def test_center_details_marks_only_selected_and_preferences_persist(self):
        with R8Mock(count=3) as server:
            t=self.login(server); t.send('\x1b[20~'); self.wait(t,'Notificações')
            self.wait(t,'Aviso 0003'); self.assertEqual(server.marked,[])
            t.send('\x1bOB\n'); self.wait(t,'Detalhes da notificação')
            end=time.monotonic()+4
            while not server.marked and time.monotonic()<end: t.read(.03)
            self.assertEqual(server.marked,[[uuid(4002)]])
            t.send('\x1b\t\n'); self.wait(t,'Preferências')
            t.send('\n\t\n')
            path=t.configuration.name+'/papo-tui/config.json'
            with open(path) as f: config=json.load(f)
            self.assertEqual(config['version'],3)
            self.assertFalse(config['desktop_notifications']); self.assertFalse(config['notification_preview'])
            t.resize(14,44); self.wait(t,'Preferências'); t.send('\x1b\x1b')
            self.wait(t,'[Canais]  [DMs]')
    def test_new_dm_picker_filters_excludes_self_and_banned_and_closes(self):
        with R8Mock() as server:
            t=self.login(server); t.send('\x1b[21~'); self.wait(t,'Nova DM'); self.wait(t,'Bia (@bob)')
            self.assertNotIn('banido','\n'.join(t.screen.display))
            server.emit(dict(type='presence_update',user_id=uuid(3),status='online'))
            t.read(.2); self.assertNotIn('banido','\n'.join(t.screen.display))
            t.send('nenhum'); self.wait(t,'Nenhum usuário disponível'); t.send('\x03')
            self.wait(t,'Bia (@bob)'); t.send('bia\n'); self.wait(t,'Mensagem direta')
            t.click(5,13); t.send('\x1bOB\x04'); self.wait(t,'Nenhuma DM')
            self.assertTrue(any(r[0]=='DELETE' and r[1].endswith('/dms/'+DM) for r in server.requests))
    def test_details_survive_ephemeral_promotion_to_different_rest_id(self):
        from r8_mock import notification
        with R8Mock(count=0) as server:
            t=self.login(server)
            server.emit(dict(type='new_notification',id=uuid(9000),user_id=AUTHOR,message_id=uuid(5001),message_content='Aviso em promoção'))
            t.send('\x1b[20~'); self.wait(t,'Aviso em promoção'); t.send('\n')
            self.wait(t,'Detalhes da notificação'); self.assertEqual(server.marked,[])
            row=notification(1); row['message_content']='Aviso confirmado'; server.notifications=[row]
            t.send('\x12'); self.wait(t,'Aviso confirmado'); t.read(.3)
            self.assertEqual(server.marked,[[uuid(4001)]])
            self.assertNotIn('Notificação indisponível','\n'.join(t.screen.display))
    def test_disconnect_notice_only_in_footer_and_priority_at_minimum_width(self):
        with R8Mock() as server:
            t=self.login(server); self.wait(t,'Tempo real conectado')
            t.resize(14,44); server.revoked=False; server.disconnect()
            self.wait(t,'Desconectado')
            self.assertIn('Desconectado',t.screen.display[-2])
            self.assertNotIn('Desconectado','\n'.join(t.screen.display[:-2]))

    def test_inactive_instance_events_and_identical_ids_are_isolated(self):
        with R8Mock() as first,R8Mock() as second:
            first.name='Primeiro'; second.name='Segundo'; second.session_cookie='other-instance-fixture'
            first.notifications[0]['message_content']='Aviso primeiro servidor'
            second.notifications[0]['message_content']='Aviso segundo servidor'
            t=self.login(first); t.click(1,70); self.wait(t,'URL do servidor')
            t.send(second.url+'\tjosé\tSynthetic!123\n'); self.wait(t,'Segundo'); self.wait(t,'Categoria')
            from r8_mock import notification
            newest=notification(2); newest['message_content']='Novidade no servidor inativo'; first.notifications.append(newest)
            first.emit(dict(type='new_notification',id=newest['id'],user_id=AUTHOR,message_id=newest['message_id'],message_content=newest['message_content']))
            t.read(.5); t.send('\x1b[20~'); self.wait(t,'Aviso segundo servidor')
            self.assertNotIn('Aviso primeiro servidor','\n'.join(t.screen.display))
            t.send('\n'); self.wait(t,'Detalhes da notificação'); t.read(.2)
            self.assertEqual(second.marked,[[uuid(4001)]]); self.assertEqual(first.marked,[])
            t.send('\x1b\x1b'); t.click(1,4); self.wait(t,'Foco: Abas'); t.send('\x1b[20~')
            self.wait(t,'Novidade no servidor inativo'); self.assertNotIn('Aviso segundo servidor','\n'.join(t.screen.display))
            self.assertFalse(any('/messages' in r[1] for r in first.requests))

    def test_presence_and_typing_in_dm_follow_snapshot_and_expire(self):
        with R8Mock(count=0) as server:
            t=self.login(server); t.click(5,13); self.wait(t,'Bia ·'); t.click(6,4); self.wait(t,'Mensagem direta')
            server.emit(dict(type='presence_sync',members=[dict(user_id=AUTHOR,status='online')]))
            self.wait(t,'online')
            server.emit(dict(type='typing',channel_id=DM,user_id=AUTHOR,is_typing=True)); self.wait(t,'Bia está digitando')
            t.send('atividade real'); t.read(.2)
            self.assertTrue(any(e.get('type')=='typing' and e.get('channel_id')==DM for e in server.ws_messages))
            self.assertLessEqual(sum(e.get('type')=='typing' for e in server.ws_messages),1)
            server.emit(dict(type='presence_update',user_id=AUTHOR,status='offline'))
            self.wait(t,'offline'); self.assertNotIn('está digitando','\n'.join(t.screen.display))

if __name__=='__main__': unittest.main()
