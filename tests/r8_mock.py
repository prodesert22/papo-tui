"""Synthetic R8 fixtures; no development backend observations."""
from urllib.parse import urlsplit, parse_qs
import time
from realtime_mock import RealtimeMock
from history_mock import uuid, CHANNEL, STAMP

DM=uuid(3000)
AUTHOR=uuid(2)

def dm():
    return dict(id=DM,user=dict(id=AUTHOR,username='bob',nickname='Bia'),created_at=STAMP,
                last_message=None,last_read_message=None,last_read_at=None,unread_count=2)

def notification(n):
    return dict(id=uuid(4000+n),message_id=uuid(5000+n),channel_id=CHANNEL,author_id=AUTHOR,
                message_content=f'Aviso {n:04d}',read=False,created_at=STAMP)

class R8Mock(RealtimeMock):
    def __init__(self,count=1,case='normal'):
        super().__init__(count=3)
        self.notifications=[notification(n) for n in range(1,count+1)]
        self.dms=[dm()]
        self.r8_case=case
        self.marked=[]
        self.opens=0
        self.read_calls=0
    def route(self,method,path,body):
        u=urlsplit(path)
        if u.path.endswith('/notifications') and method=='GET':
            if self.r8_case=='slow-notifications': time.sleep(.3)
            q=parse_qs(u.query); asc=q.get('order')==['asc']
            rows=sorted(self.notifications,key=lambda n:n['id'],reverse=not asc)
            if 'last_id' in q:
                assert q['since']==[STAMP]
                cursor=q['last_id'][0]
                rows=[n for n in rows if (n['id']>cursor if asc else n['id']<cursor)]
            return 200,dict(notifications=rows[:100],has_more=len(rows)>100)
        if u.path.endswith('/read_notification') and method=='PUT':
            self.read_calls+=1
            assert u.path.endswith('/users/'+uuid(1)+'/read_notification')
            ids=body['notification_ids']; assert 1<=len(ids)<=1000 and len(set(ids))==len(ids)
            self.marked.append(ids)
            if self.r8_case=='read-401' and self.read_calls==1: return 401,None
            if self.r8_case=='read-429': return 429,None
            updated=0
            for n in self.notifications:
                if n['id'] in ids: n['read']=True; updated+=1
            return (200,dict(updated=updated)) if updated else (404,None)
        if u.path.endswith('/dms'):
            if method=='GET': return 200,dict(dms=self.dms)
            if method=='POST':
                self.opens+=1
                if self.r8_case=='blocked': return 403,dict(type='https://papo.invalid/errors/dm-blocked')
                if self.r8_case=='open-401' and self.opens==1: return 401,None
                if not self.dms: self.dms=[dm()]
                return 200,self.dms[0]
        if '/dms/' in u.path:
            if method=='DELETE': self.dms=[]; return 204,None
            if method=='GET': return (200,self.dms[0]) if self.dms else (404,None)
        if u.path.endswith('/channels/'+DM+'/messages'):
            return 200,dict(channel_id=DM,messages=[dict(self.message(1),channel_id=DM,author_id=AUTHOR,content='Mensagem direta')],has_more=False)
        if u.path.endswith('/users') and method=='GET':
            return 200,dict(users=[dict(id=uuid(1),username='josé',created_at=STAMP),
                                  dict(id=AUTHOR,username='bob',nickname='Bia',created_at=STAMP),
                                  dict(id=uuid(3),username='banido',banned=True,created_at=STAMP)],has_more=False)
        return super().route(method,path,body)
