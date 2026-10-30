#ifndef PAPO_VOICE_CLIENT_H
#define PAPO_VOICE_CLIENT_H
#include "config/audio.h"
#include "net/auth/client.h"
typedef enum {
    VOICE_IDLE,
    VOICE_ICE,
    VOICE_JOINING,
    VOICE_NEGOTIATING,
    VOICE_CONNECTED,
    VOICE_LEAVING,
    VOICE_FAILED
} voice_state;
typedef struct voice_client voice_client;
/* Main-thread controller. Owns directory/worker/JSON; borrows auth, which must
 * outlive it. One global active call; directories remain instance-local. */
bool voice_available(void); /* False when the build has no WebRTC support. */
voice_client *voice_create(auth_client *auth);
void voice_destroy(voice_client *voice);
bool voice_poll(voice_client *voice);
bool voice_event(voice_client *voice, const json_t *event);
void voice_configure(const audio_config *config);
bool voice_join(voice_client *voice, const char *channel);
void voice_leave(voice_client *voice);
bool voice_wants_ice(const voice_client *voice);
void voice_ice_response(voice_client *voice, const json_t *body, long status, uint64_t generation);
voice_state voice_get_state(const voice_client *voice);
const char *voice_channel(const voice_client *voice);
const char *voice_notice(const voice_client *voice);
uint64_t voice_revision(const voice_client *voice);
const json_t *voice_members(const voice_client *voice, const char *channel); /* Borrowed IDs. */
bool voice_speaking(const voice_client *voice, const char *channel, const char *user);
/* 1 muted, 0 unmuted, -1 when the server has not reported this member yet. */
int voice_muted(const voice_client *voice, const char *channel, const char *user);
voice_client *voice_active(void); /* Borrowed. */
const char *voice_instance(const voice_client *voice);
void voice_microphone_toggle(void);
void voice_output_toggle(void);
bool voice_microphone_enabled(void);
bool voice_output_enabled(void);
void voice_ptt(bool pressed);
bool voice_ptt_pressed(void);
bool voice_test(voice_client *voice, bool enabled);
bool voice_testing(void);
void voice_refresh_devices(voice_client *voice);
const json_t *voice_devices(voice_client *voice); /* Borrowed device descriptors. */
double voice_level(voice_client *voice, bool peak);
#endif
