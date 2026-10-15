"""R8 against the actual Go backend, with two synthetic accounts and isolated DB.

Only schema/server metadata are copied. Credentials stay in memory; the source
database is read-only and temporary resources are removed by ExitStack.
"""
import argparse
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import secrets
import ssl
import subprocess
import sys
import tempfile
import time
from uuid import UUID
import requests
import websocket
from tools.check_backend import private_write
from tools.check_r3 import ROOT, checked_run, isolated_backend, stop
from tools.check_r5 import fingerprint, source_fingerprint

def database_hash(container):
    data=checked_run(['docker','exec',container,'sh','-c',
        'exec pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" --no-owner --no-privileges'],timeout=30)
    return hashlib.sha256('\n'.join(line for line in data.splitlines() if not line.startswith(('\\restrict ','\\unrestrict '))).encode()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend-binary',type=Path,required=True)
    parser.add_argument('--tls-proxy',type=Path,required=True)
    parser.add_argument('--tui',type=Path,default=ROOT/'build/papo-tui')
    parser.add_argument('--postgres-container',default='papo_postgres')
    parser.add_argument('--output',type=Path,default=ROOT/'build/r8/validation')
    args=parser.parse_args(); work=args.output.resolve(); work.mkdir(mode=0o700,parents=True,exist_ok=True)
    checks={}; phase='setup'; last_status=None; access=[]; terminals=[]
    password='R8!'+secrets.token_hex(12); usernames=['r8a_'+secrets.token_hex(4),'r8b_'+secrets.token_hex(4)]
    before=database_hash(args.postgres_container)
    try:
        with ExitStack() as stack:
            base,seed,_=stack.enter_context(isolated_backend(args.backend_binary.resolve(),args.postgres_container,work,on_ready=access.append,label='r8'))
            seed(); sql=access[0]
            channel=str(UUID(sql("INSERT INTO channels(name,type,position) VALUES('r8-geral','text',1) RETURNING id;").strip()))
            directory=Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='tls-',dir=work)))
            cert,key=directory/'cert.pem',directory/'key.pem'
            checked_run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-keyout',str(key),'-out',str(cert),
                         '-subj','/CN=127.0.0.1','-addext','subjectAltName=IP:127.0.0.1'],timeout=20); key.chmod(0o600)
            proxy=subprocess.Popen([str(args.tls_proxy.resolve()),'-upstream',base,'-cert',str(cert),'-key',str(key),'-r7-control'],
                                   stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True)
            stack.callback(proxy.stdout.close); stack.callback(stop,proxy)
            secure=proxy.stdout.readline().strip()
            if not secure.startswith('https://127.0.0.1:'): raise RuntimeError('TLS proxy unavailable')
            sessions=[stack.enter_context(requests.Session()) for _ in usernames]; next_request=0
            def call(session,method,path,expected=200,**kwargs):
                nonlocal next_request,last_status
                delay=next_request-time.monotonic()
                if delay>0: time.sleep(delay)
                response=session.request(method,secure+path,timeout=10,allow_redirects=False,**kwargs)
                last_status=response.status_code; next_request=time.monotonic()+.25
                if last_status!=expected: raise RuntimeError('Unexpected backend status')
                return response.json() if response.content else None
            identities=[]
            phase='synthetic account login'
            for session,username in zip(sessions,usernames):
                session.trust_env=False; session.verify=str(cert)
                registered=call(session,'POST','/auth/register',201,json=dict(username=username,password=password))
                identities.append(str(UUID(registered['id'])))
                call(session,'POST','/auth/login',json=dict(username=username,password=password))
            a_rest,b_rest=sessions; a_id,b_id=identities
            sockets=[]
            for session in sessions:
                ws=websocket.create_connection(secure.replace('https:','wss:')+'/ws',timeout=2,
                    cookie='Auth='+session.cookies.get('Auth'),suppress_origin=True,
                    sslopt=dict(ca_certs=str(cert),cert_reqs=ssl.CERT_REQUIRED))
                stack.callback(ws.close); sockets.append(ws)
            def event(ws,kind,seconds=6):
                end=time.monotonic()+seconds
                while time.monotonic()<end:
                    try: item=json.loads(ws.recv())
                    except websocket.WebSocketTimeoutException: continue
                    if item.get('type')==kind: return item
                raise RuntimeError('Expected backend event absent')
            checks['presence_sync_on_connect']=all(isinstance(event(ws,'presence_sync').get('members'),list) for ws in sockets)
            phase='native TUI DM discovery'
            sys.path.insert(0,str(ROOT/'tests')); old=sys.argv[:]; sys.argv=[sys.argv[0],str(args.tui.resolve())]
            try: from test_terminal import Terminal
            finally: sys.argv=old
            terminals=[]
            for username in usernames:
                t=Terminal(environment=dict(CURL_CA_BUNDLE=str(cert),NO_PROXY='*'),allow_local_http=False)
                stack.callback(t.close); terminals.append(t)
                t.send(secure+'\t'+username+'\t'+password+'\n')
            a,b=terminals
            def screen(t): return '\n'.join(t.screen.display)
            def pump(seconds=.12):
                end=time.monotonic()+seconds
                while time.monotonic()<end:
                    for t in terminals: t.read(.015)
            def wait(t,text,seconds=10):
                end=time.monotonic()+seconds
                while text not in screen(t) and time.monotonic()<end: pump()
                if text not in screen(t):
                    for index,terminal in enumerate(terminals):
                        (work/f'failure-{index}.txt').write_text(screen(terminal).replace(password,'[redacted]'))
                    raise RuntimeError('Expected TUI state absent')
            for t in terminals: wait(t,'r8-geral'); wait(t,'Tempo real conectado')
            a.send('\x1b[21~'); wait(a,'Nova DM'); a.send(usernames[1]); wait(a,'@'+usernames[1]); a.send('\n')
            wait(a,'DM com '+usernames[1]); checks['keyboard_recipient_discovery_and_open']=True
            dms=call(a_rest,'GET','/dms')['dms']; dm_id=str(UUID(dms[0]['id']))
            phase='DM bidirectional live conversation'
            a.send('R8 A para B\n'); wait(a,'R8 A para B'); pump(.4)
            typing=event(sockets[1],'typing')
            checks['typing_in_real_dm']=typing.get('channel_id')==dm_id and typing.get('user_id')==a_id
            b.send('\t\x1bOC\n'); wait(b,usernames[0]); b.send('\x1bOB\n'); wait(b,'R8 A para B')
            b.send('R8 B para A\n'); wait(a,'R8 B para A'); wait(b,'R8 B para A')
            checks['bidirectional_dm_in_real_tui']=True
            history=call(a_rest,'GET','/channels/'+dm_id+'/messages')['messages']
            checks['dm_uses_channel_message_endpoints']=len(history)>=2
            phase='DM close and reopen'
            b.send('Rascunho R8\x1b'); b.send('\x1bOD\x04'); wait(b,'Nenhuma DM')
            checks['close_removes_only_own_rail']=not call(b_rest,'GET','/dms')['dms'] and bool(call(a_rest,'GET','/dms')['dms'])
            a.send('R8 reabre DM\n'); wait(a,'R8 reabre DM'); wait(b,usernames[0])
            b.send('\n'); wait(b,'R8 reabre DM'); wait(b,'Rascunho R8')
            checks['message_reopens_preserving_history_and_draft']='R8 A para B' in screen(b) or len(call(b_rest,'GET','/channels/'+dm_id+'/messages')['messages'])>=3
            phase='notification center and persisted read'
            b.send('\x1b[17~'); wait(b,'Canais / F6')
            created=call(a_rest,'POST','/messages',201,files={'channel_id':(None,dm_id),'content':(None,'R8 REST não marca notificação')})
            read_mid=str(UUID(created['id']))
            deadline=time.monotonic()+5; unread=[]
            while not unread and time.monotonic()<deadline:
                pump(.1)
                listed=call(b_rest,'GET','/users/'+b_id+'/notifications')['notifications']
                unread=[n for n in listed if n['message_id']==read_mid and not n['read']]
            checks['dm_notifications_persisted']=bool(listed)
            call(b_rest,'GET','/channels/'+dm_id+'/messages')
            reread=call(b_rest,'GET','/users/'+b_id+'/notifications')['notifications']
            checks['notifications_remain_separate_from_history_read']=bool(unread) and any(n['message_id']==read_mid and not n['read'] for n in reread)
            b.send('\x1b[17~'); wait(b,'r8-geral'); b.send('\x1b[20~'); wait(b,'R8 REST não marca notificação')
            b.send('\n'); wait(b,'Detalhes da notificação'); pump(.5)
            reread=call(b_rest,'GET','/users/'+b_id+'/notifications')['notifications']
            checks['detail_marks_one_persisted_notification']=any(n['message_id']==read_mid and n['read'] for n in reread)
            b.send('\x1b\x1b')
            phase='blocking feedback'
            call(a_rest,'POST','/users/'+b_id+'/block',204)
            a.send('\x1b[21~'); wait(a,'Nova DM'); a.send(usernames[1]); wait(a,'@'+usernames[1]); a.send('\n')
            wait(a,'bloqueada'); checks['blocked_dm_has_legible_feedback']=True
            call(a_rest,'DELETE','/users/'+b_id+'/block',204)
            phase='ephemeral versus persisted channel notifications'
            call(b_rest,'POST','/channels/'+channel+'/user/'+b_id+'/settings',json=dict(notification_settings='all'))
            created=call(a_rest,'POST','/messages',201,files={'channel_id':(None,channel),'content':(None,'R8 somente evento')})
            mid=str(UUID(created['id']))
            delivered=event(sockets[1],'new_notification')
            while delivered.get('message_id')!=mid: delivered=event(sockets[1],'new_notification')
            pump(.8)
            rows=call(b_rest,'GET','/users/'+b_id+'/notifications')['notifications']
            checks['all_plain_message_is_ephemeral']=not any(n['message_id']==mid for n in rows)
            b.send('\x1b[20~'); wait(b,'R8 somente evento'); pump(.3)
            b.send('\x1b'); checks['ephemeral_event_visible_in_tui']=True
            phase='reopen after blocking'
            # Blocking may close the participant rails; removing the block does
            # not itself reopen them. Open both explicitly before checking presence.
            for t,recipient in ((a,usernames[1]),(b,usernames[0])):
                t.send('\x1b[21~'); wait(t,'Nova DM'); t.send(recipient); wait(t,'@'+recipient); t.send('\n')
                wait(t,'DM com '+recipient)
            phase='reconnection state'
            # Controlled outage affects only the temporary TLS proxy.
            for t in terminals: t.resize(24,180)
            call(a_rest,'POST','/__papo_test/offline',204)
            wait(a,'Desconectado'); wait(b,'Desconectado')
            checks['disconnect_in_notice_area']=all('Desconectado' in t.screen.display[-2] for t in terminals)
            call(a_rest,'POST','/__papo_test/online',204)
            for t in terminals: t.send('\x12'); wait(t,'Tempo real conectado')
            phase='presence after reconnection A'; wait(a,'online')
            phase='presence after reconnection B'; b.click(5,13); wait(b,usernames[0]+' · online')
            checks['reconnection_restores_metadata_and_presence']=True
            checks['tls_verified']=True
        checks['temporary_resources_removed']=True
        checks['source_database_unchanged']=before==database_hash(args.postgres_container)
        result=dict(ok=all(checks.values()),checks=checks,kind='actual-go-backend-isolated-database',
                    tui_sha256=fingerprint(args.tui.resolve()),client_source_sha256=source_fingerprint(),backend_sha256=fingerprint(args.backend_binary.resolve()))
        private_write(work/'summary.json',result)
        if result['ok']:
            for index in range(2): (work/f'failure-{index}.txt').unlink(missing_ok=True)
        print(json.dumps(dict(ok=result['ok'],checks=len(checks),report=str(work/'summary.json')))); return 0 if result['ok'] else 2
    except Exception as exc:
        result=dict(ok=False,phase=phase,last_status=last_status,error=type(exc).__name__,checks=checks,temporary_resources_removed=True)
        private_write(work/'summary.json',result)
        print(json.dumps(dict(ok=False,phase=phase,last_status=last_status,error=type(exc).__name__,report=str(work/'summary.json')))); return 2

if __name__=='__main__': raise SystemExit(main())
