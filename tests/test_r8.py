import json
import os
import select
import subprocess
import sys
import time
import unittest
from r8_mock import R8Mock, DM, AUTHOR, notification, uuid
PROBE=sys.argv.pop(1)

class Observer:
    def __init__(self,url):
        self.p=subprocess.Popen([PROBE,url],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                text=True,env=dict(os.environ,LC_ALL='C.UTF-8',NO_PROXY='*'))
    def command(self,value='snapshot'):
        self.p.stdin.write(value+'\n'); self.p.stdin.flush()
        if not select.select([self.p.stdout],[],[],5)[0]: raise AssertionError('Observer timeout')
        line=self.p.stdout.readline()
        if not line: raise AssertionError(self.p.stderr.read())
        return json.loads(line)
    def wait(self,predicate):
        until=time.monotonic()+6
        while time.monotonic()<until:
            result=self.command()
            if predicate(result): return result
            time.sleep(.02)
        raise AssertionError(result)
    def close(self):
        if self.p.poll() is None: self.p.stdin.write('quit\n'); self.p.stdin.flush()
        self.p.wait(timeout=6)
        if self.p.returncode: raise AssertionError(self.p.stderr.read())
        self.p.stdin.close(); self.p.stdout.close(); self.p.stderr.close()

class R8Tests(unittest.TestCase):
    def observer(self,server):
        client=Observer(server.url); self.addCleanup(client.close)
        client.wait(lambda r:len(r.get('dms',[]))==1 and 'notifications' in r)
        return client
    def test_listing_never_marks_read_and_oldest_cursor_preserves_ties(self):
        with R8Mock(count=205) as server:
            c=self.observer(server); r=c.wait(lambda r:len(r['notifications'])==100)
            self.assertEqual(r['notifications'][0]['id'],uuid(4205)); self.assertTrue(r['more'])
            c.command('older'); c.wait(lambda r:len(r['notifications'])==200)
            c.command('older'); r=c.wait(lambda r:len(r['notifications'])==205)
            self.assertFalse(r['more']); self.assertEqual(len({n['id'] for n in r['notifications']}),205)
            self.assertEqual(server.marked,[])
    def test_only_viewed_persisted_id_uses_own_user_endpoint(self):
        with R8Mock(count=3) as server:
            c=self.observer(server); c.wait(lambda r:len(r['notifications'])==3)
            c.command('detail '+uuid(4002)); c.wait(lambda r:any(n['id']==uuid(4002) and n['read'] for n in r['notifications']))
            self.assertEqual(server.marked,[[uuid(4002)]])
    def test_ephemeral_duplicate_never_marks_and_can_be_promoted_later(self):
        with R8Mock(count=0) as server:
            c=self.observer(server); event=dict(type='new_notification',id=uuid(9000),user_id=AUTHOR,message_id=uuid(5001),message_content='Efêmera')
            c.command('event '+json.dumps(event)); c.command('event '+json.dumps(event))
            r=c.command('eventdetail '+uuid(9000)); self.assertEqual(len(r['events']),1)
            time.sleep(.15); self.assertEqual(server.marked,[])
            server.notifications=[notification(1)]; c.command('refresh')
            r=c.wait(lambda r:len(r['notifications'])==1 and r['notifications'][0]['read'])
            self.assertEqual(r['events'],[]); self.assertEqual(server.marked,[[uuid(4001)]])
    def test_close_preserves_history_reappearance_and_inactive_has_no_history(self):
        with R8Mock() as server:
            c=self.observer(server); c.command('inactive')
            c.command('event '+json.dumps(dict(type='dm_update',dm=server.dms[0])))
            self.assertFalse(any('/messages' in r[1] for r in server.requests))
            c.command('active'); c.command('select '+DM); c.wait(lambda r:r['messages']==1)
            c.command('close '+DM); c.wait(lambda r:not r['dms'])
            server.dms=[__import__('r8_mock').dm()]
            c.command('event '+json.dumps(dict(type='dm_update',dm=server.dms[0])))
            self.assertEqual(len(c.command()['dms']),1)
    def test_viewing_message_consumes_ephemeral_before_or_after_delivery(self):
        with R8Mock(count=0) as server:
            c=self.observer(server)
            event=dict(type='new_notification',id=uuid(9000),user_id=AUTHOR,message_id=uuid(5001),message_content='Efêmera')
            c.command('event '+json.dumps(event))
            self.assertEqual(c.command()['unread'],1)
            r=c.command('view '+uuid(5001))
            self.assertEqual(r['unread'],0); self.assertTrue(r['events'][0]['_viewed'])
            c.command('view '+uuid(5002))
            event.update(id=uuid(9001),message_id=uuid(5002))
            r=c.command('event '+json.dumps(event))
            self.assertEqual(r['unread'],0); self.assertEqual(server.marked,[])
    def test_401_mutations_are_not_replayed(self):
        for case,command,counter in [('open-401','open '+AUTHOR,'opens'),('read-401','detail '+uuid(4001),'read_calls')]:
            with self.subTest(case=case),R8Mock(case=case) as server:
                c=self.observer(server); c.wait(lambda r:len(r['notifications'])==1)
                c.command(command); time.sleep(.3); c.wait(lambda r:r['session'])
                self.assertEqual(getattr(server,counter),1)
    def test_blocked_and_rate_limit_leave_state_and_require_manual_retry(self):
        with R8Mock(case='blocked') as server:
            c=self.observer(server); c.command('open '+AUTHOR)
            r=c.wait(lambda r:'bloqueada' in r['notice']); self.assertTrue(r['session']); self.assertEqual(server.opens,1)
        with R8Mock(case='read-429') as server:
            c=self.observer(server); c.wait(lambda r:len(r['notifications'])==1); c.command('detail '+uuid(4001))
            c.wait(lambda r:'Limite' in r['notice']); time.sleep(.2)
            self.assertEqual(server.read_calls,1)

    def test_presence_replacement_local_unseen_and_duplicate_after_view(self):
        with R8Mock(count=0) as server:
            c=self.observer(server)
            c.command('event '+json.dumps(dict(type='presence_sync',members=[dict(user_id=AUTHOR,status='online')])) )
            self.assertEqual(c.command()['presence'],'online')
            c.command('event '+json.dumps(dict(type='presence_update',user_id=AUTHOR,status='away')))
            self.assertEqual(c.command()['presence'],'away')
            c.command('event '+json.dumps(dict(type='presence_sync',members=[])))
            self.assertEqual(c.command()['presence'],'offline')
            message=dict(server.message(4),type='message')
            c.command('event '+json.dumps(message)); self.assertEqual(c.command()['unseen'],1)
            c.command('view '+message['id']); c.command('event '+json.dumps(message))
            self.assertEqual(c.command()['unseen'],0)

    def test_refresh_reconciles_deleted_rows_across_loaded_pages(self):
        with R8Mock(count=205) as server:
            c=self.observer(server); c.wait(lambda r:len(r['notifications'])==100)
            c.command('older'); c.wait(lambda r:len(r['notifications'])==200)
            c.command('older'); c.wait(lambda r:len(r['notifications'])==205)
            server.notifications=[n for n in server.notifications if n['id']!=uuid(4002)]
            c.command('refresh'); r=c.wait(lambda r:len(r['notifications'])==204)
            self.assertNotIn(uuid(4002),[n['id'] for n in r['notifications']])

    def test_notification_window_is_bounded_and_cursor_keeps_nanoseconds(self):
        with R8Mock(count=1100) as server:
            c=self.observer(server); c.wait(lambda r:len(r['notifications'])==100)
            for expected in range(200,1100,100):
                c.command('older'); c.wait(lambda r:len(r['notifications'])==expected)
            c.command('older'); time.sleep(.1); self.assertLessEqual(len(c.command()['notifications']),1000)
        with R8Mock(count=2) as server:
            server.notifications[0]['created_at']='2026-10-12T12:00:00.123456789Z'
            server.notifications[1]['created_at']='2026-10-12T12:00:00.123456788Z'
            c=self.observer(server); r=c.wait(lambda r:len(r['notifications'])==2)
            self.assertEqual(r['notifications'][0]['id'],uuid(4001))

if __name__=='__main__': unittest.main()
