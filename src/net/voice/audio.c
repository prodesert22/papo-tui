#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#define GST_USE_UNSTABLE_API
#include "net/voice/audio.h"
#include "platform/runtime.h"
#include <glib/gstdio.h>
#include <gst/audio/audio.h>
#include <gst/gst.h>
#include <gst/rtp/gstrtphdrext.h>
#include <gst/sdp/sdp.h>
#include <gst/webrtc/webrtc.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

typedef enum {
    DEVICES,
    START,
    STOP,
    CONFIGURE,
    OUTPUT,
    DESCRIPTION,
    CANDIDATE,
    PAD,
    DESCRIBED,
    QUIT
} command_kind;
typedef struct {
    command_kind kind;
    uint64_t epoch;
    audio_config config;
    unsigned slots, line;
    bool flag;
    char *text, *type;
    json_t *ice;
    GstPad *pad;
    gulong probe;
} audio_command;
struct voice_audio {
    gatomicrefcount references;
    GThread *thread;
    GAsyncQueue *commands, *events;
    gint transmit, overflow;
    /* Candidates gathered before the description is sent travel inside it. */
    gint trickle, gathered;
    /* Everything below is worker-owned. */
    uint64_t epoch;
    GstElement *pipeline, *rtc, *source, *sink, *capture, *echo, *mix;
    GstElement *mic_volume, *out_volume, *monitor;
    GstDeviceMonitor *devices;
    GPtrArray *catalog;
    audio_config config;
    bool monitoring, test_only, output;
    /* Microphone streaming thread only; reset before each pipeline starts. */
    GstClockTime voice_until;
    /* A local description waits for ICE gathering before it is sent. */
    bool describing, describe_offer, offer_waiting;
    GstPad *payload; /* Payloader source; the offer waits for its caps. */
    gint64 describe_deadline, describe_quiet;
    gint describe_seen;
};
static voice_audio *audio_ref(voice_audio *a)
{
    g_atomic_ref_count_inc(&a->references);
    return a;
}
static void audio_unref(voice_audio *a);
static void closure_unref(gpointer data, GClosure *closure)
{
    (void)closure;
    audio_unref(data);
}
static void command_free(audio_command *c)
{
    if (!c)
        return;
    g_free(c->text);
    g_free(c->type);
    json_decref(c->ice);
    if (c->pad) {
        if (c->probe)
            gst_pad_remove_probe(c->pad, c->probe);
        gst_object_unref(c->pad);
    }
    g_free(c);
}
static void emit_epoch(voice_audio *a, json_t *event, uint64_t epoch)
{
    if (!event)
        return;
    if (g_async_queue_length(a->events) >= 256 ||
        json_object_set_new(event, "epoch", json_integer((json_int_t)epoch)) < 0) {
        json_decref(event);
        g_atomic_int_set(&a->transmit, false);
        g_atomic_int_set(&a->overflow, true);
        return;
    }
    g_async_queue_push(a->events, event);
}
static void emit(voice_audio *a, json_t *event)
{
    emit_epoch(a, event, a->epoch);
}
static void error_event(voice_audio *a)
{
    /* SDP, ICE credentials and native diagnostics must never enter UI/logs. */
    emit(a, json_pack("{s:s}", "type", "audio_error"));
    g_atomic_int_set(&a->transmit, false);
}
/* Optional developer diagnostics: PAPO_VOICE_DIAGNOSTICS names a private file
 * that receives redacted SDP and RTP statistics. ICE credentials, DTLS
 * fingerprints, candidates and addresses never reach it. */
static GMutex diagnostics_lock;
static FILE *diagnostics_file(void)
{
    static FILE *file;
    static gsize once;
    if (g_once_init_enter(&once)) {
        const char *path = g_getenv("PAPO_VOICE_DIAGNOSTICS");
        if (path && *path) {
#ifdef G_OS_UNIX
            int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
            file = fd >= 0 ? fdopen(fd, "a") : NULL;
            if (!file && fd >= 0)
                close(fd);
#else
            file = g_fopen(path, "a");
#endif
        }
        g_once_init_leave(&once, 1);
    }
    return file;
}
#define diagnostic voice_audio_diagnostic
void voice_audio_diagnostic(const char *format, ...)
{
    FILE *file = diagnostics_file();
    if (!file)
        return;
    va_list arguments;
    va_start(arguments, format);
    g_mutex_lock(&diagnostics_lock);
    fprintf(file, "%.3f ", g_get_monotonic_time() / 1e6);
    vfprintf(file, format, arguments);
    fputc('\n', file);
    fflush(file);
    g_mutex_unlock(&diagnostics_lock);
    va_end(arguments);
}
static void diagnostic_sdp(const char *label, const char *sdp)
{
    if (!diagnostics_file() || !sdp)
        return;
    GString *safe = g_string_new(NULL);
    char **lines = g_strsplit(sdp, "\n", -1);
    for (char **line = lines; *line; ++line) {
        g_strchomp(*line);
        if (!**line || g_str_has_prefix(*line, "a=ice-ufrag") ||
            g_str_has_prefix(*line, "a=ice-pwd") || g_str_has_prefix(*line, "a=fingerprint") ||
            g_str_has_prefix(*line, "a=candidate") || g_str_has_prefix(*line, "a=crypto") ||
            g_str_has_prefix(*line, "c=") || g_str_has_prefix(*line, "o="))
            continue;
        g_string_append_printf(safe, "\n  %s", *line);
    }
    g_strfreev(lines);
    diagnostic("%s SDP:%s", label, safe->str);
    g_string_free(safe, true);
}
static gboolean diagnostic_stat(GQuark field, const GValue *value, gpointer data)
{
    (void)field;
    (void)data;
    if (!GST_VALUE_HOLDS_STRUCTURE(value))
        return true;
    const GstStructure *stat = gst_value_get_structure(value);
    GstWebRTCStatsType type = 0;
    if (!gst_structure_get(stat, "type", GST_TYPE_WEBRTC_STATS_TYPE, &type, NULL))
        return true;
    /* Candidate, transport and certificate stats carry addresses and keys. */
    if (type == GST_WEBRTC_STATS_INBOUND_RTP || type == GST_WEBRTC_STATS_OUTBOUND_RTP ||
        type == GST_WEBRTC_STATS_REMOTE_INBOUND_RTP ||
        type == GST_WEBRTC_STATS_REMOTE_OUTBOUND_RTP || type == GST_WEBRTC_STATS_CODEC) {
        char *text = gst_structure_to_string(stat);
        diagnostic("stats %s", text);
        g_free(text);
    }
    return true;
}
static void diagnostic_stats(GstElement *rtc)
{
    GstPromise *promise = gst_promise_new();
    g_signal_emit_by_name(rtc, "get-stats", NULL, promise);
    if (gst_promise_wait(promise) == GST_PROMISE_RESULT_REPLIED) {
        const GstStructure *reply = gst_promise_get_reply(promise);
        if (reply)
            gst_structure_foreach(reply, diagnostic_stat, NULL);
    }
    gst_promise_unref(promise);
}
static GstPadProbeReturn packet_gate(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void)pad;
    (void)info;
    return g_atomic_int_get(&((voice_audio *)data)->transmit) ? GST_PAD_PROBE_OK
                                                              : GST_PAD_PROBE_DROP;
}
/* The offer advertises the RFC 6464 extension with vad=on, so receivers read
 * the V bit as voice activity, but level always writes it as false. Derive it
 * from the threshold and hold of the local speaking indicator in client.c. */
#define VOICE_ACTIVITY_DBOV 50
#define VOICE_ACTIVITY_HOLD (400 * GST_MSECOND)
static GstPadProbeReturn voice_activity(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void)pad;
    voice_audio *a = data;
    GstBuffer *buffer = gst_buffer_make_writable(GST_PAD_PROBE_INFO_BUFFER(info));
    GST_PAD_PROBE_INFO_DATA(info) = buffer;
    GstAudioLevelMeta *meta = gst_buffer_get_audio_level_meta(buffer);
    if (!meta)
        return GST_PAD_PROBE_OK;
    GstClockTime time = GST_BUFFER_PTS(buffer);
    bool audible = meta->level <= VOICE_ACTIVITY_DBOV;
    if (audible && GST_CLOCK_TIME_IS_VALID(time))
        a->voice_until = time + VOICE_ACTIVITY_HOLD;
    meta->voice_activity =
        audible || (GST_CLOCK_TIME_IS_VALID(time) && GST_CLOCK_TIME_IS_VALID(a->voice_until) &&
                    time < a->voice_until);
    return GST_PAD_PROBE_OK;
}
static GstPadProbeReturn blocked(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void)pad;
    (void)info;
    (void)data;
    return GST_PAD_PROBE_OK;
}
static void pad_added(GstElement *rtc, GstPad *pad, gpointer data)
{
    voice_audio *a = data;
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;
    audio_command *c = g_new0(audio_command, 1);
    c->kind = PAD;
    guint64 *epoch = g_object_get_data(G_OBJECT(rtc), "papo-epoch");
    c->epoch = epoch ? *epoch : 0;
    c->pad = gst_object_ref(pad);
    c->probe = gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, blocked, NULL, NULL);
    if (g_async_queue_length(a->commands) < 128)
        g_async_queue_push(a->commands, c);
    else {
        command_free(c);
        g_atomic_int_set(&a->overflow, true);
    }
}
static void ice_candidate(GstElement *rtc, guint line, gchar *candidate, gpointer data)
{
    guint64 *epoch = g_object_get_data(G_OBJECT(rtc), "papo-epoch");
    g_atomic_int_inc(&((voice_audio *)data)->gathered);
    if (candidate && *candidate && diagnostics_file()) {
        /* Transport and type only: addresses and ports stay out of the log. */
        char **parts = g_strsplit(candidate, " ", -1);
        guint n = g_strv_length(parts);
        const char *type = "?";
        for (guint i = 0; i + 1 < n; ++i)
            if (!strcmp(parts[i], "typ"))
                type = parts[i + 1];
        diagnostic("local candidate line=%u transport=%s type=%s", line, n > 2 ? parts[2] : "?",
                   type);
        g_strfreev(parts);
    }
    if (candidate && *candidate && g_atomic_int_get(&((voice_audio *)data)->trickle))
        emit_epoch(
            data,
            json_pack("{s:s,s:i,s:s}", "type", "candidate", "line", line, "candidate", candidate),
            epoch ? *epoch : 0);
}
static void connection_changed(GObject *rtc, GParamSpec *pspec, gpointer data)
{
    (void)pspec;
    GstWebRTCPeerConnectionState state;
    g_object_get(rtc, "connection-state", &state, NULL);
    diagnostic("connection state %d", (int)state);
    guint64 *epoch = g_object_get_data(rtc, "papo-epoch");
    emit_epoch(data, json_pack("{s:s,s:i}", "type", "connection", "state", state),
               epoch ? *epoch : 0);
}
typedef struct {
    voice_audio *audio;
    GstElement *rtc;
    uint64_t epoch;
    bool offer;
} description_task;
static void description_task_free(gpointer data);
/* Runs on a webrtcbin thread once the local description applies; the worker
 * sends it after ICE gathering, so one event carries the candidates. */
static void description_applied(GstPromise *promise, gpointer data)
{
    description_task *task = data;
    const GstStructure *reply = gst_promise_get_reply(promise);
    audio_command *c = g_new0(audio_command, 1);
    c->kind = DESCRIBED;
    c->epoch = task->epoch;
    c->flag = task->offer;
    if (reply && gst_structure_has_field(reply, "error")) {
        g_free(c);
        emit_epoch(task->audio, json_pack("{s:s}", "type", "audio_error"), task->epoch);
    } else if (g_async_queue_length(task->audio->commands) < 128)
        g_async_queue_push(task->audio->commands, c);
    else {
        g_free(c);
        g_atomic_int_set(&task->audio->overflow, true);
    }
}
static void description_created(GstPromise *promise, gpointer data)
{
    description_task *task = data;
    const GstStructure *reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription *sdp = NULL;
    if (reply)
        gst_structure_get(reply, task->offer ? "offer" : "answer",
                          GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &sdp, NULL);
    if (sdp) {
        description_task *applied = g_new0(description_task, 1);
        applied->audio = audio_ref(task->audio);
        applied->rtc = gst_object_ref(task->rtc);
        applied->epoch = task->epoch;
        applied->offer = task->offer;
        GstPromise *set =
            gst_promise_new_with_change_func(description_applied, applied, description_task_free);
        g_signal_emit_by_name(task->rtc, "set-local-description", sdp, set);
        gst_promise_unref(set);
        gst_webrtc_session_description_free(sdp);
    } else
        emit_epoch(task->audio, json_pack("{s:s}", "type", "audio_error"), task->epoch);
    gst_promise_unref(promise);
}
static void description_task_free(gpointer data)
{
    description_task *task = data;
    if (!task)
        return;
    gst_object_unref(task->rtc);
    audio_unref(task->audio);
    g_free(task);
}
static void create_description(voice_audio *a, bool offer)
{
    description_task *task = g_new0(description_task, 1);
    task->audio = audio_ref(a);
    task->rtc = gst_object_ref(a->rtc);
    task->epoch = a->epoch;
    task->offer = offer;
    GstPromise *promise =
        gst_promise_new_with_change_func(description_created, task, description_task_free);
    g_signal_emit_by_name(a->rtc, offer ? "create-offer" : "create-answer", NULL, promise);
}
/* Sends the applied local description once gathering completes or goes quiet,
 * with a short limit so a slow STUN/TURN server cannot delay the call;
 * candidates found later still trickle. Servers limit voice events, and hosts
 * with many interfaces gather dozens of candidates. */
#define DESCRIBE_WAIT_US 2000000
#define DESCRIBE_QUIET_US 400000
static void send_description(voice_audio *a)
{
    if (!a->describing || !a->rtc)
        return;
    gint64 now = g_get_monotonic_time();
    gint gathered = g_atomic_int_get(&a->gathered);
    if (gathered != a->describe_seen) {
        a->describe_seen = gathered;
        a->describe_quiet = now + DESCRIBE_QUIET_US;
    }
    GstWebRTCICEGatheringState gathering = GST_WEBRTC_ICE_GATHERING_STATE_NEW;
    g_object_get(a->rtc, "ice-gathering-state", &gathering, NULL);
    if (gathering != GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE && now < a->describe_deadline &&
        (!gathered || now < a->describe_quiet))
        return;
    a->describing = false;
    /* Open trickle first: a candidate racing this read is sent twice at worst. */
    g_atomic_int_set(&a->trickle, true);
    GstWebRTCSessionDescription *local = NULL;
    g_object_get(a->rtc, "local-description", &local, NULL);
    GstWebRTCSDPType expected =
        a->describe_offer ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER;
    if (!local || local->type != expected) {
        if (local)
            gst_webrtc_session_description_free(local);
        error_event(a);
        return;
    }
    char *text = gst_sdp_message_as_text(local->sdp);
    diagnostic_sdp(a->describe_offer ? "local offer" : "local answer", text);
    unsigned candidates = 0;
    for (const char *at = text; (at = strstr(at, "a=candidate:")); ++at)
        ++candidates;
    diagnostic("local description sent after gathering state %d with %u candidates", (int)gathering,
               candidates);
    emit(a, json_pack("{s:s,s:s}", "type", a->describe_offer ? "offer" : "answer", "sdp", text));
    g_free(text);
    gst_webrtc_session_description_free(local);
}
/* webrtcbin declares the sending SSRC (a=ssrc) only once the payloader has
 * negotiated caps. Live microphones take a moment to start, and a server that
 * bundles several transceivers cannot route an undeclared SSRC. */
static void offer_when_ready(voice_audio *a)
{
    if (!a->offer_waiting || !a->payload || !gst_pad_has_current_caps(a->payload))
        return;
    a->offer_waiting = false;
    create_description(a, true);
}
static void close_pipeline(voice_audio *a)
{
    g_atomic_int_set(&a->transmit, false);
    g_atomic_int_set(&a->trickle, false);
    g_atomic_int_set(&a->gathered, 0);
    a->describing = a->offer_waiting = false;
    if (a->payload) {
        gst_object_unref(a->payload);
        a->payload = NULL;
    }
    if (a->rtc)
        g_signal_handlers_disconnect_by_data(a->rtc, a);
    if (a->pipeline) {
        gst_element_set_state(a->pipeline, GST_STATE_NULL);
        gst_object_unref(a->pipeline);
    }
    a->pipeline = a->rtc = a->source = a->sink = a->capture = a->echo = a->mix = NULL;
    a->mic_volume = a->out_volume = a->monitor = NULL;
}
static char *device_id(GstDevice *device)
{
    GstElement *element = gst_device_create_element(device, NULL);
    char *native = NULL;
    const char *factory = "";
    if (element) {
        GstElementFactory *f = gst_element_get_factory(element);
        if (f)
            factory = gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(f));
        GParamSpec *property = g_object_class_find_property(G_OBJECT_GET_CLASS(element), "device");
        if (property && G_PARAM_SPEC_VALUE_TYPE(property) == G_TYPE_STRING)
            g_object_get(element, "device", &native, NULL);
    }
    gchar *klass = gst_device_get_device_class(device);
    GstStructure *props = gst_device_get_properties(device);
    char *fallback = props ? gst_structure_to_string(props) : gst_device_get_display_name(device);
    char *key = g_strdup_printf("%s:%s:%s", klass, factory, native ? native : fallback);
    char *id = g_compute_checksum_for_string(G_CHECKSUM_SHA256, key, -1);
    g_free(key);
    g_free(native);
    g_free(fallback);
    g_free(klass);
    if (props)
        gst_structure_free(props);
    if (element)
        gst_object_unref(element);
    return id;
}
static void refresh_devices(voice_audio *a)
{
    if (!a->devices) {
        a->devices = gst_device_monitor_new();
        gst_device_monitor_add_filter(a->devices, "Audio/Source", NULL);
        gst_device_monitor_add_filter(a->devices, "Audio/Sink", NULL);
        gst_device_monitor_start(a->devices);
    }
    GList *devices = gst_device_monitor_get_devices(a->devices);
    g_ptr_array_set_size(a->catalog, 0);
    json_t *list = json_array();
    for (GList *item = devices; item; item = item->next) {
        if (a->catalog->len < 128) {
            GstDevice *device = item->data;
            char *id = device_id(device), *name = gst_device_get_display_name(device),
                 *klass = gst_device_get_device_class(device);
            bool input = strstr(klass, "Source") != NULL;
            if (id && name) {
                g_ptr_array_add(a->catalog, gst_object_ref(device));
                json_array_append_new(
                    list, json_pack("{s:s,s:s,s:b}", "id", id, "name", name, "input", input));
            }
            g_free(id);
            g_free(name);
            g_free(klass);
        }
    }
    g_list_free_full(devices, gst_object_unref);
    emit(a, json_pack("{s:s,s:o}", "type", "devices", "devices", list));
}
static GstElement *device_element(voice_audio *a, bool input)
{
    const char *id = input ? a->config.input : a->config.output;
    if (*id)
        for (guint i = 0; i < a->catalog->len; ++i) {
            GstDevice *device = g_ptr_array_index(a->catalog, i);
            char *current = device_id(device), *klass = gst_device_get_device_class(device);
            bool same =
                current && !strcmp(id, current) && (strstr(klass, "Source") != NULL) == input;
            g_free(current);
            g_free(klass);
            if (same)
                return gst_device_create_element(device, NULL);
        }
    if (*id)
        return NULL; /* Never silently switch an explicitly selected microphone. */
    return gst_element_factory_make(input ? "autoaudiosrc" : "autoaudiosink", NULL);
}
static bool configure_ice(voice_audio *a, const json_t *ice)
{
    if (!json_is_array(ice) || json_array_size(ice) > 32)
        return false;
    size_t i, j;
    json_t *server, *url;
    bool stun_set = false;
    json_array_foreach(ice, i, server)
    {
        json_t *urls = json_object_get(server, "urls");
        json_t *array = json_is_string(urls) ? json_pack("[O]", urls) : json_incref(urls);
        if (!json_is_array(array) || json_array_size(array) > 32) {
            json_decref(array);
            return false;
        }
        json_array_foreach(array, j, url)
        {
            const char *raw = json_string_value(url);
            if (!raw || strlen(raw) > 2048 || strchr(raw, '\n') || strchr(raw, '\r')) {
                json_decref(array);
                return false;
            }
            if (g_str_has_prefix(raw, "stun:") && !stun_set) {
                char *uri =
                    g_strdup_printf("stun://%s", raw + (g_str_has_prefix(raw, "stun://") ? 7 : 5));
                g_object_set(a->rtc, "stun-server", uri, NULL);
                g_free(uri);
                stun_set = true;
            } else if (g_str_has_prefix(raw, "turn:") || g_str_has_prefix(raw, "turns:")) {
                const char *username = json_string_value(json_object_get(server, "username")),
                           *password = json_string_value(json_object_get(server, "credential"));
                if (!username || !password) {
                    json_decref(array);
                    return false;
                }
                char *user = g_uri_escape_string(username, NULL, false),
                     *pass = g_uri_escape_string(password, NULL, false);
                const char *address = strchr(raw, ':') + 1;
                if (g_str_has_prefix(address, "//"))
                    address += 2;
                char *uri = g_strdup_printf("%s://%s:%s@%s",
                                            g_str_has_prefix(raw, "turns:") ? "turns" : "turn",
                                            user, pass, address);
                gboolean accepted = false;
                g_signal_emit_by_name(a->rtc, "add-turn-server", uri, &accepted);
                memset(uri, 0, strlen(uri));
                memset(pass, 0, strlen(pass));
                g_free(uri);
                g_free(user);
                g_free(pass);
                if (!accepted) {
                    json_decref(array);
                    return false;
                }
            }
        }
        json_decref(array);
    }
    return true;
}
static void controls(voice_audio *a)
{
    if (!a->pipeline)
        return;
    g_object_set(a->mic_volume, "volume", a->config.microphone_volume / 100.0, NULL);
    g_object_set(a->out_volume, "volume", a->config.output_volume / 100.0, "mute", !a->output,
                 NULL);
    g_object_set(a->monitor, "mute", !a->monitoring, NULL);
}
static bool start_pipeline(voice_audio *a, const audio_command *c)
{
    close_pipeline(a);
    a->epoch = c->epoch;
    a->config = c->config;
    a->test_only = c->flag;
    a->monitoring = c->flag;
    refresh_devices(a);
    const char *base =
        "audiomixer name=mix force-live=true ignore-inactive-pads=true ! audioconvert ! "
        "audioresample ! audio/x-raw,rate=48000 ! volume name=out ! webrtcechoprobe name=echo "
        "audioconvert name=capture ! audioresample ! audio/x-raw,rate=48000,channels=1 ! "
        "webrtcdsp probe=echo gain-control=false ! volume name=mic ! "
        "level name=level interval=50000000 post-messages=true audio-level-meta=true ! tee "
        "name=raw "
        "raw. ! queue max-size-time=100000000 leaky=downstream ! volume name=monitor mute=true ! "
        "mix.";
    const char *send =
        " raw. ! queue max-size-time=100000000 leaky=downstream ! opusenc audio-type=voice "
        "bitrate=32000 "
        "frame-size=20 inband-fec=true ! rtpopuspay name=pay pt=111 ! "
        "application/"
        "x-rtp,media=audio,payload=(int)111,encoding-name=OPUS,clock-rate=48000,encoding-params=("
        "string)2 ! "
        "webrtcbin name=rtc bundle-policy=max-bundle";
    char *description = g_strconcat(base, a->test_only ? "" : send, NULL);
    GError *error = NULL;
    a->pipeline = gst_parse_launch(description, &error);
    g_free(description);
    if (error) {
        g_error_free(error);
        return false;
    }
    if (!a->pipeline)
        return false;
    a->capture = gst_bin_get_by_name(GST_BIN(a->pipeline), "capture");
    a->echo = gst_bin_get_by_name(GST_BIN(a->pipeline), "echo");
    a->mix = gst_bin_get_by_name(GST_BIN(a->pipeline), "mix");
    a->mic_volume = gst_bin_get_by_name(GST_BIN(a->pipeline), "mic");
    a->out_volume = gst_bin_get_by_name(GST_BIN(a->pipeline), "out");
    a->monitor = gst_bin_get_by_name(GST_BIN(a->pipeline), "monitor");
    /* The pipeline owns these elements; discard the lookup references now. */
    GstElement *lookups[] = {a->capture, a->echo, a->mix, a->mic_volume, a->out_volume, a->monitor};
    bool complete = true;
    for (size_t i = 0; i < G_N_ELEMENTS(lookups); ++i)
        if (lookups[i])
            gst_object_unref(lookups[i]);
        else
            complete = false;
    if (!complete)
        return false;
    GstElement *level = gst_bin_get_by_name(GST_BIN(a->pipeline), "level");
    if (!level)
        return false;
    GstPad *level_pad = gst_element_get_static_pad(level, "src");
    gst_object_unref(level);
    if (!level_pad)
        return false;
    a->voice_until = GST_CLOCK_TIME_NONE;
    gst_pad_add_probe(level_pad, GST_PAD_PROBE_TYPE_BUFFER, voice_activity, a, NULL);
    gst_object_unref(level_pad);
    a->source = device_element(a, true);
    a->sink = device_element(a, false);
    if (!a->source || !a->sink) {
        if (a->source)
            gst_object_unref(a->source);
        if (a->sink)
            gst_object_unref(a->sink);
        a->source = a->sink = NULL;
        return false;
    }
    gst_bin_add_many(GST_BIN(a->pipeline), a->source, a->sink, NULL);
    if (!gst_element_link(a->source, a->capture) || !gst_element_link(a->echo, a->sink))
        return false;
    if (!a->test_only) {
        a->rtc = gst_bin_get_by_name(GST_BIN(a->pipeline), "rtc");
        if (!a->rtc)
            return false;
        gst_object_unref(a->rtc);
        if (!configure_ice(a, c->ice))
            return false;
        guint64 *epoch = g_new(guint64, 1);
        *epoch = a->epoch;
        g_object_set_data_full(G_OBJECT(a->rtc), "papo-epoch", epoch, g_free);
        g_signal_connect_data(a->rtc, "on-ice-candidate", G_CALLBACK(ice_candidate), audio_ref(a),
                              closure_unref, 0);
        g_signal_connect_data(a->rtc, "pad-added", G_CALLBACK(pad_added), audio_ref(a),
                              closure_unref, 0);
        g_signal_connect_data(a->rtc, "notify::connection-state", G_CALLBACK(connection_changed),
                              audio_ref(a), closure_unref, 0);
        GstElement *pay = gst_bin_get_by_name(GST_BIN(a->pipeline), "pay");
        GstRTPHeaderExtension *extension =
            gst_rtp_header_extension_create_from_uri("urn:ietf:params:rtp-hdrext:ssrc-audio-level");
        if (!extension) {
            gst_object_unref(pay);
            return false;
        }
        gst_rtp_header_extension_set_id(extension, 1);
        g_signal_emit_by_name(pay, "add-extension", extension);
        GstPad *pad = gst_element_get_static_pad(pay, "src");
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
                          packet_gate, a, NULL);
        gst_object_unref(pad);
        gst_object_unref(pay);
        GstCaps *caps = gst_caps_from_string(
            "application/x-rtp,media=audio,payload=(int)111,encoding-name=OPUS,"
            "clock-rate=48000,encoding-params=(string)2");
        gst_rtp_header_extension_set_caps_from_attributes(extension, caps);
        gst_object_unref(extension);
        GstWebRTCRTPTransceiver *sender = NULL;
        g_signal_emit_by_name(a->rtc, "get-transceiver", 0, &sender);
        if (!sender) {
            gst_caps_unref(caps);
            return false;
        }
        g_object_set(sender, "codec-preferences", caps, "direction",
                     GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDRECV, NULL);
        gst_object_unref(sender);
        /* The send transceiver is created by linking the payloader. */
        for (unsigned i = 1; i < c->slots; ++i) {
            GstWebRTCRTPTransceiver *transceiver = NULL;
            g_signal_emit_by_name(a->rtc, "add-transceiver",
                                  GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_RECVONLY, caps,
                                  &transceiver);
            if (transceiver)
                gst_object_unref(transceiver);
        }
        gst_caps_unref(caps);
    }
    controls(a);
    if (gst_element_set_state(a->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        return false;
    if (a->rtc) {
        GstElement *pay = gst_bin_get_by_name(GST_BIN(a->pipeline), "pay");
        a->payload = pay ? gst_element_get_static_pad(pay, "src") : NULL;
        if (pay)
            gst_object_unref(pay);
        if (!a->payload)
            return false;
        a->offer_waiting = true;
        offer_when_ready(a);
    } else
        emit(a, json_pack("{s:s}", "type", "test_started"));
    return true;
}
static void received_pad(voice_audio *a, audio_command *c)
{
    if (!a->pipeline || c->epoch != a->epoch)
        return;
    GError *error = NULL;
    GstElement *branch = gst_parse_bin_from_description(
        "queue max-size-time=200000000 ! rtpopusdepay ! opusdec use-inband-fec=true ! "
        "audioconvert ! audioresample",
        true, &error);
    if (error) {
        g_error_free(error);
        if (branch)
            gst_object_unref(branch);
        error_event(a);
        return;
    }
    gst_bin_add(GST_BIN(a->pipeline), branch);
    GstPad *sink = gst_element_get_static_pad(branch, "sink");
    bool linked = gst_pad_link(c->pad, sink) == GST_PAD_LINK_OK && gst_element_link(branch, a->mix);
    gst_object_unref(sink);
    if (!linked || !gst_element_sync_state_with_parent(branch))
        error_event(a);
}
static bool replace_device(voice_audio *a, bool input)
{
    GstElement *element = device_element(a, input), *old = input ? a->source : a->sink;
    if (!element)
        return false;
    gst_element_set_state(old, GST_STATE_NULL);
    if (input)
        gst_element_unlink(old, a->capture);
    else
        gst_element_unlink(a->echo, old);
    gst_bin_remove(GST_BIN(a->pipeline), old);
    gst_bin_add(GST_BIN(a->pipeline), element);
    if (input)
        a->source = element;
    else
        a->sink = element;
    return (input ? gst_element_link(element, a->capture) : gst_element_link(a->echo, element)) &&
           gst_element_sync_state_with_parent(element);
}
static double first_level(const GValue *value)
{
    if (value && GST_VALUE_HOLDS_LIST(value) && gst_value_list_get_size(value))
        return g_value_get_double(gst_value_list_get_value(value, 0));
    /* level retains its historical boxed array message ABI. Avoid deprecated
     * accessor functions while accepting both that ABI and GstValueList. */
    if (value && G_VALUE_HOLDS_BOXED(value) && !strcmp(G_VALUE_TYPE_NAME(value), "GValueArray")) {
        const GValueArray *array = g_value_get_boxed(value);
        if (array && array->n_values)
            return g_value_get_double(&array->values[0]);
    }
    return -120;
}
static void bus_events(voice_audio *a)
{
    if (a->devices) {
        GstBus *bus = gst_device_monitor_get_bus(a->devices);
        GstMessage *message;
        bool changed = false;
        while ((message = gst_bus_pop(bus))) {
            changed |= GST_MESSAGE_TYPE(message) == GST_MESSAGE_DEVICE_ADDED ||
                       GST_MESSAGE_TYPE(message) == GST_MESSAGE_DEVICE_REMOVED;
            gst_message_unref(message);
        }
        gst_object_unref(bus);
        if (changed)
            refresh_devices(a);
    }
    if (!a->pipeline)
        return;
    GstBus *bus = gst_element_get_bus(a->pipeline);
    GstMessage *message;
    bool failed = false;
    while ((message = gst_bus_pop(bus))) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR)
            failed = true;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ELEMENT) {
            const GstStructure *s = gst_message_get_structure(message);
            if (s && gst_structure_has_name(s, "level")) {
                const GValue *rms = gst_structure_get_value(s, "rms"),
                             *peak = gst_structure_get_value(s, "peak");
                double r = first_level(rms), p = first_level(peak);
                emit(a, json_pack("{s:s,s:f,s:f}", "type", "level", "rms", isfinite(r) ? r : -120,
                                  "peak", isfinite(p) ? p : -120));
            }
        }
        gst_message_unref(message);
    }
    gst_object_unref(bus);
    if (failed) {
        error_event(a);
        close_pipeline(a);
    }
}
static gpointer worker(gpointer data)
{
    voice_audio *a = data;
    GError *error = NULL;
    /* Keep normal registry probing in its scanner process: scanning all host
     * plugins in process would retain unrelated graphics libraries in Light. */
    bool initialized = gst_init_check(NULL, NULL, &error);
    if (error)
        g_error_free(error);
    if (initialized) {
        char *executable = papo_executable_path();
        if (executable) {
            char *directory = g_path_get_dirname(executable);
            char *plugins = g_build_filename(directory, "papo-backends", "gstreamer-1.0", NULL);
            GDir *directory_handle = g_dir_open(plugins, 0, NULL);
            const char *name;
            while (directory_handle && (name = g_dir_read_name(directory_handle))) {
                if (!g_str_has_suffix(name, ".so") && !g_str_has_suffix(name, ".dll"))
                    continue;
                char *path = g_build_filename(plugins, name, NULL);
                GstPlugin *plugin = gst_plugin_load_file(path, NULL);
                if (plugin)
                    gst_object_unref(plugin);
                g_free(path);
            }
            if (directory_handle)
                g_dir_close(directory_handle);
            g_free(plugins);
            g_free(directory);
            free(executable);
        }
    }
    a->catalog = g_ptr_array_new_with_free_func(gst_object_unref);
    bool quit = false;
    gint64 next_stats = 0;
    while (!quit) {
        if (a->rtc && diagnostics_file() && g_get_monotonic_time() >= next_stats) {
            next_stats = g_get_monotonic_time() + 2000000;
            diagnostic("transmit gate %s", g_atomic_int_get(&a->transmit) ? "open" : "closed");
            diagnostic_stats(a->rtc);
        }
        audio_command *c = g_async_queue_timeout_pop(a->commands, 10000);
        if (g_atomic_int_compare_and_exchange(&a->overflow, true, false)) {
            error_event(a);
            close_pipeline(a);
        }
        if (c) {
            if (!initialized && c->kind != QUIT) {
                a->epoch = c->epoch;
                error_event(a);
            } else if (c->kind == START) {
                if (!start_pipeline(a, c)) {
                    error_event(a);
                    close_pipeline(a);
                }
            } else if (c->kind == STOP)
                close_pipeline(a);
            else if (c->kind == DEVICES)
                refresh_devices(a);
            else if (c->kind == CONFIGURE) {
                bool input = strcmp(a->config.input, c->config.input),
                     output = strcmp(a->config.output, c->config.output);
                a->config = c->config;
                a->monitoring = c->flag;
                if (a->pipeline && ((input && !replace_device(a, true)) ||
                                    (output && !replace_device(a, false)))) {
                    error_event(a);
                    close_pipeline(a);
                }
                controls(a);
            } else if (c->kind == OUTPUT) {
                a->output = c->flag;
                controls(a);
            } else if (c->kind == PAD)
                received_pad(a, c);
            else if (c->kind == DESCRIBED && a->rtc && c->epoch == a->epoch) {
                a->describing = true;
                a->describe_offer = c->flag;
                a->describe_deadline = g_get_monotonic_time() + DESCRIBE_WAIT_US;
                a->describe_seen = -1;
                send_description(a);
            } else if (c->kind == DESCRIPTION && a->rtc) {
                GstSDPMessage *sdp = NULL;
                gst_sdp_message_new(&sdp);
                if (gst_sdp_message_parse_buffer((const guint8 *)c->text, strlen(c->text), sdp) ==
                    GST_SDP_OK) {
                    bool offer = !strcmp(c->type, "offer");
                    diagnostic_sdp(offer ? "remote offer" : "remote answer", c->text);
                    GstWebRTCSessionDescription *remote = gst_webrtc_session_description_new(
                        offer ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
                    GstPromise *promise = gst_promise_new();
                    g_signal_emit_by_name(a->rtc, "set-remote-description", remote, promise);
                    gst_promise_wait(promise); /* Only the audio worker waits. */
                    const GstStructure *reply = gst_promise_get_reply(promise);
                    bool failed = reply && gst_structure_has_field(reply, "error");
                    gst_promise_unref(promise);
                    gst_webrtc_session_description_free(remote);
                    if (failed) {
                        diagnostic("remote description rejected");
                        error_event(a);
                    } else if (offer)
                        create_description(a, false);
                } else {
                    gst_sdp_message_free(sdp);
                    error_event(a);
                }
            } else if (c->kind == CANDIDATE && a->rtc)
                g_signal_emit_by_name(a->rtc, "add-ice-candidate", c->line, c->text);
            else if (c->kind == QUIT)
                quit = true;
            command_free(c);
        }
        offer_when_ready(a);
        send_description(a);
        bus_events(a);
    }
    close_pipeline(a);
    if (a->devices) {
        gst_device_monitor_stop(a->devices);
        gst_object_unref(a->devices);
    }
    g_ptr_array_unref(a->catalog);
    return NULL;
}
static bool submit(voice_audio *a, audio_command *c)
{
    if (!a || g_async_queue_length(a->commands) >= 128) {
        command_free(c);
        return false;
    }
    if (!a->thread) {
        GError *error = NULL;
        a->thread = g_thread_try_new("papo-audio", worker, a, &error);
        if (!a->thread) {
            g_clear_error(&error);
            command_free(c);
            return false;
        }
    }
    g_async_queue_push(a->commands, c);
    return true;
}
voice_audio *voice_audio_create(void)
{
    voice_audio *a = g_try_new0(voice_audio, 1);
    if (a) {
        g_atomic_ref_count_init(&a->references);
        a->commands = g_async_queue_new();
        a->events = g_async_queue_new();
        a->output = true;
    }
    return a;
}
void voice_audio_destroy(voice_audio *a)
{
    if (!a)
        return;
    voice_audio_transmit(a, false);
    if (a->thread) {
        audio_command *c = g_new0(audio_command, 1);
        c->kind = QUIT;
        g_async_queue_push(a->commands, c);
        g_thread_join(a->thread);
    }
    audio_unref(a);
}
static void audio_unref(voice_audio *a)
{
    if (!g_atomic_ref_count_dec(&a->references))
        return;
    audio_command *c;
    while ((c = g_async_queue_try_pop(a->commands)))
        command_free(c);
    json_t *event;
    while ((event = voice_audio_pop(a)))
        json_decref(event);
    g_async_queue_unref(a->commands);
    g_async_queue_unref(a->events);
    g_free(a);
}
bool voice_audio_devices(voice_audio *a)
{
    audio_command *c = g_new0(audio_command, 1);
    c->kind = DEVICES;
    return submit(a, c);
}
bool voice_audio_start(voice_audio *a, uint64_t epoch, const audio_config *config, unsigned slots,
                       const json_t *ice, bool test_only)
{
    if (!audio_config_valid(config) || slots < 1 || slots > 64)
        return false;
    audio_command *c = g_new0(audio_command, 1);
    c->kind = START;
    c->epoch = epoch;
    c->config = *config;
    c->slots = slots;
    c->ice = json_deep_copy(ice);
    c->flag = test_only;
    return submit(a, c);
}
void voice_audio_stop(voice_audio *a)
{
    if (!a)
        return;
    voice_audio_transmit(a, false);
    if (a->thread) {
        audio_command *c = g_new0(audio_command, 1);
        c->kind = STOP;
        if (!submit(a, c))
            g_atomic_int_set(&a->overflow, true);
    }
}
bool voice_audio_configure(voice_audio *a, const audio_config *config, bool monitoring)
{
    if (!audio_config_valid(config))
        return false;
    audio_command *c = g_new0(audio_command, 1);
    c->kind = CONFIGURE;
    c->config = *config;
    c->flag = monitoring;
    return submit(a, c);
}
void voice_audio_transmit(voice_audio *a, bool enabled)
{
    if (a && g_atomic_int_get(&a->transmit) != (gint)enabled) {
        g_atomic_int_set(&a->transmit, enabled);
        diagnostic("transmit gate -> %s", enabled ? "open" : "closed");
    }
}
bool voice_audio_output(voice_audio *a, bool enabled)
{
    audio_command *c = g_new0(audio_command, 1);
    c->kind = OUTPUT;
    c->flag = enabled;
    return submit(a, c);
}
bool voice_audio_description(voice_audio *a, const char *type, const char *sdp)
{
    if (!type || (strcmp(type, "offer") && strcmp(type, "answer")) || !sdp ||
        strlen(sdp) > 96 * 1024)
        return false;
    audio_command *c = g_new0(audio_command, 1);
    c->kind = DESCRIPTION;
    c->type = g_strdup(type);
    c->text = g_strdup(sdp);
    return submit(a, c);
}
bool voice_audio_candidate(voice_audio *a, unsigned line, const char *candidate)
{
    if (line > 128 || !candidate || strlen(candidate) > 4096)
        return false;
    audio_command *c = g_new0(audio_command, 1);
    c->kind = CANDIDATE;
    c->line = line;
    c->text = g_strdup(candidate);
    return submit(a, c);
}
json_t *voice_audio_pop(voice_audio *a)
{
    return a ? g_async_queue_try_pop(a->events) : NULL;
}
