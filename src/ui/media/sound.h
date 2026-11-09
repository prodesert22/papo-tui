#ifndef PAPO_SOUND_H
#define PAPO_SOUND_H
#include <stdbool.h>
/* The client's own short sounds, played through the papo_sound_player module
 * on the event loop thread. Callers decide whether a sound is allowed. */
typedef enum {
    SOUND_MESSAGE,
    SOUND_MENTION,
    SOUND_CALL_JOIN,
    SOUND_CALL_LEAVE,
    SOUND_PEER_JOIN,
    SOUND_PEER_LEAVE,
    SOUND_COUNT
} sound_id;
/* Starts sound unless another one started moments ago; never blocks. Loads
 * the module once; without it the client stays silent. PAPO_SOUND_LOG names a
 * private file that gets one "sound=<name>" line per sound, which then plays
 * to a null output so tests make no noise. */
void sound_play(sound_id sound);
void sound_shutdown(void);
#endif
