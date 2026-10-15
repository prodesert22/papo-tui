/* Exercise decoded REST/WS model transitions without sockets. Direct access
 * selects the response owner normally established by inbox_dispatch(). */
#include "../src/model/inbox.c"
#include <assert.h>

static const char *channel_id="00000000-0000-4000-8000-000000000010";
static const char *other_id="00000000-0000-4000-8000-000000000011";
static const char *author_id="00000000-0000-4000-8000-000000000002";
static const char *stamp="2026-10-15T12:00:00Z";
static const char *last_id="00000000-0000-4000-8000-000000000099";
static json_t *record(unsigned serial,const char *channel)
{
    char id[37],message[37];
    snprintf(id,sizeof(id),"00000000-0000-4000-8000-%012u",4000+serial);
    snprintf(message,sizeof(message),"00000000-0000-4000-8000-%012u",5000+serial);
    return json_pack("{s:s,s:s,s:s,s:s,s:s,s:b,s:s}","id",id,"message_id",message,
        "channel_id",channel,"author_id",author_id,"message_content","Old notification",
        "read",false,"created_at",stamp);
}
static void page(inbox *box,enum operation kind,json_t *records)
{
    if (kind==RECENT) {
        json_decref(box->fresh); box->fresh=json_array(); box->refresh_target=100;
    }
    box->pending=kind;
    json_t *body=json_pack("{s:o,s:b}","notifications",records,"has_more",false);
    assert(body && inbox_response(box,body,200,false)); json_decref(body);
}
static void one(inbox *box,enum operation kind,unsigned serial)
{
    json_t *records=json_array(); assert(records);
    assert(!json_array_append_new(records,record(serial,channel_id))); page(box,kind,records);
}
static void message(inbox *box,unsigned serial)
{
    char id[37]; snprintf(id,sizeof(id),"00000000-0000-4000-8000-%012u",5000+serial);
    json_t *event=json_pack("{s:s,s:s,s:s,s:s,s:s,s:s}","type","message","id",id,
        "channel_id",channel_id,"author_id",author_id,"content","New activity","created_at",stamp);
    assert(event && inbox_event(box,event)); json_decref(event);
}
static void notification_event(inbox *box,unsigned serial)
{
    char id[37],mid[37];
    snprintf(id,sizeof(id),"00000000-0000-4000-8000-%012u",9000+serial);
    snprintf(mid,sizeof(mid),"00000000-0000-4000-8000-%012u",5000+serial);
    json_t *event=json_pack("{s:s,s:s,s:s,s:s,s:s}","type","new_notification","id",id,
        "message_id",mid,"user_id",author_id,"message_content","Event preview");
    assert(event && inbox_event(box,event)); json_decref(event);
}
int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT)==CURLE_OK);
    auth_client *auth=auth_create(); assert(auth);
    inbox *box=inbox_create(auth); assert(box);
    /* Old notifications outside the visible history no longer pin the local
     * badge. They remain unread in the central and never enter a read batch. */
    one(box,RECENT,1);
    assert(inbox_conversation_notified(box,channel_id));
    inbox_acknowledge_conversation(box,channel_id,last_id);
    assert(inbox_conversation_acknowledged(box,channel_id,last_id));
    assert(!inbox_conversation_acknowledged(box,other_id,last_id));
    assert(!inbox_conversation_acknowledged(box,channel_id,author_id));
    assert(inbox_unread(box)==1 && !json_array_size(box->reads) && !json_object_size(box->viewed));
    uint64_t revision=inbox_revision(box);
    inbox_acknowledge_conversation(box,channel_id,last_id);
    assert(inbox_revision(box)==revision); /* No endless redraw loop. */
    one(box,RECENT,1);
    assert(inbox_conversation_acknowledged(box,channel_id,last_id));
    /* Loading older notifications must not restore a cleared channel badge. */
    one(box,OLDER,0);
    assert(inbox_conversation_acknowledged(box,channel_id,last_id));
    /* A genuinely new notification, including REST recovery after downtime,
     * invalidates only the acknowledgement of its own conversation. */
    inbox_acknowledge_conversation(box,other_id,"");
    one(box,NEWER,2);
    assert(!inbox_conversation_acknowledged(box,channel_id,last_id));
    assert(inbox_conversation_acknowledged(box,other_id,""));
    inbox_acknowledge_conversation(box,channel_id,last_id);
    /* A newly received message relights the badge; its duplicate does not
     * relight it after the conversation is acknowledged. */
    message(box,3); assert(!inbox_conversation_acknowledged(box,channel_id,last_id));
    notification_event(box,3);
    inbox_acknowledge_conversation(box,channel_id,last_id);
    message(box,3); assert(inbox_conversation_acknowledged(box,channel_id,last_id));
    /* Delayed REST persistence of an already acknowledged event is local
     * metadata, not a new alert or permission to mark the message read. */
    one(box,NEWER,3);
    assert(inbox_conversation_acknowledged(box,channel_id,last_id));
    assert(!json_array_size(box->reads) && !json_object_size(box->viewed));
    one(box,RECENT,3);
    assert(inbox_conversation_acknowledged(box,channel_id,last_id));
    one(box,NEWER,4);
    assert(!inbox_conversation_acknowledged(box,channel_id,last_id));
    inbox_reset(box);
    assert(!inbox_conversation_acknowledged(box,channel_id,last_id));
    /* Reading the active channel before the initial metadata page arrives
     * must also suppress that initial backlog (including empty histories). */
    inbox_acknowledge_conversation(box,channel_id,""); one(box,RECENT,1);
    assert(inbox_conversation_acknowledged(box,channel_id,""));
    one(box,NEWER,2); assert(!inbox_conversation_acknowledged(box,channel_id,""));
    inbox_destroy(box); auth_destroy(auth); curl_global_cleanup();
    puts("Conversation badges: stale metadata, old notifications, delayed events, new activity and reset passed");
    return 0;
}
