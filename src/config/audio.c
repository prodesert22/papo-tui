#define _POSIX_C_SOURCE 200809L
#include "config/audio.h"
#include <glib.h>
#include <stdio.h>
#include <string.h>
static void ptt_default(key_binding *binding)
{
    binding->kind = KEY_BINDING_CHAR;
    binding->code = 'y';
    binding->modifiers = KEY_BINDING_CTRL;
}
static bool ptt_functional_key(const key_binding *binding)
{
    /* Kitty reports modifiers and other functional keys in the private-use
     * area. A saved capture must never send those codes as a text key. */
    return binding->kind == KEY_BINDING_CHAR && binding->code >= 0xe000 && binding->code <= 0xf8ff;
}
void audio_config_default(audio_config *c)
{
    memset(c, 0, sizeof(*c));
    c->microphone_volume = c->output_volume = 100;
    ptt_default(&c->ptt);
}
static bool device_valid(const char *s)
{
    size_t length = strnlen(s, AUDIO_DEVICE_BYTES);
    return length < AUDIO_DEVICE_BYTES && g_utf8_validate(s, (gssize)length, NULL);
}
bool audio_config_valid(const audio_config *c)
{
    if (!c || !device_valid(c->input) || !device_valid(c->output) || c->microphone_volume < 0 ||
        c->microphone_volume > 100 || c->output_volume < 0 || c->output_volume > 100 ||
        !key_binding_valid(&c->ptt))
        return false;
    for (size_t i = 0; i < AUDIO_INSTANCE_LIMIT; ++i) {
        const audio_instance_config *v = &c->instances[i];
        if (!*v->origin)
            continue;
        if (strnlen(v->origin, sizeof(v->origin)) != 64 || v->slots < 1 || v->slots > 64)
            return false;
        for (size_t j = 0; j < 64; ++j)
            if (!g_ascii_isxdigit(v->origin[j]))
                return false;
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(v->origin, c->instances[j].origin))
                return false;
    }
    return true;
}
bool audio_config_decode(audio_config *c, const json_t *value)
{
    audio_config next;
    audio_config_default(&next);
    if (!json_is_object(value) || json_object_size(value) != 6)
        return false;
    const char *keys[] = {"input", "output"};
    char *targets[] = {next.input, next.output};
    for (size_t i = 0; i < 2; ++i) {
        json_t *s = json_object_get(value, keys[i]);
        if (!json_is_string(s) || json_string_length(s) >= AUDIO_DEVICE_BYTES ||
            strlen(json_string_value(s)) != json_string_length(s))
            return false;
        snprintf(targets[i], AUDIO_DEVICE_BYTES, "%s", json_string_value(s));
    }
    json_t *mic = json_object_get(value, "microphone_volume"),
           *out = json_object_get(value, "output_volume"),
           *ptt = json_object_get(value, "push_to_talk"),
           *key = json_is_object(ptt) ? json_object_get(ptt, "key") : NULL,
           *enabled = json_is_object(ptt) ? json_object_get(ptt, "enabled") : NULL,
           *slots = json_object_get(value, "server_slots");
    if (!json_is_integer(mic) || json_integer_value(mic) < 0 || json_integer_value(mic) > 100 ||
        !json_is_integer(out) || json_integer_value(out) < 0 || json_integer_value(out) > 100 ||
        !json_is_object(ptt) || json_object_size(ptt) != 2 || !json_is_boolean(enabled) ||
        (!json_is_integer(key) && !json_is_string(key)) || !json_is_object(slots) ||
        json_object_size(slots) > AUDIO_INSTANCE_LIMIT)
        return false;
    if (json_is_integer(key)) {
        json_int_t function_key = json_integer_value(key);
        if (function_key < 2 || function_key > 12 ||
            !key_binding_legacy((unsigned)function_key, &next.ptt))
            return false;
    } else {
        const char *text = json_string_value(key);
        if (json_string_length(key) >= 64 || strlen(text) != json_string_length(key) ||
            !key_binding_parse(text, &next.ptt))
            return false;
        if (ptt_functional_key(&next.ptt))
            ptt_default(&next.ptt);
    }
    next.microphone_volume = (int)json_integer_value(mic);
    next.output_volume = (int)json_integer_value(out);
    next.push_to_talk = json_is_true(enabled);
    const char *origin;
    json_t *number;
    size_t n = 0;
    json_object_foreach(slots, origin, number)
    {
        if (strlen(origin) != 64 || !json_is_integer(number) || json_integer_value(number) < 1 ||
            json_integer_value(number) > 64)
            return false;
        snprintf(next.instances[n].origin, 65, "%s", origin);
        next.instances[n++].slots = (unsigned)json_integer_value(number);
    }
    if (!audio_config_valid(&next))
        return false;
    *c = next;
    return true;
}
json_t *audio_config_encode(const audio_config *c)
{
    if (!audio_config_valid(c))
        return NULL;
    char key[64];
    if (!key_binding_format(&c->ptt, key, sizeof(key)))
        return NULL;
    json_t *slots = json_object();
    if (!slots)
        return NULL;
    for (size_t i = 0; i < AUDIO_INSTANCE_LIMIT; ++i)
        if (*c->instances[i].origin &&
            json_object_set_new(slots, c->instances[i].origin,
                                json_integer(c->instances[i].slots)) < 0) {
            json_decref(slots);
            return NULL;
        }
    return json_pack("{s:s,s:s,s:i,s:i,s:{s:b,s:s},s:o}", "input", c->input, "output", c->output,
                     "microphone_volume", c->microphone_volume, "output_volume", c->output_volume,
                     "push_to_talk", "enabled", c->push_to_talk, "key", key, "server_slots", slots);
}
unsigned audio_config_slots(const audio_config *c, const char *url)
{
    char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, url ? url : "", -1);
    unsigned result = 8;
    if (hash && c)
        for (size_t i = 0; i < AUDIO_INSTANCE_LIMIT; ++i)
            if (!strcmp(hash, c->instances[i].origin))
                result = c->instances[i].slots;
    g_free(hash);
    return result;
}
bool audio_config_set_slots(audio_config *c, const char *url, unsigned slots)
{
    if (!c || !url || !*url || slots < 1 || slots > 64)
        return false;
    char *hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, url, -1);
    if (!hash)
        return false;
    size_t index = AUDIO_INSTANCE_LIMIT;
    for (size_t i = 0; i < AUDIO_INSTANCE_LIMIT; ++i) {
        if (!strcmp(hash, c->instances[i].origin)) {
            index = i;
            break;
        }
        if (index == AUDIO_INSTANCE_LIMIT && !*c->instances[i].origin)
            index = i;
    }
    if (index != AUDIO_INSTANCE_LIMIT) {
        snprintf(c->instances[index].origin, 65, "%s", hash);
        c->instances[index].slots = slots;
    }
    g_free(hash);
    return index != AUDIO_INSTANCE_LIMIT;
}
