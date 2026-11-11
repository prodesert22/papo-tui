/* Build without the WebRTC engine: the voice API stays callable so the user
 * interface keeps the same shape, but no call can be joined and no audio
 * device is opened. The stub owns a single inert controller; the callers
 * treat it as an opaque handle. */
#include "model/text/translation.h"
#include "net/voice/client.h"
#include <jansson.h>
#include <stdio.h>
#include <string.h>

struct voice_client {
    char notice[64];
};

static struct voice_client inert;

bool voice_available(void)
{
    return false;
}

voice_client *voice_create(auth_client *auth)
{
    (void)auth;
    return &inert;
}

void voice_destroy(voice_client *voice)
{
    (void)voice;
}

bool voice_poll(voice_client *voice)
{
    (void)voice;
    return false;
}

bool voice_event(voice_client *voice, const json_t *event)
{
    (void)voice;
    (void)event;
    return false;
}

void voice_configure(const audio_config *config)
{
    (void)config;
}

bool voice_join(voice_client *voice, const char *channel)
{
    (void)voice;
    (void)channel;
    snprintf(inert.notice, sizeof(inert.notice), _("Voz indisponível nesta compilação."));
    return false;
}

void voice_leave(voice_client *voice)
{
    (void)voice;
}

bool voice_wants_ice(const voice_client *voice)
{
    (void)voice;
    return false;
}

void voice_ice_response(voice_client *voice, const json_t *body, long status, uint64_t generation)
{
    (void)voice;
    (void)body;
    (void)status;
    (void)generation;
}

voice_state voice_get_state(const voice_client *voice)
{
    (void)voice;
    return VOICE_FAILED;
}

const char *voice_channel(const voice_client *voice)
{
    (void)voice;
    return "";
}

const char *voice_notice(const voice_client *voice)
{
    (void)voice;
    return inert.notice;
}

uint64_t voice_revision(const voice_client *voice)
{
    (void)voice;
    return 0;
}

const json_t *voice_members(const voice_client *voice, const char *channel)
{
    static json_t *empty;
    (void)voice;
    (void)channel;
    if (!empty)
        empty = json_array();
    return empty;
}

bool voice_speaking(const voice_client *voice, const char *channel, const char *user)
{
    (void)voice;
    (void)channel;
    (void)user;
    return false;
}

int voice_muted(const voice_client *voice, const char *channel, const char *user)
{
    (void)voice;
    (void)channel;
    (void)user;
    return -1;
}

voice_client *voice_active(void)
{
    return NULL;
}

const char *voice_instance(const voice_client *voice)
{
    (void)voice;
    return "";
}

void voice_microphone_toggle(void)
{
}

void voice_output_toggle(void)
{
}

bool voice_microphone_enabled(void)
{
    return false;
}

bool voice_output_enabled(void)
{
    return false;
}

void voice_ptt(bool pressed)
{
    (void)pressed;
}

bool voice_ptt_pressed(void)
{
    return false;
}

bool voice_test(voice_client *voice, bool enabled)
{
    (void)voice;
    (void)enabled;
    return false;
}

bool voice_testing(void)
{
    return false;
}

void voice_refresh_devices(voice_client *voice)
{
    (void)voice;
}

const json_t *voice_devices(voice_client *voice)
{
    static json_t *empty;
    (void)voice;
    if (!empty)
        empty = json_array();
    return empty;
}

double voice_level(voice_client *voice, bool peak)
{
    (void)voice;
    (void)peak;
    return -120.0;
}
