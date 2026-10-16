#ifndef PAPO_EMOJI_USAGE_H
#define PAPO_EMOJI_USAGE_H
#include <stdint.h>
typedef struct emoji_usage emoji_usage;
emoji_usage *emoji_usage_create(const char *instance, const char *user);
void emoji_usage_destroy(emoji_usage *usage);
uint64_t emoji_usage_count(const emoji_usage *usage, const char *key);
/* Records successful explicit insertions; unavailable storage keeps memory use. */
void emoji_usage_record(emoji_usage *usage, const char *key);
#endif
