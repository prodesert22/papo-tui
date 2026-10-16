#ifndef PAPO_SETTINGS_H
#define PAPO_SETTINGS_H
#include "config/profiles.h"
#include <ncurses.h>
typedef struct {
    bool open, applied;
    int section, selection, focus;
    app_mode draft;
    WINDOW *window;
    char error[192];
} app_settings;
void settings_open(app_settings *settings, const profile_config *config);
void settings_close(app_settings *settings);
void settings_draw(app_settings *settings);
void settings_key(app_settings *settings, profile_config *config, int type, uint32_t key);
#endif
