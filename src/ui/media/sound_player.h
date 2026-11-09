#ifndef PAPO_SOUND_PLAYER_H
#define PAPO_SOUND_PLAYER_H
#include <stdbool.h>
#include <stddef.h>
/* Interface of the papo_sound_player module, which keeps libmpv out of the
 * main binary. It only plays the client's own embedded sounds. */
#define PAPO_SOUND_BACKEND_ABI 1U
typedef struct sound_player sound_player;
typedef struct {
    unsigned abi;
    size_t size;
    /* silent: decode and "play" to a null output, for tests. */
    sound_player *(*create)(bool silent);
    /* Copies uri; replaces the sound that is playing. Never blocks. */
    bool (*play)(sound_player *player, const char *uri, int volume);
    void (*destroy)(sound_player *player);
} sound_player_api;
typedef const sound_player_api *(*sound_player_entry)(unsigned abi, size_t size);
#endif
