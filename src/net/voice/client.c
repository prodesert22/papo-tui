#include "net/voice/client.h"
#include "net/clock.h"
#include "net/voice/audio.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
struct voice_client {
    auth_client *auth;
    voice_audio *audio;
    json_t *rooms, *speakers, *mutes, *devices, *ice, *early_remote, *early_local, *outgoing;
    voice_state state;
    uint64_t context, ws_epoch, epoch, revision, deadline, token_time, speaking_until;
    double tokens, rms, peak;
    bool server_muted, requested_muted, remote_description, local_description, engine, speaking;
    char channel[37], next_channel[37], notice[192], pending[37];
    uint64_t pending_until;
};
static GList *clients;
static voice_client *active, *next_call, *tester;
static audio_config settings;
static bool configured, microphone = true, output = true, pressed;
static const char *str(const json_t *o, const char *key)
{
    const char *s = json_string_value(json_object_get(o, key));
    return s ? s : "";
}
bool voice_available(void)
{
    return true;
}
static bool uuid(const char *id)
{
    if (!id || strlen(id) != 36)
        return false;
    for (size_t i = 0; i < 36; ++i)
        if (i == 8 || i == 13 || i == 18 || i == 23 ? id[i] != '-' : !g_ascii_isxdigit(id[i]))
            return false;
    return true;
}
static bool includes(const json_t *ids, const char *id)
{
    size_t i;
    json_t *value;
    json_array_foreach(ids, i, value) if (json_is_string(value) &&
                                          !strcmp(json_string_value(value), id)) return true;
    return false;
}
static void remove_id(json_t *ids, const char *id)
{
    for (size_t i = json_array_size(ids); i > 0; --i)
        if (!strcmp(json_string_value(json_array_get(ids, i - 1)), id))
            json_array_remove(ids, i - 1);
}
static bool member(voice_client *v, const char *channel, const char *user, bool add)
{
    if (!uuid(channel) || !uuid(user))
        return false;
    json_t *ids = json_object_get(v->rooms, channel);
    if (!ids && add) {
        if (json_object_size(v->rooms) >= 2048 ||
            json_object_set_new(v->rooms, channel, json_array()) < 0)
            return false;
        ids = json_object_get(v->rooms, channel);
    }
    if (add && !includes(ids, user))
        return json_array_size(ids) < 4096 && json_array_append_new(ids, json_string(user)) == 0;
    if (!add) {
        remove_id(ids, user);
        remove_id(json_object_get(v->speakers, channel), user);
        json_t *mutes = json_object_get(v->mutes, channel);
        if (mutes)
            json_object_del(mutes, user);
    }
    return true;
}
/* Channel -> user -> bool. A missing entry means the server has not reported
 * that member's microphone state yet, which is different from unmuted. */
static bool set_mute(voice_client *v, const char *channel, const char *user, bool muted)
{
    if (!uuid(channel) || !uuid(user))
        return false;
    json_t *users = json_object_get(v->mutes, channel);
    if (!users) {
        if (json_object_size(v->mutes) >= 2048 ||
            json_object_set_new(v->mutes, channel, json_object()) < 0)
            return false;
        users = json_object_get(v->mutes, channel);
    }
    if (json_object_size(users) >= 4096 && !json_object_get(users, user))
        return false;
    return json_object_set(users, user, muted ? json_true() : json_false()) == 0;
}
static void prune_mutes(voice_client *v)
{
    json_t *mutes = json_object();
    if (!mutes)
        return;
    const char *channel;
    json_t *users;
    json_object_foreach(v->mutes, channel, users)
    {
        json_t *room = json_object_get(v->rooms, channel);
        if (!json_is_array(room))
            continue;
        json_t *kept = json_object();
        if (!kept) {
            json_decref(mutes);
            return;
        }
        const char *user;
        json_t *muted;
        json_object_foreach(users, user, muted) if (includes(room, user) &&
                                                    json_object_set(kept, user, muted) < 0)
        {
            json_decref(kept);
            json_decref(mutes);
            return;
        }
        if (json_object_size(kept))
            json_object_set_new(mutes, channel, kept);
        else
            json_decref(kept);
    }
    json_decref(v->mutes);
    v->mutes = mutes;
}
static bool send_event(voice_client *v, json_t *event)
{
    if (!event)
        return false;
    const json_t *muted = json_object_get(event, "muted");
    voice_audio_diagnostic("ws out %s muted=%s", str(event, "type"),
                           json_is_boolean(muted) ? (json_is_true(muted) ? "true" : "false") : "-");
    bool result = json_array_size(v->outgoing) < 128 &&
                  json_object_set_new(event, "channel_id", json_string(v->channel)) == 0 &&
                  json_array_append(v->outgoing, event) == 0;
    json_decref(event);
    return result;
}
static void stop_engine(voice_client *v)
{
    voice_audio_stop(v->audio);
    v->engine = false;
    ++v->epoch;
    v->rms = v->peak = -120;
    v->speaking = false;
    v->speaking_until = 0;
    v->remote_description = v->local_description = false;
    json_array_clear(v->early_local);
    json_array_clear(v->early_remote);
    if (tester == v)
        tester = NULL;
    pressed = false;
}
static void idle(voice_client *v)
{
    stop_engine(v);
    v->state = VOICE_IDLE;
    v->deadline = 0;
    v->channel[0] = 0;
    v->pending[0] = 0;
    v->pending_until = 0;
    json_array_clear(v->outgoing);
    json_decref(v->ice);
    v->ice = NULL;
    if (active == v)
        active = NULL;
    ++v->revision;
}
static void fail(voice_client *v, const char *notice)
{
    bool joined =
        v->state == VOICE_JOINING || v->state == VOICE_NEGOTIATING || v->state == VOICE_CONNECTED;
    bool leaving = v->state == VOICE_LEAVING;
    json_array_clear(v->outgoing);
    stop_engine(v);
    if (joined && send_event(v, json_pack("{s:s}", "type", "voice_leave"))) {
        uint64_t now = 0;
        net_now_ms(&now);
        v->state = VOICE_LEAVING;
        v->deadline = now + 5000;
    } else {
        if (joined || leaving)
            auth_recover_ws(v->auth);
        v->state = VOICE_FAILED;
        v->deadline = 0;
        if (active == v)
            active = NULL;
    }
    snprintf(v->notice, sizeof(v->notice), "%s", notice);
    v->pending[0] = 0;
    v->pending_until = 0;
    next_call = NULL;
    ++v->revision;
}
voice_client *voice_create(auth_client *auth)
{
    voice_client *v = g_try_new0(voice_client, 1);
    if (!v)
        return NULL;
    if (!configured) {
        audio_config_default(&settings);
        configured = true;
    }
    v->auth = auth;
    v->audio = voice_audio_create();
    v->rooms = json_object();
    v->speakers = json_object();
    v->mutes = json_object();
    v->devices = json_array();
    v->early_local = json_array();
    v->early_remote = json_array();
    v->outgoing = json_array();
    v->tokens = 20;
    v->rms = v->peak = -120;
    v->context = auth_context_generation(auth);
    v->ws_epoch = auth_ws_epoch(auth);
    if (!v->audio || !v->rooms || !v->speakers || !v->mutes || !v->devices || !v->early_local ||
        !v->early_remote || !v->outgoing) {
        voice_destroy(v);
        return NULL;
    }
    clients = g_list_prepend(clients, v);
    return v;
}
void voice_destroy(voice_client *v)
{
    if (!v)
        return;
    if (active == v) {
        json_t *event = json_pack("{s:s,s:s}", "type", "voice_leave", "channel_id", v->channel);
        if (event)
            auth_send_event(v->auth, event);
        json_decref(event);
        active = NULL;
    }
    if (next_call == v)
        next_call = NULL;
    if (tester == v)
        tester = NULL;
    clients = g_list_remove(clients, v);
    voice_audio_destroy(v->audio);
    json_decref(v->rooms);
    json_decref(v->speakers);
    json_decref(v->mutes);
    json_decref(v->devices);
    json_decref(v->ice);
    json_decref(v->early_local);
    json_decref(v->early_remote);
    json_decref(v->outgoing);
    g_free(v);
}
void voice_configure(const audio_config *config)
{
    if (!audio_config_valid(config))
        return;
    settings = *config;
    configured = true;
    pressed = false;
    for (GList *item = clients; item; item = item->next) {
        voice_client *v = item->data;
        voice_audio_transmit(v->audio, false);
        if (v->engine)
            voice_audio_configure(v->audio, config, tester == v);
        ++v->revision;
    }
}
/* The realtime connection and the permission snapshot may still be loading
 * when the member opens a voice channel. Those answers are not final: keep
 * the request and retry for a short while instead of denying the call. */
static bool postpone(voice_client *v, const char *channel)
{
    snprintf(v->pending, sizeof(v->pending), "%s", channel);
    uint64_t now = 0;
    net_now_ms(&now);
    v->pending_until = now + 10000;
    snprintf(v->notice, sizeof(v->notice), "Entrando na chamada de voz…");
    ++v->revision;
    return false;
}
static bool begin(voice_client *v, const char *channel)
{
    permission_decision decision =
        permissions_channel(auth_permissions(v->auth), channel, PERMISSION_CONNECT_VOICE);
    if (!auth_has_session(v->auth) || decision == PERMISSION_DENIED) {
        snprintf(v->notice, sizeof(v->notice), "Voz indisponível ou sem permissão de conexão.");
        v->pending[0] = 0;
        v->pending_until = 0;
        ++v->revision;
        return false;
    }
    if (auth_ws_state(v->auth) != WS_OPEN || decision != PERMISSION_ALLOWED)
        return postpone(v, channel);
    if (tester)
        voice_test(tester, false);
    stop_engine(v);
    json_array_clear(v->outgoing);
    v->state = VOICE_ICE;
    snprintf(v->channel, sizeof(v->channel), "%s", channel);
    v->pending[0] = 0;
    v->pending_until = 0;
    v->context = auth_context_generation(v->auth);
    v->ws_epoch = auth_ws_epoch(v->auth);
    v->notice[0] = 0;
    uint64_t now = 0;
    net_now_ms(&now);
    v->deadline = now + 30000;
    active = v;
    ++v->revision;
    return true;
}
bool voice_join(voice_client *v, const char *channel)
{
    if (!v || !uuid(channel))
        return false;
    if (active && active == v && !strcmp(v->channel, channel) && v->state != VOICE_LEAVING)
        return true;
    if (!active && *v->pending && !strcmp(v->pending, channel))
        return true;
    if (active) {
        snprintf(v->next_channel, sizeof(v->next_channel), "%s", channel);
        next_call = v;
        voice_leave(active);
        return true;
    }
    return begin(v, channel);
}
void voice_leave(voice_client *v)
{
    if (!v)
        return;
    if (*v->pending && v->state == VOICE_IDLE) {
        v->pending[0] = 0;
        v->pending_until = 0;
        v->notice[0] = 0;
        ++v->revision;
        return;
    }
    if (v->state == VOICE_IDLE || v->state == VOICE_FAILED || v->state == VOICE_LEAVING)
        return;
    if (v->state == VOICE_ICE) {
        idle(v);
        return;
    }
    stop_engine(v);
    json_array_clear(v->outgoing);
    if (!send_event(v, json_pack("{s:s}", "type", "voice_leave"))) {
        fail(v, "Não foi possível solicitar a saída da chamada.");
        return;
    }
    v->state = VOICE_LEAVING;
    uint64_t now = 0;
    net_now_ms(&now);
    v->deadline = now + 5000;
    ++v->revision;
}
bool voice_wants_ice(const voice_client *v)
{
    return v && v->state == VOICE_ICE;
}
void voice_ice_response(voice_client *v, const json_t *body, long status, uint64_t generation)
{
    if (!v || v->state != VOICE_ICE || generation != v->context)
        return;
    const json_t *servers = json_object_get(body, "ice_servers");
    if (status != 200 || !json_is_array(servers) || json_array_size(servers) > 32) {
        fail(v, "Não foi possível obter STUN/TURN. Entre novamente na chamada para tentar.");
        return;
    }
    json_decref(v->ice);
    v->ice = json_deep_copy(servers);
    if (!v->ice || !send_event(v, json_pack("{s:s}", "type", "voice_join"))) {
        fail(v, "Não foi possível iniciar a chamada.");
        return;
    }
    v->state = VOICE_JOINING;
    ++v->revision;
}
static bool speakers(voice_client *v, const char *channel, const json_t *ids)
{
    if (!uuid(channel) || !json_is_array(ids) || json_array_size(ids) > 4096)
        return false;
    size_t i;
    json_t *id;
    json_array_foreach(ids, i, id) if (!uuid(json_string_value(id))) return false;
    return json_object_set(v->speakers, channel, (json_t *)ids) == 0;
}
bool voice_event(voice_client *v, const json_t *event)
{
    const char *type = str(event, "type"), *channel = str(event, "channel_id"),
               *user = str(event, "user_id");
    if (!v)
        return false;
    if (!strncmp(type, "voice_", 6) || !strcmp(type, "active_speaker_update")) {
        const json_t *muted = json_object_get(event, "muted");
        voice_audio_diagnostic("ws in %s user=%s self=%d muted=%s", type, user,
                               *user && !strcmp(user, auth_user_id(v->auth)),
                               json_is_boolean(muted) ? (json_is_true(muted) ? "true" : "false")
                                                      : "-");
    }
    if (!strcmp(type, "presence_sync")) {
        const json_t *members = json_object_get(event, "members");
        if (!json_is_array(members) || json_array_size(members) > 10000)
            return false;
        json_t *old = v->rooms;
        v->rooms = json_object();
        if (!v->rooms) {
            v->rooms = old;
            return false;
        }
        size_t i, j;
        json_t *m, *id;
        bool ok = true;
        json_array_foreach(members, i, m)
        {
            json_t *channels = json_object_get(m, "user_voice");
            if (!channels)
                continue; /* Old instance: no authoritative directory before join. */
            if (!json_is_array(channels) || json_array_size(channels) > 64) {
                ok = false;
                break;
            }
            json_array_foreach(channels, j, id) if (!member(v, json_string_value(id),
                                                            str(m, "user_id"), true)) ok = false;
        }
        if (!ok) {
            json_decref(v->rooms);
            v->rooms = old;
            return false;
        }
        json_decref(old);
        json_object_clear(v->speakers);
        prune_mutes(v);
        ++v->revision;
        return true;
    }
    if (!strcmp(type, "voice_state_update")) {
        const json_t *value = json_object_get(event, "muted");
        if (!json_is_boolean(value) || !member(v, channel, user, true) ||
            !set_mute(v, channel, user, json_is_true(value)))
            return false;
        if (json_is_true(value))
            remove_id(json_object_get(v->speakers, channel), user);
        if (!strcmp(channel, v->channel) && !strcmp(user, auth_user_id(v->auth)))
            v->server_muted = json_is_true(value);
    } else if (!strcmp(type, "voice_leave")) {
        if (!member(v, channel, user, false))
            return false;
        if (!strcmp(channel, v->channel) && !strcmp(user, auth_user_id(v->auth)))
            idle(v);
    } else if (!strcmp(type, "active_speaker_update")) {
        if (!speakers(v, channel, json_object_get(event, "user_ids")))
            return false;
    } else if (!strcmp(type, "voice_joined")) {
        if (v->state != VOICE_JOINING || strcmp(channel, v->channel))
            return true;
        const json_t *members = json_object_get(event, "members");
        if (!json_is_array(members) || json_array_size(members) > 4096 ||
            json_object_set_new(v->rooms, channel, json_array()) < 0)
            return false;
        size_t i;
        json_t *m;
        json_object_del(v->mutes, channel);
        json_array_foreach(members, i, m)
        {
            const json_t *muted = json_object_get(m, "muted");
            if (!member(v, channel, str(m, "user_id"), true))
                return false;
            if (json_is_boolean(muted) &&
                !set_mute(v, channel, str(m, "user_id"), json_is_true(muted)))
                return false;
        }
        if (!speakers(v, channel, json_object_get(event, "active_speakers")))
            return false;
        v->server_muted = v->requested_muted = true;
        v->engine = voice_audio_start(v->audio, v->epoch, &settings,
                                      audio_config_slots(&settings, auth_instance_url(v->auth)),
                                      v->ice, false);
        if (!v->engine) {
            fail(v, "Não foi possível iniciar o áudio.");
            return true;
        }
        voice_audio_output(v->audio, output);
        v->state = VOICE_NEGOTIATING;
    } else if (!strcmp(type, "voice_offer") || !strcmp(type, "voice_answer")) {
        if (strcmp(channel, v->channel) || !v->engine || v->state == VOICE_LEAVING)
            return true;
        if (!voice_audio_description(v->audio, !strcmp(type, "voice_offer") ? "offer" : "answer",
                                     str(event, "sdp")))
            return false;
        v->remote_description = true;
        for (size_t i = 0; i < json_array_size(v->early_remote); ++i) {
            json_t *candidate = json_array_get(v->early_remote, i);
            if (!voice_audio_candidate(
                    v->audio,
                    (unsigned)json_integer_value(json_object_get(candidate, "sdp_mline_index")),
                    str(candidate, "candidate")))
                return false;
        }
        json_array_clear(v->early_remote);
    } else if (!strcmp(type, "voice_ice_candidate")) {
        if (strcmp(channel, v->channel) || !v->engine)
            return true;
        json_t *index = json_object_get(event, "sdp_mline_index");
        json_int_t line = json_integer_value(index);
        if ((index && !json_is_null(index) && !json_is_integer(index)) || line < 0 || line > 128 ||
            !*str(event, "candidate") || strlen(str(event, "candidate")) > 4096)
            return false;
        if (!v->remote_description) {
            if (json_array_size(v->early_remote) >= 128 ||
                json_array_append(v->early_remote, (json_t *)event) < 0)
                return false;
        } else if (!voice_audio_candidate(v->audio, (unsigned)line, str(event, "candidate")))
            return false;
    } else if (!strcmp(type, "error") && g_str_has_prefix(str(event, "code"), "voice-")) {
        voice_audio_diagnostic("ws error code=%s message=%s", str(event, "code"),
                               str(event, "message"));
        if (active != v)
            return true;
        if (v->state == VOICE_LEAVING && !strcmp(str(event, "code"), "voice-not-found"))
            idle(v);
        else
            fail(v, !strcmp(str(event, "code"), "voice-invalid-sdp")
                        ? "Negociação recusada. Confira os fluxos de áudio desta instância."
                        : "O servidor recusou a operação de voz. Confira permissões e "
                          "disponibilidade.");
    } else if (!strcmp(type, "voice_audio_routes"))
        return true; /* Slot IDs are not stable identities; UI uses the server's speaker IDs. */
    else
        return !strncmp(type, "voice_", 6);
    ++v->revision;
    return true;
}
static bool want_transmit(void)
{
    return microphone && !tester && (!settings.push_to_talk || pressed);
}
/* Servers rank only the loudest publishers, and the contract does not require
 * the local publisher to appear in active_speaker_update. Use the same local
 * level that feeds the microphone meter so your own name also turns green. */
#define SELF_SPEAKING_RMS (-50.0)
#define SELF_SPEAKING_HOLD_MS 400
static void update_speaking(voice_client *v, uint64_t now, bool audible)
{
    bool transmitting = v->state == VOICE_CONNECTED && want_transmit();
    if (audible && transmitting)
        v->speaking_until = now + SELF_SPEAKING_HOLD_MS;
    bool speaking = transmitting && now < v->speaking_until;
    if (speaking != v->speaking) {
        v->speaking = speaking;
        ++v->revision;
    }
}
static bool dispatch(voice_client *v, uint64_t now)
{
    if (!v->token_time)
        v->token_time = now;
    v->tokens = MIN(20.0, v->tokens + (now - v->token_time) / 100.0);
    v->token_time = now;
    bool changed = false;
    while (v->tokens >= 1 && json_array_size(v->outgoing) && auth_ws_state(v->auth) == WS_OPEN) {
        if (!auth_send_event(v->auth, json_array_get(v->outgoing, 0)))
            break; /* Existing bounded WS queue will drain in the event loop. */
        json_array_remove(v->outgoing, 0);
        v->tokens -= 1;
        changed = true;
    }
    return changed;
}
bool voice_poll(voice_client *v)
{
    if (!v)
        return false;
    uint64_t before = v->revision, now = 0;
    net_now_ms(&now);
    if (v->context != auth_context_generation(v->auth) || v->ws_epoch != auth_ws_epoch(v->auth) ||
        (!auth_has_session(v->auth) &&
         (v->state != VOICE_IDLE || v->engine || json_object_size(v->rooms))) ||
        (v->state > VOICE_ICE && v->state < VOICE_FAILED && auth_ws_state(v->auth) != WS_OPEN)) {
        bool in_call = v->state != VOICE_IDLE && v->state != VOICE_FAILED;
        idle(v);
        json_object_clear(v->rooms);
        json_object_clear(v->speakers);
        json_object_clear(v->mutes);
        v->context = auth_context_generation(v->auth);
        v->ws_epoch = auth_ws_epoch(v->auth);
        if (in_call)
            snprintf(v->notice, sizeof(v->notice),
                     "Chamada encerrada: conexão ou sessão interrompida.");
    }
    if (!active && *v->pending) {
        permission_decision decision =
            permissions_channel(auth_permissions(v->auth), v->pending, PERMISSION_CONNECT_VOICE);
        if (!auth_has_session(v->auth)) {
            v->pending[0] = 0;
            v->pending_until = 0;
            v->notice[0] = 0;
            ++v->revision;
        } else if (decision == PERMISSION_DENIED) {
            v->pending[0] = 0;
            v->pending_until = 0;
            snprintf(v->notice, sizeof(v->notice), "Voz indisponível ou sem permissão de conexão.");
            ++v->revision;
        } else if (decision == PERMISSION_ALLOWED && auth_ws_state(v->auth) == WS_OPEN) {
            char channel[37];
            snprintf(channel, sizeof(channel), "%s", v->pending);
            v->pending[0] = 0;
            v->pending_until = 0;
            begin(v, channel);
        } else if (now >= v->pending_until) {
            v->pending[0] = 0;
            v->pending_until = 0;
            snprintf(v->notice, sizeof(v->notice),
                     "Não foi possível entrar na chamada de voz. Tente novamente.");
            ++v->revision;
        }
    }
    json_t *event;
    while ((event = voice_audio_pop(v->audio))) {
        const char *type = str(event, "type");
        if (!strcmp(type, "devices")) {
            json_t *devices = json_object_get(event, "devices");
            if (json_is_array(devices)) {
                json_decref(v->devices);
                v->devices = json_incref(devices);
                ++v->revision;
            }
        } else if ((uint64_t)json_integer_value(json_object_get(event, "epoch")) == v->epoch &&
                   v->engine) {
            if (!strcmp(type, "audio_error"))
                fail(v, "Falha no áudio. Confira dispositivos, permissões e plugins GStreamer.");
            else if (!strcmp(type, "level")) {
                v->rms = json_number_value(json_object_get(event, "rms"));
                v->peak = json_number_value(json_object_get(event, "peak"));
                update_speaking(v, now, v->rms > SELF_SPEAKING_RMS);
                if (tester == v)
                    ++v->revision;
            } else if (!strcmp(type, "connection")) {
                int state = (int)json_integer_value(json_object_get(event, "state"));
                if (state == 2) { /* GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED. */
                    v->state = VOICE_CONNECTED;
                    v->deadline = 0;
                    ++v->revision;
                } else if (state == 3) {
                    voice_audio_transmit(v->audio, false);
                    v->state = VOICE_NEGOTIATING;
                    v->deadline = now + 10000;
                    ++v->revision;
                } else if (state == 4 || state == 5)
                    fail(v, "Conexão de voz interrompida. Entre novamente para tentar.");
            } else if (!strcmp(type, "offer") || !strcmp(type, "answer")) {
                if (!send_event(v,
                                json_pack("{s:s,s:s}", "type",
                                          !strcmp(type, "offer") ? "voice_offer" : "voice_answer",
                                          "sdp", str(event, "sdp"))))
                    fail(v, "Fila de sinalização de voz cheia.");
                else {
                    v->local_description = true;
                    for (size_t i = 0; i < json_array_size(v->early_local); ++i)
                        send_event(v, json_incref(json_array_get(v->early_local, i)));
                    json_array_clear(v->early_local);
                }
            } else if (!strcmp(type, "candidate")) {
                json_t *candidate =
                    json_pack("{s:s,s:s,s:i}", "type", "voice_ice_candidate", "candidate",
                              str(event, "candidate"), "sdp_mline_index",
                              (int)json_integer_value(json_object_get(event, "line")));
                bool ok;
                if (v->local_description)
                    ok = send_event(v, candidate);
                else {
                    ok = candidate && json_array_size(v->early_local) < 128 &&
                         json_array_append(v->early_local, candidate) == 0;
                    json_decref(candidate);
                }
                if (!ok)
                    fail(v, "Fila de candidatos ICE cheia.");
            }
        }
        json_decref(event);
    }
    /* Clear the hold when levels stop arriving or transmission is disabled. */
    update_speaking(v, now, false);
    if (v->state == VOICE_CONNECTED) {
        if (permissions_channel(auth_permissions(v->auth), v->channel, PERMISSION_CONNECT_VOICE) ==
            PERMISSION_DENIED) {
            voice_leave(v);
            snprintf(v->notice, sizeof(v->notice), "Chamada encerrada: permissão de voz removida.");
        }
    }
    if (v->state == VOICE_CONNECTED) {
        bool muted = !want_transmit();
        if (muted != v->requested_muted) {
            /* Coalesce pending mute changes, never repeat key autorepeat events. */
            for (size_t i = json_array_size(v->outgoing); i > 0; --i)
                if (!strcmp(str(json_array_get(v->outgoing, i - 1), "type"), "voice_mute"))
                    json_array_remove(v->outgoing, i - 1);
            if (send_event(v, json_pack("{s:s,s:b}", "type", "voice_mute", "muted", muted)))
                v->requested_muted = muted;
        }
        voice_audio_transmit(v->audio, !muted && !v->server_muted);
    }
    if (v->deadline && now >= v->deadline)
        fail(v, "Tempo de conexão/saída esgotado. Entre novamente para tentar.");
    dispatch(v, now);
    if (!active && next_call) {
        voice_client *next = next_call;
        char channel[37];
        snprintf(channel, sizeof(channel), "%s", next->next_channel);
        next_call = NULL;
        begin(next, channel);
    }
    return before != v->revision;
}
voice_state voice_get_state(const voice_client *v)
{
    return v ? v->state : VOICE_IDLE;
}
const char *voice_channel(const voice_client *v)
{
    return v ? v->channel : "";
}
const char *voice_notice(const voice_client *v)
{
    return v ? v->notice : "";
}
uint64_t voice_revision(const voice_client *v)
{
    return v ? v->revision : 0;
}
const json_t *voice_members(const voice_client *v, const char *channel)
{
    return v ? json_object_get(v->rooms, channel) : NULL;
}
bool voice_speaking(const voice_client *v, const char *channel, const char *user)
{
    if (!v)
        return false;
    if (v->speaking && !strcmp(channel, v->channel) && !strcmp(user, auth_user_id(v->auth)))
        return true;
    return includes(json_object_get(v->speakers, channel), user);
}
int voice_muted(const voice_client *v, const char *channel, const char *user)
{
    if (!v || !uuid(channel) || !uuid(user))
        return -1;
    const json_t *users = json_object_get(v->mutes, channel);
    const json_t *muted = users ? json_object_get(users, user) : NULL;
    return json_is_boolean(muted) ? (json_is_true(muted) ? 1 : 0) : -1;
}
voice_client *voice_active(void)
{
    return active;
}
const char *voice_instance(const voice_client *v)
{
    return v ? auth_instance_url(v->auth) : "";
}
void voice_microphone_toggle(void)
{
    microphone = !microphone;
    pressed = false;
    if (active) {
        voice_audio_transmit(active->audio, false);
        ++active->revision;
    }
}
void voice_output_toggle(void)
{
    output = !output;
    voice_client *v = active ? active : tester;
    if (v) {
        voice_audio_output(v->audio, output);
        ++v->revision;
    }
}
bool voice_microphone_enabled(void)
{
    return microphone;
}
bool voice_output_enabled(void)
{
    return output;
}
void voice_ptt(bool down)
{
    pressed = down && settings.push_to_talk && microphone && !tester;
    if (active) {
        if (!pressed)
            voice_audio_transmit(active->audio, false);
        ++active->revision;
    }
}
bool voice_ptt_pressed(void)
{
    return pressed;
}
bool voice_test(voice_client *v, bool enabled)
{
    if (!enabled) {
        if (tester) {
            voice_client *old = tester;
            tester = NULL;
            voice_audio_transmit(old->audio, false);
            if (old != active)
                stop_engine(old);
            else
                voice_audio_configure(old->audio, &settings, false);
            ++old->revision;
        }
        pressed = false;
        return true;
    }
    if (tester)
        voice_test(tester, false);
    v = active ? active : v;
    if (!v || (active && active->state != VOICE_CONNECTED))
        return false;
    tester = v;
    pressed = false;
    voice_audio_transmit(v->audio, false);
    if (v == active)
        return voice_audio_configure(v->audio, &settings, true);
    ++v->epoch;
    v->engine = voice_audio_start(v->audio, v->epoch, &settings, 8, NULL, true);
    if (v->engine)
        voice_audio_output(v->audio, output);
    else
        tester = NULL;
    return v->engine;
}
bool voice_testing(void)
{
    return tester != NULL;
}
void voice_refresh_devices(voice_client *v)
{
    if (active)
        v = active;
    if (v)
        voice_audio_devices(v->audio);
}
const json_t *voice_devices(voice_client *v)
{
    if (active)
        v = active;
    return v ? v->devices : NULL;
}
double voice_level(voice_client *v, bool peak)
{
    if (tester)
        v = tester;
    return v ? (peak ? v->peak : v->rms) : -120;
}
