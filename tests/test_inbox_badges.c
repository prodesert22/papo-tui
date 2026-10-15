#define _POSIX_C_SOURCE 200809L
/* Decode actual channel/DM cursor shapes and exercise model transitions without
 * HTTP sockets. A session fixture permits injecting normal WS message events. */
#include "net/auth.h"
static bool fixture_session(const auth_client *client) { return client!=NULL; }
static const char *fixture_url="",*fixture_user="";
static const char *fixture_instance(const auth_client *client) { (void)client; return fixture_url; }
static const char *fixture_identity(const auth_client *client) { (void)client; return fixture_user; }
#define auth_has_session fixture_session
#define auth_instance_url fixture_instance
#define auth_user_id fixture_identity
#include "../src/model/history.c"
#undef auth_has_session
#undef auth_instance_url
#undef auth_user_id
#include <assert.h>
#include <unistd.h>

static const char *channel_id="00000000-0000-4000-8000-000000000010";
static const char *author_id="00000000-0000-4000-8000-000000000002";
static const char *latest_id="00000000-0000-4000-8000-000000000103";
static const char *previous_id="00000000-0000-4000-8000-000000000102";
static const char *stamp="2026-10-15T12:00:00Z";
static json_t *conversation(const char *read)
{
    return json_pack("{s:s,s:s,s:s,s:i,s:{s:s,s:s},s:s*,s:s*,s:i}",
        "id",channel_id,"name","Fixture","type","text","position",1,
        "last_message","id",latest_id,"created_at",stamp,
        "last_read_message",read,"last_read_at",read ? "2026-10-15T12:01:00Z" : NULL,"unread_count",99);
}
static history *model(auth_client *auth,json_t *channel)
{
    history *h=history_create(auth); assert(h); history_enable_r8(h);
    json_t *body=json_pack("{s:[O]}","channels",channel);
    assert(body && channels_response(h,body)); json_decref(body);
    return h;
}
static json_t *message(const char *id)
{
    return json_pack("{s:s,s:s,s:s,s:s,s:s}","id",id,"channel_id",channel_id,
        "author_id",author_id,"content","Visible message","created_at",stamp);
}
static void persisted_read(auth_client *auth)
{
    char temporary[]="/tmp/papo-model-read-XXXXXX";
    assert(mkdtemp(temporary));
    const char *environment=getenv("XDG_STATE_HOME");
    char *previous=environment ? strdup(environment) : NULL;
    assert(!environment || previous);
    assert(setenv("XDG_STATE_HOME",temporary,1)==0);
    fixture_url="https://first.example.test/papo"; fixture_user=author_id;
    json_t *channel=conversation(NULL); assert(channel);
    json_object_set_new(channel,"last_read_message",json_null());
    json_object_set_new(channel,"last_read_at",json_null());
    history *h=model(auth,channel); assert(history_channel_unread(h,history_channel(h,0)));
    assert(history_select(h,0)); h->want_messages=false;
    json_t *body=json_pack("{s:s,s:[o],s:b}","channel_id",channel_id,"messages",message(latest_id),"has_more",false);
    assert(body && messages_response(h,body)); json_decref(body);
    assert(!history_channel_unread(h,history_channel(h,0)));
    history_destroy(h); /* Graceful close flushes without needing another poll. */
    h=model(auth,channel);
    assert(!history_channel_unread(h,history_channel(h,0)));
    assert(history_channel_unread(NULL,history_channel(h,0))); /* Backend still stale. */
    history_destroy(h);
    json_object_set_new(channel,"last_read_message",json_string(previous_id));
    h=model(auth,channel); assert(!history_channel_unread(h,history_channel(h,0))); history_destroy(h);
    fixture_url="https://second.example.test/papo";
    h=model(auth,channel); assert(history_channel_unread(h,history_channel(h,0))); history_destroy(h);
    fixture_url="https://first.example.test/papo"; fixture_user=channel_id;
    h=model(auth,channel); assert(history_channel_unread(h,history_channel(h,0))); history_destroy(h);
    fixture_user=author_id;
    json_object_set_new(json_object_get(channel,"last_message"),"id",json_string("00000000-0000-4000-8000-000000000104"));
    h=model(auth,channel);
    assert(history_channel_unread(h,history_channel(h,0))); /* New message while closed. */
    history_destroy(h); json_decref(channel);
    fixture_url=fixture_user="";
    char path[512];
    snprintf(path,sizeof(path),"%s/papo-tui/read-cursors.json",temporary); assert(unlink(path)==0);
    snprintf(path,sizeof(path),"%s/papo-tui/read-cursors.lock",temporary); assert(unlink(path)==0);
    snprintf(path,sizeof(path),"%s/papo-tui",temporary); assert(rmdir(path)==0);
    assert(rmdir(temporary)==0);
    assert((previous ? setenv("XDG_STATE_HOME",previous,1) : unsetenv("XDG_STATE_HOME"))==0);
    free(previous);
}
int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    auth_client *auth=auth_create(); assert(auth);
    /* Matching UUID is authoritative despite old notifications, stale counts
     * or a different last_read_at. This must also hold in a fresh process. */
    json_t *channel=conversation(latest_id); assert(channel);
    assert(!history_channel_unread(NULL,channel));
    json_object_set_new(channel,"last_read_at",json_string("2026-10-15T11:59:00Z"));
    assert(!history_channel_unread(NULL,channel));
    history *first=model(auth,channel),*restart=model(auth,channel);
    json_t *event=json_pack("{s:s,s:s,s:s,s:s,s:s}","type","new_notification",
        "id","00000000-0000-4000-8000-000000004001","message_id",previous_id,
        "user_id",author_id,"message_content","Old notification");
    assert(event && inbox_event(history_inbox(first),event)); json_decref(event);
    assert(inbox_unread(history_inbox(first))==1);
    assert(!history_channel_unread(first,history_channel(first,0)));
    assert(!history_channel_unread(restart,history_channel(restart,0)));
    history_destroy(first); history_destroy(restart);
    json_object_set_new(channel,"last_read_message",json_string(previous_id));
    assert(history_channel_unread(NULL,channel)); /* Distinct ID, even equal timestamp. */
    json_object_set_new(channel,"last_read_message",json_null());
    json_object_set_new(channel,"last_read_at",json_null());
    assert(history_channel_unread(NULL,channel));
    json_object_set_new(channel,"last_read_at",json_string("2026-10-15T11:59:59Z"));
    assert(history_channel_unread(NULL,channel));
    json_object_set_new(channel,"last_read_at",json_string("2026-10-15T12:00:00Z"));
    assert(!history_channel_unread(NULL,channel));
    json_object_set_new(channel,"last_read_at",json_string("2026-10-15T08:00:01-04:00"));
    assert(!history_channel_unread(NULL,channel));
    json_object_set_new(json_object_get(channel,"last_message"),"created_at",json_string("2026-10-15T12:00:00.123457Z"));
    json_object_set_new(channel,"last_read_at",json_string("2026-10-15T12:00:00.123456Z"));
    assert(history_channel_unread(NULL,channel)); /* Preserve backend microseconds. */
    json_object_set_new(channel,"last_message",json_null());
    assert(!history_channel_unread(NULL,channel)); /* No latest message: no badge. */
    json_decref(channel);
    channel=conversation(previous_id);
    json_object_set_new(channel,"type",json_string("category"));
    assert(!history_channel_unread(NULL,channel)); json_decref(channel);

    /* A successful recent GET covers the latest message. A lagging metadata
     * response must not resurrect the badge during this session. */
    channel=conversation(previous_id); history *h=model(auth,channel);
    assert(history_select(h,0)); h->want_messages=false;
    json_t *body=json_pack("{s:s,s:[o],s:b}","channel_id",channel_id,"messages",message(latest_id),"has_more",false);
    assert(body && messages_response(h,body)); json_decref(body);
    assert(!history_channel_unread(h,history_channel(h,0)) && h->want_channels);
    assert(!strcmp(h->read_attempt,latest_id));
    history_read_visible(h,latest_id); assert(!*h->read_visible); /* No retry loop. */
    assert(!strcmp(str(history_channel(h,0),"last_read_message"),previous_id));
    body=json_pack("{s:[O]}","channels",channel);
    assert(body && channels_response(h,body)); json_decref(body);
    assert(!history_channel_unread(h,history_channel(h,0)));
    restart=model(auth,channel);
    assert(history_channel_unread(restart,history_channel(restart,0)));
    history_destroy(restart); /* No local cursor crosses models or sessions. */
    body=json_pack("{s:s,s:[o],s:b}","channel_id",channel_id,"messages",message(previous_id),"has_more",false);
    h->older=true;
    assert(body && messages_response(h,body)); json_decref(body);
    assert(!strcmp(str(json_object_get(h->read_messages,channel_id),"id"),latest_id));
    h->older=false;
    json_object_set_new(channel,"last_read_message",json_string(latest_id));
    body=json_pack("{s:[O]}","channels",channel);
    assert(body && channels_response(h,body)); json_decref(body);
    assert(!history_channel_unread(h,history_channel(h,0)) && history_message_count(h)==2);
    history_destroy(h);
    /* Reopening with this same backend snapshot stays read, without any local
     * acknowledgements, seen maps or serialized notification state. */
    h=model(auth,channel); assert(!history_channel_unread(h,history_channel(h,0)));
    assert(history_select(h,0));
    h->want_messages=false;
    event=message("00000000-0000-4000-8000-000000000104");
    json_object_set_new(event,"type",json_string("message"));
    assert(history_event(h,event,auth_context_generation(auth))); json_decref(event);
    assert(history_channel_unread(h,history_channel(h,0)));
    history_read_visible(h,"00000000-0000-4000-8000-000000000104");
    assert(*h->read_visible);
    history_read_visible(h,NULL); assert(!*h->read_visible); /* Scroll/focus cancellation. */
    h->failed=AUTHORS; history_read_visible(h,"00000000-0000-4000-8000-000000000104");
    assert(*h->read_visible); /* Author lookup failure must not block reading. */
    history_read_visible(h,NULL);
    h->failed=MESSAGES; history_read_visible(h,"00000000-0000-4000-8000-000000000104");
    assert(!*h->read_visible);
    h->failed=NONE; history_active(h,false);
    history_read_visible(h,"00000000-0000-4000-8000-000000000104"); assert(!*h->read_visible);
    history_destroy(h); json_decref(channel);

    /* Real DM response shape: nullable last_message and independent count. */
    json_t *dm=json_pack("{s:s,s:{s:s,s:s},s:s,s:O,s:s,s:s,s:i}","id",channel_id,
        "user","id",author_id,"username","Recipient","created_at",stamp,
        "last_message",json_null(),"last_read_message",latest_id,"last_read_at",stamp,"unread_count",99);
    h=history_create(auth); assert(h); history_enable_r8(h);
    event=json_pack("{s:s,s:O}","type","dm_update","dm",dm);
    assert(event && inbox_event(history_inbox(h),event)); json_decref(event);
    assert(!history_channel_unread(h,history_channel(h,0)));
    json_t *last=json_pack("{s:s,s:s}","id",latest_id,"created_at",stamp);
    json_object_set_new(dm,"last_message",last);
    event=json_pack("{s:s,s:O}","type","dm_update","dm",dm);
    assert(event && inbox_event(history_inbox(h),event)); json_decref(event);
    assert(!history_channel_unread(h,history_channel(h,0)));
    json_object_set_new(dm,"last_read_message",json_string(previous_id));
    event=json_pack("{s:s,s:O}","type","dm_update","dm",dm);
    assert(event && inbox_event(history_inbox(h),event)); json_decref(event);
    assert(history_channel_unread(h,history_channel(h,0)));
    assert(history_select(h,0)); h->want_messages=false;
    body=json_pack("{s:s,s:[o],s:b}","channel_id",channel_id,"messages",message(latest_id),"has_more",false);
    assert(body && messages_response(h,body)); json_decref(body);
    assert(!history_channel_unread(h,history_channel(h,0)));
    /* A stale dm_update also cannot resurrect a read conversation. */
    event=json_pack("{s:s,s:O}","type","dm_update","dm",dm);
    assert(event && inbox_event(history_inbox(h),event)); json_decref(event);
    assert(!history_channel_unread(h,history_channel(h,0)));
    assert(history_channel_unread(NULL,history_channel(h,0)));
    /* A generation change discards reading state not bound to an identity. */
    ++h->context; history_poll(h);
    assert(json_object_size(h->read_messages)==0);
    history_destroy(h); json_decref(dm);
    persisted_read(auth);
    auth_destroy(auth); curl_global_cleanup();
    puts("Cursor badges: backend startup, successful reads, stale metadata, older pages, author failures, DMs and context isolation passed");
    return 0;
}
