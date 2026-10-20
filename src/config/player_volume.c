#include "config/player_volume.h"
#include <stdatomic.h>
static atomic_int current_volume = PLAYER_VOLUME_DEFAULT;
void player_volume_set(int level)
{
    if (level < PLAYER_VOLUME_MIN)
        level = PLAYER_VOLUME_MIN;
    if (level > PLAYER_VOLUME_MAX)
        level = PLAYER_VOLUME_MAX;
    atomic_store(&current_volume, level);
}
int player_volume_get(void)
{
    return atomic_load(&current_volume);
}
