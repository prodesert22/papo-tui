#include "ui/media/sound_player.h"

#include <gmodule.h>
#include <mpv/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sound_player {
    mpv_handle *handle;
};

static void drain(mpv_handle *handle)
{
    /* Nothing is observed; keep the event queue from filling up. */
    while (mpv_wait_event(handle, 0)->event_id != MPV_EVENT_NONE) {
    }
}
static sound_player *create(bool silent)
{
    sound_player *p = calloc(1, sizeof(*p));
    if (!p)
        return NULL;
    p->handle = mpv_create();
    if (!p->handle) {
        free(p);
        return NULL;
    }
    const char *options[][2] = {{"config", "no"},
                                {"terminal", "no"},
                                {"load-scripts", "no"},
                                {"ytdl", "no"},
                                {"input-terminal", "no"},
                                {"input-default-bindings", "no"},
                                {"idle", "yes"},
                                {"vid", "no"},
                                {"video", "no"},
                                {"audio-display", "no"},
                                {"access-references", "no"},
                                {"audio-client-name", "papo-tui"},
                                {"audio-fallback-to-null", "yes"},
                                {"ao", silent ? "null" : ""}};
    for (size_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        if (!*options[i][1])
            continue;
        int result = mpv_set_option_string(p->handle, options[i][0], options[i][1]);
        /* Minimal libmpv builds omit scripting and some switches entirely;
         * an absent switch that turns an absent feature off is satisfied. */
        if (result < 0 && result != MPV_ERROR_OPTION_NOT_FOUND) {
            mpv_terminate_destroy(p->handle);
            free(p);
            return NULL;
        }
    }
    if (mpv_initialize(p->handle) < 0) {
        mpv_terminate_destroy(p->handle);
        free(p);
        return NULL;
    }
    return p;
}
static bool play(sound_player *p, const char *uri, int volume)
{
    if (!p || !uri)
        return false;
    drain(p->handle);
    /* Only asynchronous requests: a synchronous one waits for mpv's core, which
     * can be busy opening an audio output that never answers (no PipeWire or
     * PulseAudio in the session), and would freeze the interface meanwhile.
     * Requests of one handle run in order, so the volume applies first. */
    double level = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    if (mpv_set_property_async(p->handle, 0, "volume", MPV_FORMAT_DOUBLE, &level) < 0)
        return false;
    const char *command[] = {"loadfile", uri, "replace", NULL};
    return mpv_command_async(p->handle, 0, command) >= 0;
}
static void destroy(sound_player *p)
{
    if (!p)
        return;
    mpv_terminate_destroy(p->handle);
    free(p);
}

G_MODULE_EXPORT const sound_player_api *papo_sound_backend(unsigned abi, size_t size)
{
    static const sound_player_api api = {PAPO_SOUND_BACKEND_ABI, sizeof(sound_player_api), create,
                                         play, destroy};
    return abi == PAPO_SOUND_BACKEND_ABI && size == sizeof(api) ? &api : NULL;
}
