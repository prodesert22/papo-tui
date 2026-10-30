#include "config/audio.h"
#include "net/voice/client.h"
#include <assert.h>
#include <string.h>
static const char *channel = "11111111-1111-4111-8111-111111111111";
static const char *user = "22222222-2222-4222-8222-222222222222";
static void test_key_binding(void)
{
    key_binding binding;
    char text[64], label[64];
    assert(key_binding_parse("ctrl+shift+m", &binding));
    assert(binding.kind == KEY_BINDING_CHAR && binding.code == 'm' &&
           binding.modifiers == (KEY_BINDING_CTRL | KEY_BINDING_SHIFT));
    assert(key_binding_valid(&binding));
    assert(key_binding_format(&binding, text, sizeof(text)));
    assert(!strcmp(text, "ctrl+shift+m"));
    assert(key_binding_label(&binding, label, sizeof(label)));
    assert(!strcmp(label, "Ctrl+Shift+M"));
    assert(key_binding_parse("ctrl+y", &binding) && key_binding_valid(&binding));
    assert(key_binding_label(&binding, label, sizeof(label)));
    assert(!strcmp(label, "Ctrl+Y"));
    assert(key_binding_legacy_ctrl_byte(&binding) == 0x19);
    assert(key_binding_parse("ctrl+1", &binding) && !key_binding_legacy_ctrl_byte(&binding));
    assert(key_binding_parse("alt+y", &binding) && !key_binding_legacy_ctrl_byte(&binding));
    assert(key_binding_parse("ctrl+backspace", &binding) &&
           !key_binding_legacy_ctrl_byte(&binding));
    assert(key_binding_parse("ctrl+h", &binding) && !key_binding_legacy_ctrl_byte(&binding));
    assert(key_binding_parse("alt+space", &binding) && key_binding_valid(&binding));
    assert(key_binding_parse("ctrl+u002b", &binding) && binding.code == '+' &&
           key_binding_valid(&binding));
    assert(key_binding_format(&binding, text, sizeof(text)));
    assert(!strcmp(text, "ctrl+u002b"));
    assert(key_binding_parse("shift+tab", &binding) && key_binding_valid(&binding));
    assert(key_binding_parse("shift+left", &binding) && key_binding_valid(&binding));
    assert(key_binding_parse("mouse4", &binding) && binding.kind == KEY_BINDING_MOUSE &&
           binding.code == 4 && key_binding_valid(&binding));
    assert(key_binding_parse("ctrl+mouse5", &binding) && binding.code == 5 && !binding.modifiers &&
           key_binding_valid(&binding));
    assert(!key_binding_parse("mouse6", &binding));
    assert(!key_binding_parse("ctrl+", &binding));
    assert(!key_binding_parse("ctrl+shift", &binding));
    assert(key_binding_parse("ctrl+\xee\x81\xa2", &binding));
    assert(!key_binding_valid(&binding));
    assert(key_binding_parse("m", &binding) && !key_binding_valid(&binding));
    assert(key_binding_parse("shift+m", &binding) && !key_binding_valid(&binding));
    assert(key_binding_parse("tab", &binding) && !key_binding_valid(&binding));
    assert(key_binding_parse("ctrl+f12", &binding) && key_binding_valid(&binding));
    assert(key_binding_parse("f12", &binding) && key_binding_valid(&binding));
    assert(key_binding_legacy(2, &binding) && binding.kind == KEY_BINDING_KEY &&
           binding.code == KEY_BINDING_F2 && binding.modifiers == KEY_BINDING_CTRL);
    assert(key_binding_legacy(12, &binding) && binding.code == KEY_BINDING_F12);
    assert(!key_binding_legacy(1, &binding) && !key_binding_legacy(13, &binding));
}
int main(void)
{
    test_key_binding();
    audio_config config, decoded;
    audio_config_default(&config);
    assert(config.microphone_volume == 100 && config.output_volume == 100 && !config.push_to_talk);
    assert(config.ptt.kind == KEY_BINDING_CHAR && config.ptt.code == 'y' &&
           config.ptt.modifiers == KEY_BINDING_CTRL);
    assert(audio_config_slots(&config, "https://a.invalid") == 8);
    assert(audio_config_set_slots(&config, "https://a.invalid", 4));
    assert(audio_config_set_slots(&config, "https://b.invalid", 12));
    assert(audio_config_slots(&config, "https://a.invalid") == 4);
    assert(audio_config_slots(&config, "https://b.invalid") == 12);
    assert(!audio_config_set_slots(&config, "https://b.invalid", 65));
    json_t *encoded = audio_config_encode(&config);
    assert(encoded && audio_config_decode(&decoded, encoded));
    assert(!memcmp(&config, &decoded, sizeof(config)));
    json_t *key = json_object_get(json_object_get(encoded, "push_to_talk"), "key");
    assert(json_is_string(key) && !strcmp(json_string_value(key), "ctrl+y"));
    json_object_set_new(json_object_get(encoded, "push_to_talk"), "key", json_integer(5));
    assert(audio_config_decode(&decoded, encoded));
    assert(decoded.ptt.kind == KEY_BINDING_KEY && decoded.ptt.code == KEY_BINDING_F5 &&
           decoded.ptt.modifiers == KEY_BINDING_CTRL);
    json_object_set_new(json_object_get(encoded, "push_to_talk"), "key", json_string("mouse4"));
    assert(audio_config_decode(&decoded, encoded));
    assert(decoded.ptt.kind == KEY_BINDING_MOUSE && decoded.ptt.code == 4);
    json_object_set_new(json_object_get(encoded, "push_to_talk"), "key", json_string("m"));
    assert(!audio_config_decode(&decoded, encoded));
    /* Older captures could save a lone modifier as a private-use character. */
    json_object_set_new(json_object_get(encoded, "push_to_talk"), "key",
                        json_string("ctrl+\xee\x81\xa2"));
    assert(audio_config_decode(&decoded, encoded));
    assert(decoded.ptt.kind == KEY_BINDING_CHAR && decoded.ptt.code == 'y' &&
           decoded.ptt.modifiers == KEY_BINDING_CTRL);
    json_object_set_new(encoded, "microphone_volume", json_integer(-1));
    assert(!audio_config_decode(&decoded, encoded));
    json_decref(encoded);
    auth_client *auth_a = auth_create(), *auth_b = auth_create();
    assert(auth_a && auth_b);
    voice_client *a = voice_create(auth_a), *b = voice_create(auth_b);
    assert(a && b);
    json_t *sync = json_pack("{s:s,s:[{s:s,s:[s]}]}", "type", "presence_sync", "members", "user_id",
                             user, "user_voice", channel);
    assert(sync && voice_event(a, sync));
    assert(json_array_size(voice_members(a, channel)) == 1);
    assert(json_array_size(voice_members(b, channel)) == 0);
    json_decref(sync);
    json_t *speaking = json_pack("{s:s,s:s,s:[s]}", "type", "active_speaker_update", "channel_id",
                                 channel, "user_ids", user);
    assert(voice_event(a, speaking));
    assert(voice_speaking(a, channel, user) && !voice_speaking(b, channel, user));
    json_decref(speaking);
    assert(voice_muted(a, channel, user) == -1);
    json_t *state =
        json_pack("{s:s,s:s,s:s,s:b,s:b,s:b}", "type", "voice_state_update", "channel_id", channel,
                  "user_id", user, "muted", 1, "camera_on", 0, "screen_sharing", 0);
    assert(state && voice_event(a, state));
    assert(voice_muted(a, channel, user) == 1);
    assert(voice_muted(b, channel, user) == -1);
    json_decref(state);
    state = json_pack("{s:s,s:s,s:s,s:b,s:b,s:b}", "type", "voice_state_update", "channel_id",
                      channel, "user_id", user, "muted", 0, "camera_on", 0, "screen_sharing", 0);
    assert(state && voice_event(a, state));
    assert(voice_muted(a, channel, user) == 0);
    json_decref(state);
    json_t *leave =
        json_pack("{s:s,s:s,s:s}", "type", "voice_leave", "channel_id", channel, "user_id", user);
    assert(voice_event(a, leave));
    assert(!voice_speaking(a, channel, user) && !json_array_size(voice_members(a, channel)));
    assert(voice_muted(a, channel, user) == -1);
    json_decref(leave);
    sync = json_pack("{s:s,s:[{s:s}]}", "type", "presence_sync", "members", "user_id", user);
    assert(voice_event(a, sync)); /* Old servers have no user_voice field. */
    assert(!voice_members(a, channel));
    json_decref(sync);
    voice_destroy(a);
    voice_destroy(b);
    auth_destroy(auth_a);
    auth_destroy(auth_b);
    return 0;
}
