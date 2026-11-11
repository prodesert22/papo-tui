#include "ui/media/sound.h"
#include "model/text/translation.h"
#include "net/clock.h"
#include "ui/media/backend_module.h"
#include "ui/media/sound_player.h"
#include <fcntl.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "ui/media/sound_data.inc"

G_STATIC_ASSERT(G_N_ELEMENTS(sound_assets) == SOUND_COUNT);
/* Several alerts in a burst sound once. */
#define SOUND_GAP_MS 400
#define SOUND_VOLUME 70

static const sound_player_api *api;
static sound_player *player;
static bool loaded;
static char *uris[SOUND_COUNT];
static uint64_t last_played;

static const sound_player_api *load(void)
{
    if (loaded)
        return api;
    loaded = true;
    GModule *module = backend_module_open(PAPO_SOUND_MODULE);
    gpointer symbol = NULL;
    sound_player_entry entry = NULL;
    G_STATIC_ASSERT(sizeof(entry) == sizeof(symbol));
    if (!module || !g_module_symbol(module, "papo_sound_backend", &symbol)) {
        if (module)
            g_module_close(module);
        return NULL;
    }
    memcpy(&entry, &symbol, sizeof(entry));
    const sound_player_api *candidate = entry(PAPO_SOUND_BACKEND_ABI, sizeof(sound_player_api));
    if (!candidate || candidate->abi != PAPO_SOUND_BACKEND_ABI ||
        candidate->size != sizeof(*candidate) || !candidate->create || !candidate->play ||
        !candidate->destroy) {
        g_module_close(module);
        return NULL;
    }
    g_module_make_resident(module);
    g_module_close(module);
    api = candidate;
    player = api->create(getenv("PAPO_SOUND_LOG") != NULL);
    return player ? api : NULL;
}
/* mpv's hex:// stream takes the bytes as hexadecimal text: no file needed. */
static const char *uri(sound_id sound)
{
    if (!uris[sound]) {
        static const char digits[] = "0123456789abcdef";
        size_t size = sound_assets[sound].size;
        char *text = malloc(6 + size * 2 + 1);
        if (!text)
            return NULL;
        memcpy(text, "hex://", 6);
        for (size_t i = 0; i < size; ++i) {
            text[6 + i * 2] = digits[sound_assets[sound].data[i] >> 4];
            text[6 + i * 2 + 1] = digits[sound_assets[sound].data[i] & 15];
        }
        text[6 + size * 2] = 0;
        uris[sound] = text;
    }
    return uris[sound];
}
static void record(const char *name, bool played)
{
    const char *path = getenv("PAPO_SOUND_LOG");
    if (!path || !*path)
        return;
#ifdef _WIN32
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, 0600);
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
#endif
    if (fd < 0)
        return;
    char line[64];
    int length = snprintf(line, sizeof(line), "sound=%s%s\n", name, played ? "" : " unavailable");
    if (length > 0) {
#ifdef _WIN32
        (void)!_write(fd, line, (unsigned)length);
        _close(fd);
#else
        (void)!write(fd, line, (size_t)length);
        close(fd);
#endif
    }
}

void sound_play(sound_id sound)
{
    if ((unsigned)sound >= SOUND_COUNT)
        return;
    uint64_t now = 0;
    if (!net_now_ms(&now) || (last_played && now - last_played < SOUND_GAP_MS))
        return;
    last_played = now;
    const char *address = load() ? uri(sound) : NULL;
    record(sound_assets[sound].name, address && api->play(player, address, SOUND_VOLUME));
}
void sound_shutdown(void)
{
    if (api && player)
        api->destroy(player);
    player = NULL;
    for (size_t i = 0; i < SOUND_COUNT; ++i) {
        free(uris[i]);
        uris[i] = NULL;
    }
}
