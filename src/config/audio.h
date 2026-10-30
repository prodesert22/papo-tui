#ifndef PAPO_AUDIO_CONFIG_H
#define PAPO_AUDIO_CONFIG_H
#include "config/key_binding.h"
#include <jansson.h>
#include <stdbool.h>
#define AUDIO_DEVICE_BYTES 512
#define AUDIO_INSTANCE_LIMIT 64
typedef struct {
    char origin[65]; /* SHA-256 of the canonical instance URL; no identity or secrets. */
    unsigned slots;
} audio_instance_config;
typedef struct {
    char input[AUDIO_DEVICE_BYTES], output[AUDIO_DEVICE_BYTES];
    int microphone_volume, output_volume;
    bool push_to_talk;
    key_binding ptt; /* Keyboard combination or mouse button; never plain text. */
    audio_instance_config instances[AUDIO_INSTANCE_LIMIT];
} audio_config;
void audio_config_default(audio_config *config);
bool audio_config_valid(const audio_config *config);
bool audio_config_decode(audio_config *config, const json_t *value);
json_t *audio_config_encode(const audio_config *config); /* Caller owns result. */
unsigned audio_config_slots(const audio_config *config, const char *url);
bool audio_config_set_slots(audio_config *config, const char *url, unsigned slots);
#endif
