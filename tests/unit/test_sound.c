/* Loads the real papo_sound_player module next to this executable and plays
 * the embedded sounds to libmpv's null output. */
#include "ui/media/sound.h"
#undef NDEBUG
#include <assert.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char *directory = g_dir_make_tmp("papo-sound-XXXXXX", NULL);
    assert(directory);
    char *log = g_build_filename(directory, "sounds.log", NULL);
    assert(g_setenv("PAPO_SOUND_LOG", log, TRUE));
    sound_play(SOUND_MESSAGE);
    /* A burst sounds once. */
    sound_play(SOUND_MENTION);
    g_usleep(450 * 1000);
    sound_play(SOUND_PEER_LEAVE);
    g_usleep(450 * 1000);
    sound_play(SOUND_COUNT); /* Out of range: ignored. */
    sound_play(SOUND_CALL_JOIN);
    sound_shutdown();
    char *text = NULL;
    assert(g_file_get_contents(log, &text, NULL, NULL));
    if (strcmp(text, "sound=message\nsound=peer_leave\nsound=call_join\n")) {
        fprintf(stderr, "unexpected sound log:\n%s", text);
        return 1;
    }
    g_free(text);
    g_remove(log);
    g_rmdir(directory);
    g_free(log);
    g_free(directory);
    return 0;
}
