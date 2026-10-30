#define GST_USE_UNSTABLE_API
#include "net/voice/audio.h"
#include "voice_devices.h"
#include <gst/gst.h>
#include <gst/rtp/gstrtpbuffer.h>
#include <gst/webrtc/webrtc.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static const char *string(const json_t *event, const char *key)
{
    const char *s = json_string_value(json_object_get(event, key));
    return s ? s : "";
}
static json_t *wait_event(voice_audio *audio, const char *type)
{
    gint64 deadline = g_get_monotonic_time() + 8000000;
    while (g_get_monotonic_time() < deadline) {
        json_t *event = voice_audio_pop(audio);
        if (event) {
            if (!strcmp(string(event, "type"), type))
                return event;
            g_assert_cmpstr(string(event, "type"), !=, "audio_error");
            json_decref(event);
        } else
            g_usleep(1000);
    }
    g_error("Timed out waiting for audio event %s", type);
    return NULL;
}
static gint received_packets, received_levels, received_voice;
static gint received_ssrc; /* SSRC bits; atomics use gint. */
static GstPadProbeReturn received_rtp(GstPad *pad, GstPadProbeInfo *info, gpointer data)
{
    (void)pad;
    (void)data;
    GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
    if (gst_rtp_buffer_map(GST_PAD_PROBE_INFO_BUFFER(info), GST_MAP_READ, &rtp)) {
        g_atomic_int_set(&received_ssrc, (gint)gst_rtp_buffer_get_ssrc(&rtp));
        gpointer extension = NULL;
        guint size = 0;
        if (gst_rtp_buffer_get_extension_onebyte_header(&rtp, 1, 0, &extension, &size) &&
            size == 1) {
            guint8 value = *(guint8 *)extension;
            if ((value & 0x7f) < 100)
                g_atomic_int_inc(&received_levels);
            if (value & 0x80) /* RFC 6464 V bit, negotiated with vad=on. */
                g_atomic_int_inc(&received_voice);
        }
        gst_rtp_buffer_unmap(&rtp);
    }
    g_atomic_int_inc(&received_packets);
    return GST_PAD_PROBE_OK;
}
static void remote_track(GstElement *rtc, GstPad *pad, gpointer data)
{
    (void)rtc;
    GstElement *pipeline = data;
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC)
        return;
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, received_rtp, NULL, NULL);
    GError *error = NULL;
    GstElement *sink = gst_parse_bin_from_description(
        "queue ! rtpopusdepay ! opusdec ! fakesink sync=false", true, &error);
    g_assert_no_error(error);
    gst_bin_add(GST_BIN(pipeline), sink);
    GstPad *target = gst_element_get_static_pad(sink, "sink");
    g_assert_cmpint(gst_pad_link(pad, target), ==, GST_PAD_LINK_OK);
    gst_object_unref(target);
    g_assert(gst_element_sync_state_with_parent(sink));
}
static void remote_ice(GstElement *rtc, guint line, gchar *candidate, gpointer data)
{
    (void)rtc;
    if (candidate && *candidate)
        g_async_queue_push(data, json_pack("{s:i,s:s}", "line", line, "candidate", candidate));
}
static void answer_offer(GstElement *rtc, voice_audio *audio, const char *text)
{
    GstSDPMessage *sdp = NULL;
    g_assert_cmpint(gst_sdp_message_new_from_text(text, &sdp), ==, GST_SDP_OK);
    GstWebRTCSessionDescription *remote =
        gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp);
    GstPromise *promise = gst_promise_new();
    g_signal_emit_by_name(rtc, "set-remote-description", remote, promise);
    g_assert_cmpint(gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
    gst_promise_unref(promise);
    gst_webrtc_session_description_free(remote);
    promise = gst_promise_new();
    g_signal_emit_by_name(rtc, "create-answer", NULL, promise);
    g_assert_cmpint(gst_promise_wait(promise), ==, GST_PROMISE_RESULT_REPLIED);
    GstWebRTCSessionDescription *answer = NULL;
    g_assert(gst_structure_get(gst_promise_get_reply(promise), "answer",
                               GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, NULL));
    gst_promise_unref(promise);
    promise = gst_promise_new();
    g_signal_emit_by_name(rtc, "set-local-description", answer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);
    char *serialized = gst_sdp_message_as_text(answer->sdp);
    g_assert(voice_audio_description(audio, "answer", serialized));
    g_free(serialized);
    gst_webrtc_session_description_free(answer);
}
static void transport_test(voice_audio *audio, const audio_config *config)
{
    GError *error = NULL;
    GstElement *remote = gst_parse_launch(
        "webrtcbin name=rtc bundle-policy=max-bundle "
        "audiotestsrc is-live=true freq=700 volume=0.15 ! audioconvert ! audioresample ! "
        "audio/x-raw,rate=48000,channels=1 ! opusenc ! rtpopuspay pt=111 ! "
        "application/"
        "x-rtp,media=audio,payload=(int)111,encoding-name=OPUS,clock-rate=48000,encoding-params=("
        "string)2 ! rtc.",
        &error);
    g_assert_no_error(error);
    g_assert_nonnull(remote);
    GstElement *rtc = gst_bin_get_by_name(GST_BIN(remote), "rtc");
    GAsyncQueue *ice = g_async_queue_new();
    g_signal_connect(rtc, "on-ice-candidate", G_CALLBACK(remote_ice), ice);
    g_signal_connect(rtc, "pad-added", G_CALLBACK(remote_track), remote);
    g_assert_cmpint(gst_element_set_state(remote, GST_STATE_PLAYING), !=, GST_STATE_CHANGE_FAILURE);
    json_t *empty = json_array();
    g_assert(voice_audio_start(audio, 2, config, 8, empty, false));
    json_decref(empty);
    gint baseline = test_voice_audible_buffers();
    bool connected = false, offered = false;
    guint declared = 0;
    json_t *early = json_array();
    gint64 deadline = g_get_monotonic_time() + 12000000;
    while (g_get_monotonic_time() < deadline &&
           (!connected || test_voice_audible_buffers() <= baseline + 4 ||
            !g_atomic_int_get(&received_levels) || !g_atomic_int_get(&received_voice))) {
        json_t *event;
        while ((event = voice_audio_pop(audio))) {
            if (json_integer_value(json_object_get(event, "epoch")) == 2) {
                const char *type = string(event, "type");
                g_assert_cmpstr(type, !=, "audio_error");
                if (!strcmp(type, "offer")) {
                    /* Servers bundling several transceivers route by the
                     * declared SSRC, and limit voice events: the offer must
                     * declare the sender and carry the gathered candidates. */
                    const char *sdp = string(event, "sdp"), *ssrc = strstr(sdp, "a=ssrc:");
                    g_assert_nonnull(ssrc);
                    declared = (guint)g_ascii_strtoull(ssrc + 7, NULL, 10);
                    g_assert_nonnull(strstr(sdp, "a=candidate:"));
                    g_assert_cmpuint(json_array_size(early), ==, 0);
                    answer_offer(rtc, audio, sdp);
                    offered = true;
                } else if (!strcmp(type, "candidate"))
                    json_array_append(early, event);
                else if (!strcmp(type, "connection") &&
                         json_integer_value(json_object_get(event, "state")) ==
                             GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED) {
                    connected = true;
                    voice_audio_transmit(audio, true);
                }
            }
            json_decref(event);
        }
        if (offered) {
            for (size_t i = 0; i < json_array_size(early); ++i) {
                event = json_array_get(early, i);
                g_signal_emit_by_name(rtc, "add-ice-candidate",
                                      (guint)json_integer_value(json_object_get(event, "line")),
                                      string(event, "candidate"));
            }
            json_array_clear(early);
            while ((event = g_async_queue_try_pop(ice))) {
                g_assert(voice_audio_candidate(
                    audio, (unsigned)json_integer_value(json_object_get(event, "line")),
                    string(event, "candidate")));
                json_decref(event);
            }
        }
        g_usleep(1000);
    }
    printf("WebRTC connected=%d, playback buffers=%d, received RTP=%d, RFC6464=%d, voice=%d\n",
           connected, test_voice_audible_buffers() - baseline, g_atomic_int_get(&received_packets),
           g_atomic_int_get(&received_levels), g_atomic_int_get(&received_voice));
    g_assert(connected);
    g_assert_cmpint(test_voice_audible_buffers(), >, baseline + 4);
    g_assert_cmpint(g_atomic_int_get(&received_levels), >, 0);
    g_assert_cmpint(g_atomic_int_get(&received_voice), >, 0);
    g_assert_cmpuint((guint)g_atomic_int_get(&received_ssrc), ==, declared);
    /* A silent microphone keeps sending level packets but clears V after the hold. */
    audio_config silent = *config;
    silent.microphone_volume = 0;
    g_assert(voice_audio_configure(audio, &silent, false));
    g_usleep(800000); /* Temporal assertion: exceeds the 400 ms voice hold and network drain. */
    gint voiced = g_atomic_int_get(&received_voice), sent = g_atomic_int_get(&received_packets);
    g_usleep(400000);
    g_assert_cmpint(g_atomic_int_get(&received_packets), >, sent);
    g_assert_cmpint(g_atomic_int_get(&received_voice), ==, voiced);
    g_assert(voice_audio_configure(audio, config, false));
    voice_audio_transmit(audio, false);
    g_usleep(250000); /* Negative temporal assertion: allow in-flight network packets to drain. */
    gint packets = g_atomic_int_get(&received_packets);
    g_usleep(250000);
    g_assert_cmpint(g_atomic_int_get(&received_packets), ==, packets);
    g_assert(voice_audio_output(audio, false));
    g_usleep(250000);
    baseline = test_voice_audible_buffers();
    g_usleep(250000);
    g_assert_cmpint(test_voice_audible_buffers(), ==, baseline);
    puts("Bidirectional Opus, RFC6464 level and voice activity, microphone packet gate and "
         "output mute verified.");
    voice_audio_stop(audio);
    gst_element_set_state(remote, GST_STATE_NULL);
    g_signal_handlers_disconnect_by_data(rtc, ice);
    gst_object_unref(rtc);
    gst_object_unref(remote);
    json_decref(early);
    json_t *event;
    while ((event = g_async_queue_try_pop(ice)))
        json_decref(event);
    g_async_queue_unref(ice);
}
int main(void)
{
    test_voice_devices_register();
    voice_audio *audio = voice_audio_create();
    g_assert_nonnull(audio);
    audio_config config;
    audio_config_default(&config);
    g_assert(voice_audio_devices(audio));
    json_t *event = wait_event(audio, "devices");
    test_voice_devices_select(&config, json_object_get(event, "devices"));
    json_decref(event);
    g_assert(voice_audio_start(audio, 1, &config, 8, NULL, true));
    event = wait_event(audio, "test_started");
    json_decref(event);
    event = wait_event(audio, "level");
    double level = json_number_value(json_object_get(event, "rms"));
    printf("Synthetic microphone RMS: %.2f dBFS\n", level);
    g_assert_cmpfloat(level, >, -60);
    json_decref(event);
    gint64 deadline = g_get_monotonic_time() + 2000000;
    while (!test_voice_audible_buffers() && g_get_monotonic_time() < deadline)
        g_usleep(1000);
    g_assert_cmpint(test_voice_audible_buffers(), >, 0);
    config.microphone_volume = 0;
    g_assert(voice_audio_configure(audio, &config, true));
    bool silent = false;
    deadline = g_get_monotonic_time() + 2000000;
    while (!silent && g_get_monotonic_time() < deadline) {
        event = wait_event(audio, "level");
        silent = json_number_value(json_object_get(event, "rms")) < -90;
        json_decref(event);
    }
    g_assert(silent);
    voice_audio_stop(audio);
    config.microphone_volume = 100;
    transport_test(audio, &config);
    voice_audio_destroy(audio);
    puts("Local microphone monitor, playback and gain verified with synthetic devices.");
    return 0;
}
