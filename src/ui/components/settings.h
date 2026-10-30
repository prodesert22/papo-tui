#ifndef PAPO_SETTINGS_H
#define PAPO_SETTINGS_H
#include "config/profiles.h"
#include "net/voice/client.h"
#include <ncurses.h>
typedef struct {
    bool open, applied, ptt_capture;
    int section, selection, focus;
    app_mode draft;
    audio_config audio, original_audio;
    voice_client *voice; /* Borrowed; the instance model outlives the modal. */
    int audio_selection, audio_first;
    int tab_x[5], tab_w[5], tab_prev, tab_next; /* Hit boxes from the last draw. */
    WINDOW *window;
    char error[192];
} app_settings;
void settings_open(app_settings *settings, const profile_config *config, voice_client *voice);
void settings_close(app_settings *settings);
void settings_draw(app_settings *settings);
void settings_key(app_settings *settings, profile_config *config, int type, uint32_t key);
#endif
