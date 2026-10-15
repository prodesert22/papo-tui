#define _POSIX_C_SOURCE 200809L
#include "model/inbox.h"
#include "model/history.h"
#include "net/session_time.h"
#include "net/clock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum operation { NONE, DMS, RECENT, OLDER, NEWER, OPEN, CLOSE, READ, RESOLVE };
struct inbox {
    auth_client *auth;
    json_t *dms, *notifications, *events, *presence, *seen, *viewed, *messages, *alerts, *reads, *batch, *journal, *fresh, *unseen;
    enum operation pending, action;
    bool want_dms, want_recent, want_newer, want_older, more, read_failed, presence_ready, presence_partial;
    char oldest_time[65], oldest_id[37], newest_time[65], newest_id[37];
    char request_id[37], action_id[37], opened[37], notice[192];
    char refresh_id[37],refresh_time[65];
    size_t refresh_target;
    uint64_t revision, next_refresh;
};
static const char *s(const json_t *o, const char *k)
{ const char *v = json_string_value(json_object_get(o, k)); return v ? v : ""; }
static bool uuid_field(const json_t *o, const char *k)
{ json_t *v = json_object_get(o, k); return json_is_string(v) && json_string_length(v) == 36 && history_uuid(s(o,k)); }
static bool text_field(const json_t *o, const char *k, size_t max, bool optional)
{
    json_t *v = json_object_get(o,k);
    if (optional && (!v || json_is_null(v))) return true;
    return json_is_string(v) && json_string_length(v) <= max && strlen(s(o,k)) == json_string_length(v);
}
static size_t find(json_t *array, const char *id)
{ size_t i; json_t *v; json_array_foreach(array,i,v) if (!strcmp(s(v,"id"),id)) return i; return SIZE_MAX; }
static bool bounded_key(json_t *map, const char *key, json_t *value)
{
    if (!json_object_get(map,key) && json_object_size(map) >= 4096) {
        const char *first = json_object_iter_key(json_object_iter(map));
        if (first) json_object_del(map,first);
    }
    return json_object_set(map,key,value)==0;
}
static size_t bytes(json_t *v)
{ char *data=json_dumps(v,JSON_COMPACT); if (!data) return SIZE_MAX; size_t n=strlen(data); free(data); return n; }
static bool trim(json_t *array, size_t count, size_t limit, bool oldest)
{
    while (json_array_size(array)>count || bytes(array)>limit) {
        size_t n=json_array_size(array);
        if (!n) return false;
        if (json_array_remove(array,oldest ? n-1 : 0)) return false;
    }
    return true;
}
static bool upsert(json_t *array, json_t *value)
{
    size_t i=find(array,s(value,"id"));
    return (i==SIZE_MAX ? json_array_append(array,value) : json_array_set(array,i,value))==0;
}
static int compare_time(const char *a,const char *b)
{
    uint64_t am=0,bm=0; session_time_parse(a,&am); session_time_parse(b,&bm);
    if (am/1000!=bm/1000) return am/1000>bm/1000 ? 1 : -1;
    if (strlen(a)<19 || strlen(b)<19) return 0;
    const char *af=a+19,*bf=b+19;
    if (*af=='.') ++af;
    if (*bf=='.') ++bf;
    while ((*af>='0' && *af<='9') || (*bf>='0' && *bf<='9')) {
        char ac=*af>='0' && *af<='9' ? *af++ : '0',bc=*bf>='0' && *bf<='9' ? *bf++ : '0';
        if (ac!=bc) return ac>bc ? 1 : -1;
    }
    return 0;
}
static int recent_compare(const void *ap, const void *bp)
{
    json_t *a=*(json_t *const *)ap,*b=*(json_t *const *)bp;
    const char *at=s(a,"created_at"),*bt=s(b,"created_at");
    if (json_is_true(json_object_get(a,"_dm")) && *s(json_object_get(a,"last_message"),"created_at")) at=s(json_object_get(a,"last_message"),"created_at");
    if (json_is_true(json_object_get(b,"_dm")) && *s(json_object_get(b,"last_message"),"created_at")) bt=s(json_object_get(b,"last_message"),"created_at");
    int time=compare_time(at,bt);
    if (time) return -time;
    return -strcmp(s(a,"id"),s(b,"id"));
}
static bool sort(json_t *array)
{
    size_t n=json_array_size(array);
    json_t **items=calloc(n ? n : 1,sizeof(*items));
    if (!items) return false;
    for (size_t i=0;i<n;++i) items[i]=json_incref(json_array_get(array,i));
    qsort(items,n,sizeof(*items),recent_compare);
    bool ok=true;
    for (size_t i=0;i<n;++i) { if (json_array_set(array,i,items[i])) ok=false; json_decref(items[i]); }
    free(items); return ok;
}
static void notice(inbox *b, const char *text)
{ snprintf(b->notice,sizeof(b->notice),"%s",text); ++b->revision; }
inbox *inbox_create(auth_client *auth)
{
    inbox *b=calloc(1,sizeof(*b));
    if (!b) return NULL;
    b->auth=auth;
    b->dms=json_array(); b->notifications=json_array(); b->events=json_array(); b->presence=json_object();
    b->seen=json_object(); b->viewed=json_object(); b->messages=json_object(); b->alerts=json_array();
    b->reads=json_array(); b->journal=json_array();
    b->unseen=json_object();
    if (!b->dms || !b->notifications || !b->events || !b->presence || !b->seen || !b->viewed ||
        !b->messages || !b->alerts || !b->reads || !b->journal || !b->unseen) { inbox_destroy(b); return NULL; }
    b->want_dms=b->want_recent=true;
    return b;
}
void inbox_destroy(inbox *b)
{
    if (!b) return;
    json_decref(b->dms); json_decref(b->notifications); json_decref(b->events); json_decref(b->presence);
    json_decref(b->seen); json_decref(b->viewed); json_decref(b->messages); json_decref(b->alerts);
    json_decref(b->reads); json_decref(b->batch); json_decref(b->journal); json_decref(b->fresh); json_decref(b->unseen); free(b);
}
void inbox_reset(inbox *b)
{
    json_array_clear(b->dms); json_array_clear(b->notifications); json_array_clear(b->events);
    json_array_clear(b->alerts); json_array_clear(b->reads); json_array_clear(b->journal);
    json_object_clear(b->presence); json_object_clear(b->seen); json_object_clear(b->viewed); json_object_clear(b->messages);
    json_object_clear(b->unseen); b->presence_partial=false;
    json_decref(b->batch); b->batch=NULL; b->pending=b->action=NONE;
    json_decref(b->fresh); b->fresh=NULL; b->refresh_id[0]=b->refresh_time[0]=0;
    b->oldest_id[0]=b->newest_id[0]=b->oldest_time[0]=b->newest_time[0]=b->opened[0]=b->notice[0]=0;
    b->want_older=b->want_newer=b->read_failed=b->more=b->presence_ready=false;
    b->want_dms=b->want_recent=true; b->next_refresh=0; ++b->revision;
}
static bool dm_update(inbox *b, const json_t *dm)
{
    json_t *user=json_object_get(dm,"user"),*unread=json_object_get(dm,"unread_count");
    uint64_t time;
    if (!uuid_field(dm,"id") || !uuid_field(user,"id") || !text_field(user,"username",1024,false) ||
        !text_field(user,"nickname",1024,true) || !session_time_parse(s(dm,"created_at"),&time) ||
        !json_is_integer(unread) || json_integer_value(unread)<0) return false;
    json_t *copy=json_deep_copy(dm);
    const char *name=*s(user,"nickname") ? s(user,"nickname") : s(user,"username");
    bool ok=copy && !json_object_set_new(copy,"type",json_string("text")) &&
        !json_object_set_new(copy,"name",json_string(name)) && !json_object_set_new(copy,"_dm",json_true()) && upsert(b->dms,copy);
    json_decref(copy);
    return ok && sort(b->dms) && trim(b->dms,500,4U*1024U*1024U,true);
}
static void queue_read(inbox *b, json_t *n)
{
    if (json_is_true(json_object_get(n,"read"))) return;
    const char *id=s(n,"id"); size_t i; json_t *v;
    json_array_foreach(b->reads,i,v) if (!strcmp(json_string_value(v),id)) return;
    if (json_array_size(b->reads)<1000) json_array_append_new(b->reads,json_string(id));
}
static bool notification_valid(json_t *n)
{
    uint64_t time;
    return uuid_field(n,"id") && uuid_field(n,"message_id") && uuid_field(n,"channel_id") &&
        text_field(n,"author_id",36,true) && (!*s(n,"author_id") || history_uuid(s(n,"author_id"))) &&
        text_field(n,"message_content",2048,false) && json_is_boolean(json_object_get(n,"read")) &&
        text_field(n,"created_at",64,false) && session_time_parse(s(n,"created_at"),&time);
}
static bool notifications_response(inbox *b,json_t *body,enum operation kind)
{
    json_t *page=json_object_get(body,"notifications"),*more=json_object_get(body,"has_more");
    size_t count=json_array_size(page);
    if (!json_is_array(page) || count>100 || !json_is_boolean(more)) return false;
    for (size_t i=0;i<count;++i) if (!notification_valid(json_array_get(page,i))) return false;
    json_t *last=count ? json_array_get(page,count-1) : NULL;
    if (last && ((kind==OLDER && !strcmp(s(last,"id"),b->oldest_id)) ||
                 (kind==NEWER && !strcmp(s(last,"id"),b->newest_id)))) return false;
    for (size_t i=0;i<count;++i) {
        json_t *n=json_array_get(page,i); json_t *copy=json_deep_copy(n);
        if (!copy || !upsert(kind==RECENT ? b->fresh : b->notifications,copy)) { json_decref(copy); return false; }
        json_decref(copy);
        bounded_key(b->messages,s(n,"message_id"),json_object_get(n,"channel_id"));
        for (size_t j=0;j<json_array_size(b->events);) {
            json_t *e=json_array_get(b->events,j);
            if (!strcmp(s(e,"id"),s(n,"id")) || !strcmp(s(e,"message_id"),s(n,"message_id"))) {
                if (json_is_true(json_object_get(e,"_viewed"))) bounded_key(b->viewed,s(n,"message_id"),json_true());
                json_array_remove(b->events,j);
            } else ++j;
        }
        if (json_object_get(b->viewed,s(n,"message_id"))) queue_read(b,n);
    }
    if (kind==RECENT) {
        if (count && json_is_true(more) && json_array_size(b->fresh)<b->refresh_target) {
            if (!strcmp(b->refresh_id,s(last,"id"))) return false;
            snprintf(b->refresh_id,sizeof(b->refresh_id),"%s",s(last,"id"));
            snprintf(b->refresh_time,sizeof(b->refresh_time),"%s",s(last,"created_at"));
            b->want_recent=true; return true;
        }
        json_decref(b->notifications); b->notifications=b->fresh; b->fresh=NULL;
        b->refresh_id[0]=b->refresh_time[0]=0;
        /* Reconciliation removes IDs that vanished or are already read. */
        for (size_t i=0;i<json_array_size(b->reads);) {
            size_t at=find(b->notifications,json_string_value(json_array_get(b->reads,i)));
            if (at==SIZE_MAX || json_is_true(json_object_get(json_array_get(b->notifications,at),"read"))) json_array_remove(b->reads,i);
            else ++i;
        }
        if (!json_array_size(b->reads)) b->read_failed=false;
    }
    if (!sort(b->notifications) || !trim(b->notifications,1000,4U*1024U*1024U,true)) return false;
    if (count) {
        json_t *newest=kind==NEWER ? last : json_array_get(b->notifications,0);
        int time=compare_time(s(newest,"created_at"),b->newest_time);
        if (!*b->newest_id || time>0 || (time==0 && strcmp(s(newest,"id"),b->newest_id)>0)) {
            snprintf(b->newest_id,sizeof(b->newest_id),"%s",s(newest,"id"));
            snprintf(b->newest_time,sizeof(b->newest_time),"%s",s(newest,"created_at"));
        }
        if (kind!=NEWER) {
            snprintf(b->oldest_id,sizeof(b->oldest_id),"%s",s(last,"id"));
            snprintf(b->oldest_time,sizeof(b->oldest_time),"%s",s(last,"created_at"));
        }
    }
    if (kind!=NEWER) b->more=count && json_is_true(more);
    else b->want_newer=count && json_is_true(more);
    return true;
}
static bool presence_set(json_t *map,const json_t *event)
{
    const char *state=s(event,"status");
    if (!uuid_field(event,"user_id") || (strcmp(state,"online") && strcmp(state,"offline") && strcmp(state,"away") && strcmp(state,"busy")) ||
        !text_field(event,"status_message",2048,true) || !text_field(event,"nickname",1024,true) || !text_field(event,"typing",256,true)) return false;
    json_t *prior=json_object_get(map,s(event,"user_id"));
    json_t *copy=prior ? json_deep_copy(prior) : json_object();
    if (!copy) return false;
    const char *fields[]={"user_id","status","status_message","nickname","typing"};
    bool ok=true;
    for (size_t i=0;i<5;++i) {
        json_t *value=json_object_get(event,fields[i]);
        if (value && json_object_set(copy,fields[i],value)) ok=false;
    }
    if (ok) ok=bounded_key(map,s(event,"user_id"),copy);
    json_decref(copy); return ok;
}
bool inbox_event(inbox *b,const json_t *event)
{
    const char *type=s(event,"type");
    if (!strcmp(type,"dm_update")) {
        bool ok=dm_update(b,json_object_get(event,"dm"));
        if (!ok) return false;
        if (b->pending==DMS && json_array_size(b->journal)<256) {
            json_array_append_new(b->journal,json_deep_copy(event));
            if (bytes(b->journal)>1024U*1024U) { json_array_remove(b->journal,json_array_size(b->journal)-1); b->want_dms=true; }
        }
        else if (b->pending==DMS) b->want_dms=true;
        ++b->revision; return ok;
    }
    if (!strcmp(type,"presence_sync")) {
        json_t *members=json_object_get(event,"members"),*next=json_object();
        if (!json_is_array(members) || json_array_size(members)>10000 || !next) { json_decref(next); return false; }
        size_t i; json_t *m;
        json_array_foreach(members,i,m) if (!presence_set(next,m)) { json_decref(next); return false; }
        b->presence_partial=json_array_size(members)>4096;
        json_decref(b->presence); b->presence=next; b->presence_ready=true; ++b->revision; return true;
    }
    if (!strcmp(type,"presence_update")) {
        if (!json_object_get(b->presence,s(event,"user_id")) && json_object_size(b->presence)>=4096) b->presence_partial=true;
        if (!presence_set(b->presence,event)) return false;
        ++b->revision; return true;
    }
    if (!strcmp(type,"message")) {
        uint64_t time;
        if (!uuid_field(event,"id") || !uuid_field(event,"channel_id") || !text_field(event,"content",32768,true) ||
            !text_field(event,"created_at",64,false) || !session_time_parse(s(event,"created_at"),&time) ||
            !text_field(event,"author_id",36,true) || (*s(event,"author_id") && !history_uuid(s(event,"author_id")))) return false;
        bounded_key(b->messages,s(event,"id"),json_object_get(event,"channel_id"));
        if (strcmp(s(event,"author_id"),auth_user_id(b->auth)) && !json_object_get(b->viewed,s(event,"id"))) {
            json_t *prior=json_object_get(b->unseen,s(event,"channel_id"));
            if (!prior || compare_time(s(event,"created_at"),s(prior,"created_at"))>=0) {
                json_t *mark=json_pack("{s:s,s:s}","id",s(event,"id"),"created_at",s(event,"created_at"));
                if (mark) bounded_key(b->unseen,s(event,"channel_id"),mark);
                json_decref(mark);
            }
        }
        for (size_t i=0;i<json_array_size(b->dms);++i) {
            json_t *dm=json_array_get(b->dms,i);
            if (!strcmp(s(dm,"id"),s(event,"channel_id"))) {
                json_t *last=json_pack("{s:O,s:O,s:O,s:O}","id",json_object_get(event,"id"),"created_at",json_object_get(event,"created_at"),
                    "author_id",json_object_get(event,"author_id") ? json_object_get(event,"author_id") : json_null(),
                    "content",json_object_get(event,"content") ? json_object_get(event,"content") : json_null());
                if (!last) return false;
                json_object_set(dm,"last_message",last); json_decref(last);
                sort(b->dms); trim(b->dms,500,4U*1024U*1024U,true); ++b->revision; break;
            }
        }
        return true;
    }
    if (strcmp(type,"new_notification")) return true;
    if (!uuid_field(event,"id") || !uuid_field(event,"message_id") || !text_field(event,"user_id",36,true) ||
        (*s(event,"user_id") && !history_uuid(s(event,"user_id"))) || !text_field(event,"message_content",2048,false)) return false;
    if (json_object_get(b->seen,s(event,"id"))) return true;
    if (!bounded_key(b->seen,s(event,"id"),json_true())) return false;
    bool persisted=false;
    size_t i; json_t *n;
    json_array_foreach(b->notifications,i,n) if (!strcmp(s(n,"id"),s(event,"id")) || !strcmp(s(n,"message_id"),s(event,"message_id"))) {
        if (json_is_true(json_object_get(n,"read"))) return true;
        persisted=true; break;
    }
    json_t *copy=json_deep_copy(event);
    if (copy && json_object_get(b->viewed,s(event,"message_id"))) json_object_set_new(copy,"_viewed",json_true());
    if (!copy || (!persisted && json_array_append(b->events,copy)) || json_array_append(b->alerts,copy)) { json_decref(copy); return false; }
    json_decref(copy); trim(b->events,256,1024U*1024U,false); trim(b->alerts,256,1024U*1024U,false);
    b->want_newer=true; ++b->revision; return true;
}
void inbox_recover(inbox *b)
{ json_object_clear(b->presence); b->presence_ready=false; b->want_dms=b->want_recent=true; b->want_newer=*b->newest_id!=0; ++b->revision; }
void inbox_refresh(inbox *b)
{
    if (b->pending!=RECENT) { json_decref(b->fresh); b->fresh=NULL; b->refresh_id[0]=b->refresh_time[0]=0; }
    b->want_dms=b->want_recent=true; if (!b->read_failed) b->notice[0]=0;
}
void inbox_older(inbox *b) { if (b->more) b->want_older=true; }
void inbox_retry(inbox *b) { b->read_failed=false; inbox_refresh(b); }
bool inbox_pending(const inbox *b) { return b->pending!=NONE; }
void inbox_cancel_read(inbox *b)
{
    if (b->pending==DMS) b->want_dms=true;
    else if (b->pending==RECENT) {
        b->want_recent=true; json_decref(b->fresh); b->fresh=NULL; b->refresh_id[0]=b->refresh_time[0]=0;
    } else if (b->pending==OLDER) b->want_older=true;
    else if (b->pending==NEWER) b->want_newer=true;
    if (b->pending==DMS || b->pending==RECENT || b->pending==OLDER || b->pending==NEWER || b->pending==RESOLVE) b->pending=NONE;
}
bool inbox_open_dm(inbox *b,const char *user)
{
    if (!history_uuid(user) || !strcmp(user,auth_user_id(b->auth)) || b->action!=NONE || b->pending==OPEN || b->pending==CLOSE) return false;
    snprintf(b->action_id,sizeof(b->action_id),"%s",user); b->action=OPEN; notice(b,"Abrindo DM…"); return true;
}
bool inbox_close_dm(inbox *b,const char *id)
{
    if (!history_uuid(id) || b->action!=NONE || b->pending==OPEN || b->pending==CLOSE) return false;
    snprintf(b->action_id,sizeof(b->action_id),"%s",id); b->action=CLOSE; notice(b,"Fechando DM…"); return true;
}
void inbox_resolve_dm(inbox *b,const char *id)
{ if (history_uuid(id) && b->action==NONE) { b->action=RESOLVE; snprintf(b->action_id,sizeof(b->action_id),"%s",id); } }
static bool notification_request(inbox *b,enum operation kind)
{
    char path[512]; const char *id=kind==RECENT ? b->refresh_id : kind==OLDER ? b->oldest_id : b->newest_id;
    const char *stamp=kind==RECENT ? b->refresh_time : kind==OLDER ? b->oldest_time : b->newest_time;
    char *escaped=*id ? curl_easy_escape(NULL,stamp,0) : NULL;
    if (*id && !escaped) return false;
    snprintf(path,sizeof(path),"/users/%s/notifications?order=%s",auth_user_id(b->auth),kind==NEWER ? "asc" : "desc");
    if (escaped) snprintf(path+strlen(path),sizeof(path)-strlen(path),"&since=%s&last_id=%s",escaped,id);
    curl_free(escaped); return auth_get_json(b->auth,path);
}
bool inbox_dispatch(inbox *b)
{
    if (b->pending!=NONE || !auth_has_session(b->auth) || auth_busy(b->auth)) return false;
    if (!history_uuid(auth_user_id(b->auth))) { notice(b,"Identidade incompatível com o serviço."); return false; }
    uint64_t now; if (!net_now_ms(&now)) return false;
    enum operation kind=b->action!=NONE ? b->action : (!b->read_failed && json_array_size(b->reads)) ? READ :
        b->want_dms ? DMS : b->want_recent ? RECENT : b->want_newer && now>=b->next_refresh ? NEWER : b->want_older ? OLDER : NONE;
    char path[192]; json_t *body=NULL; bool ok=false;
    if (kind==NONE) return false;
    snprintf(b->request_id,sizeof(b->request_id),"%s",b->action_id);
    if (kind==OPEN) {
        body=json_pack("{s:s}","user_id",b->action_id);
        if (body) ok=auth_mutate_json(b->auth,"POST","/dms",body);
    } else if (kind==CLOSE || kind==RESOLVE) {
        snprintf(path,sizeof(path),"/dms/%s",b->action_id);
        ok=kind==CLOSE ? auth_mutate_json(b->auth,"DELETE",path,NULL) : auth_get_json(b->auth,path);
    } else if (kind==READ) {
        json_decref(b->batch); b->batch=json_deep_copy(b->reads);
        body=b->batch ? json_pack("{s:O}","notification_ids",b->batch) : NULL;
        snprintf(path,sizeof(path),"/users/%s/read_notification",auth_user_id(b->auth));
        if (body) ok=auth_mutate_json(b->auth,"PUT",path,body);
    } else if (kind==DMS) ok=auth_get_json(b->auth,"/dms");
    else {
        if (kind==RECENT && !b->fresh) {
            b->fresh=json_array(); b->refresh_target=json_array_size(b->notifications);
            if (b->refresh_target<100) b->refresh_target=100;
        }
        if (kind!=RECENT || b->fresh) ok=notification_request(b,kind);
    }
    json_decref(body);
    if (!ok) { notice(b,"Não foi possível preparar a operação. Ctrl+R: tentar novamente."); return false; }
    b->pending=kind;
    if (kind==DMS) { b->want_dms=false; json_array_clear(b->journal); }
    if (kind==RECENT) { b->want_recent=false; if (!*b->newest_id) b->want_newer=false; }
    if (kind==NEWER) { b->want_newer=false; b->next_refresh=now+1000; }
    if (kind==OLDER) b->want_older=false;
    if (kind==OPEN || kind==CLOSE || kind==RESOLVE) b->action=NONE;
    return true;
}
bool inbox_response(inbox *b,json_t *body,long code,bool uncertain)
{
    enum operation kind=b->pending; b->pending=NONE; bool ok=false;
    if (kind==DMS && code==200) {
        json_t *dms=json_object_get(body,"dms"),*old=b->dms;
        if (json_is_array(dms) && json_array_size(dms)<=500) {
            b->dms=json_array(); ok=b->dms!=NULL;
            for (size_t i=0;ok && i<json_array_size(dms);++i) ok=dm_update(b,json_array_get(dms,i));
            if (!ok) { json_decref(b->dms); b->dms=old; } else json_decref(old);
        }
        for (size_t i=0;i<json_array_size(b->journal);++i) dm_update(b,json_object_get(json_array_get(b->journal,i),"dm"));
        json_array_clear(b->journal);
    } else if ((kind==RECENT || kind==OLDER || kind==NEWER) && code==200) ok=notifications_response(b,body,kind);
    else if ((kind==OPEN || kind==RESOLVE) && (code==200 || code==201)) {
        ok=dm_update(b,body);
        if (ok) snprintf(b->opened,sizeof(b->opened),"%s",s(body,"id"));
    } else if (kind==CLOSE && code==204) {
        size_t i=find(b->dms,b->request_id); if (i!=SIZE_MAX) json_array_remove(b->dms,i); b->want_dms=true; ok=true;
    } else if (kind==READ) {
        json_t *updated=json_object_get(body,"updated");
        ok=code==200 && json_is_integer(updated) && json_integer_value(updated)==(json_int_t)json_array_size(b->batch);
        if (ok) {
            size_t i; json_t *id;
            json_array_foreach(b->batch,i,id) {
                size_t at=find(b->notifications,json_string_value(id));
                if (at!=SIZE_MAX) json_object_set_new(json_array_get(b->notifications,at),"read",json_true());
                for (size_t j=0;j<json_array_size(b->reads);)
                    if (!strcmp(json_string_value(json_array_get(b->reads,j)),json_string_value(id))) json_array_remove(b->reads,j); else ++j;
            }
            json_t *n;
            json_array_foreach(b->notifications,i,n) if (json_object_get(b->viewed,s(n,"message_id"))) queue_read(b,n);
        } else { b->read_failed=true; b->want_recent=true; }
        json_decref(b->batch); b->batch=NULL;
    }
    if (!ok && kind==RECENT) { json_decref(b->fresh); b->fresh=NULL; b->refresh_id[0]=b->refresh_time[0]=0; b->want_recent=false; }
    if (ok && b->read_failed) { ++b->revision; return ok; }
    notice(b,ok ? "" : uncertain ? "Resultado incerto. Atualize antes de tentar novamente." :
        code==403 ? "Acesso recusado ou DM bloqueada." : code==404 ? "Conversa ou notificação indisponível." :
        code==429 ? "Limite de requisições. Aguarde e tente novamente." :
        kind==READ ? "Leitura não confirmada. Ctrl+R: tentar novamente." : "Falha ao carregar. Ctrl+R: tentar novamente.");
    if (uncertain && (kind==OPEN || kind==CLOSE)) b->want_dms=true;
    return ok;
}
size_t inbox_dm_count(const inbox *b) { return json_array_size(b->dms); }
json_t *inbox_dm(const inbox *b,size_t i) { return json_array_get(b->dms,i); }
const char *inbox_opened(const inbox *b) { return b->opened; }
void inbox_opened_clear(inbox *b) { b->opened[0]=0; }
size_t inbox_notification_count(const inbox *b) { return json_array_size(b->notifications); }
json_t *inbox_notification(const inbox *b,size_t i) { return json_array_get(b->notifications,i); }
size_t inbox_event_count(const inbox *b) { return json_array_size(b->events); }
json_t *inbox_notification_event(const inbox *b,size_t i) { return json_array_get(b->events,i); }
bool inbox_has_more(const inbox *b) { return b->more; }
size_t inbox_unread(const inbox *b)
{
    size_t n=0,i; json_t *v;
    json_array_foreach(b->notifications,i,v) if (!json_is_true(json_object_get(v,"read"))) ++n;
    json_array_foreach(b->events,i,v) if (!json_is_true(json_object_get(v,"_viewed"))) ++n;
    return n;
}
void inbox_view_message(inbox *b,const char *id)
{
    if (!history_uuid(id)) return;
    bool known=json_object_get(b->viewed,id)!=NULL;
    if (bounded_key(b->viewed,id,json_true()) && !known) ++b->revision;
    const char *channel; json_t *mark;
    json_object_foreach(b->unseen,channel,mark) if (!strcmp(s(mark,"id"),id)) {
        json_object_del(b->unseen,channel); ++b->revision; break;
    }
    size_t i; json_t *n;
    json_array_foreach(b->events,i,n) if (!strcmp(s(n,"message_id"),id) && !json_is_true(json_object_get(n,"_viewed"))) {
        json_object_set_new(n,"_viewed",json_true()); ++b->revision;
    }
    json_array_foreach(b->notifications,i,n) if (!strcmp(s(n,"message_id"),id)) queue_read(b,n);
}
void inbox_view(inbox *b,const char *id,bool event)
{
    json_t *n=json_array_get(event ? b->events : b->notifications,find(event ? b->events : b->notifications,id));
    if (!n) return;
    inbox_view_message(b,s(n,"message_id"));
    if (event) json_object_set_new(n,"_viewed",json_true()); else queue_read(b,n);
    ++b->revision;
}
void inbox_read_conversation(inbox *b,const char *id)
{
    for (size_t i=0;i<json_array_size(b->dms);++i) if (!strcmp(s(json_array_get(b->dms,i),"id"),id)) {
        b->want_dms=true; break;
    }
}
bool inbox_conversation_unseen(const inbox *b,const char *id) { return json_object_get(b->unseen,id)!=NULL; }
bool inbox_message_viewed(const inbox *b,const char *id) { return json_object_get(b->viewed,id)!=NULL; }
size_t inbox_unseen_conversations(const inbox *b) { return json_object_size(b->unseen); }
const char *inbox_notice(const inbox *b) { return b->notice; }
const char *inbox_presence(const inbox *b,const char *id)
{ const char *state=s(json_object_get(b->presence,id),"status"); return *state ? state : b->presence_ready && !b->presence_partial ? "offline" : "unknown"; }
const char *inbox_message_channel(const inbox *b,const char *id)
{ const char *value=json_string_value(json_object_get(b->messages,id)); return value ? value : ""; }
json_t *inbox_take_alert(inbox *b)
{ json_t *e=json_array_get(b->alerts,0); if (!e) return NULL; json_incref(e); json_array_remove(b->alerts,0); return e; }
uint64_t inbox_revision(const inbox *b) { return b->revision; }
