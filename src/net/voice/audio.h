#ifndef PAPO_VOICE_AUDIO_H
#define PAPO_VOICE_AUDIO_H
#include "config/audio.h"
#include <glib.h>
#include <stdint.h>
typedef struct voice_audio voice_audio;
/* Owns a lazy worker, command/event queues, devices and pipelines. All public
 * calls are on the application thread; the worker never calls auth or curses.
 * Commands copy their arguments, events transfer an owned JSON reference.
 * Epoch tags let the application reject callbacks after cancellation. */
voice_audio *voice_audio_create(void);
void voice_audio_destroy(voice_audio *audio);
bool voice_audio_devices(voice_audio *audio);
bool voice_audio_start(voice_audio *audio, uint64_t epoch, const audio_config *config,
                       unsigned slots, const json_t *ice, bool test_only);
void voice_audio_stop(voice_audio *audio);
bool voice_audio_configure(voice_audio *audio, const audio_config *config, bool monitoring);
/* Atomic packet gate: closes before this function returns, including packets
 * already queued by the encoder. Opening still requires a connected call. */
void voice_audio_transmit(voice_audio *audio, bool enabled);
bool voice_audio_output(voice_audio *audio, bool enabled);
bool voice_audio_description(voice_audio *audio, const char *type, const char *sdp);
bool voice_audio_candidate(voice_audio *audio, unsigned line, const char *candidate);
json_t *voice_audio_pop(voice_audio *audio);
/* Appends a line to the private file named by PAPO_VOICE_DIAGNOSTICS, if any.
 * Callers must never pass SDP, ICE candidates, credentials or cookies. */
void voice_audio_diagnostic(const char *format, ...) G_GNUC_PRINTF(1, 2);
#endif
