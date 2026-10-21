#ifndef PAPO_PLAYER_VOLUME_H
#define PAPO_PLAYER_VOLUME_H
/* Runtime player volume shared by every media view; the profile persists it.
 * The media helper owns a single player, so one level serves every tab. */
#define PLAYER_VOLUME_DEFAULT 50
#define PLAYER_VOLUME_MIN 0
#define PLAYER_VOLUME_MAX 100
void player_volume_set(int level);
int player_volume_get(void);
#endif
