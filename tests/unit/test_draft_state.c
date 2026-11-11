#define _POSIX_C_SOURCE 200809L
#include "config/draft_state.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *url = "https://example.com/papo";
static const char *user = "00000000-0000-4000-8000-000000000001";
static const char *other = "00000000-0000-4000-8000-000000000002";
static const char *channel = "00000000-0000-4000-8000-000000000010";
static const char *second = "00000000-0000-4000-8000-000000000011";
static const char *reply = "00000000-0000-4000-8000-000000000103";

static json_t *drafts(const char *text, const char *target)
{
    json_t *map = json_pack("{s:{s:s,s:o}}", channel, "text", text, "reply_to",
                            target ? json_string(target) : json_null());
    assert(map);
    return map;
}

static char *file_path(const char *root)
{
    static char path[4096];
    snprintf(path, sizeof(path), "%s/papo-tui/drafts.json", root);
    return path;
}

int main(void)
{
    char root[] = "/tmp/papo-drafts-XXXXXX";
    assert(mkdtemp(root));
    assert(setenv("XDG_STATE_HOME", root, 1) == 0);

    /* Nothing saved yet, or an invalid scope: an empty map. */
    json_t *loaded = draft_state_load(url, user);
    assert(json_is_object(loaded) && !json_object_size(loaded));
    json_decref(loaded);
    loaded = draft_state_load("https://Example.com/papo/", user);
    assert(json_is_object(loaded) && !json_object_size(loaded));
    json_decref(loaded);

    /* Text with a new line and a tab, and a reply target, survive a round trip. */
    json_t *map = drafts("Olá\n\tmundo 😀", reply);
    assert(draft_state_save(url, user, map));
    json_decref(map);
    struct stat st;
    assert(stat(file_path(root), &st) == 0 && (st.st_mode & 07777) == 0600);
    loaded = draft_state_load(url, user);
    json_t *entry = json_object_get(loaded, channel);
    assert(entry && !strcmp(json_string_value(json_object_get(entry, "text")), "Olá\n\tmundo 😀"));
    assert(!strcmp(json_string_value(json_object_get(entry, "reply_to")), reply));
    json_decref(loaded);

    /* Another account on the same server sees none of it. */
    loaded = draft_state_load(url, other);
    assert(!json_object_size(loaded));
    json_decref(loaded);

    /* Saving replaces the account's map: conversations left out are gone. */
    map = json_pack("{s:{s:s,s:n}}", second, "text", "Outro", "reply_to");
    assert(draft_state_save(url, user, map));
    json_decref(map);
    loaded = draft_state_load(url, user);
    assert(json_object_size(loaded) == 1 && json_object_get(loaded, second));
    json_decref(loaded);

    /* Invalid input is refused and leaves the file as it was. */
    json_t *invalid[] = {
        json_pack("{s:{s:s,s:n}}", "not-a-uuid", "text", "x", "reply_to"),
        json_pack("{s:{s:i,s:n}}", channel, "text", 1, "reply_to"),
        json_pack("{s:{s:s,s:s}}", channel, "text", "x", "reply_to", "bad"),
        json_pack("{s:{s:s}}", channel, "text", "x"),
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        assert(invalid[i] && !draft_state_save(url, user, invalid[i]));
        json_decref(invalid[i]);
    }
    char *long_text = malloc(DRAFT_STATE_TEXT_BYTES + 2);
    assert(long_text);
    memset(long_text, 'a', DRAFT_STATE_TEXT_BYTES + 1);
    long_text[DRAFT_STATE_TEXT_BYTES + 1] = 0;
    map = drafts(long_text, NULL);
    assert(!draft_state_save(url, user, map));
    json_decref(map);
    free(long_text);
    loaded = draft_state_load(url, user);
    assert(json_object_size(loaded) == 1 && json_object_get(loaded, second));
    json_decref(loaded);

    /* Control characters written by another program never reach the editor. */
    map = drafts("antes\x1b]52;c;x\x07"
                 "depois\r",
                 NULL);
    assert(draft_state_save(url, user, map));
    json_decref(map);
    loaded = draft_state_load(url, user);
    assert(!strcmp(json_string_value(json_object_get(json_object_get(loaded, channel), "text")),
                   "antes]52;c;xdepois"));
    json_decref(loaded);

    /* Signing out forgets the account; the other account's drafts stay. */
    map = drafts("da outra conta", NULL);
    assert(draft_state_save(url, other, map));
    json_decref(map);
    assert(draft_state_forget(url, user));
    loaded = draft_state_load(url, user);
    assert(!json_object_size(loaded));
    json_decref(loaded);
    loaded = draft_state_load(url, other);
    assert(json_object_size(loaded) == 1);
    json_decref(loaded);

    /* A damaged file loads as nothing instead of failing the client. */
    FILE *damaged = fopen(file_path(root), "w");
    assert(damaged && fputs("{broken", damaged) >= 0 && fclose(damaged) == 0);
    loaded = draft_state_load(url, other);
    assert(json_is_object(loaded) && !json_object_size(loaded));
    json_decref(loaded);

    unlink(file_path(root));
    char directory[4200];
    snprintf(directory, sizeof(directory), "%s/papo-tui", root);
    rmdir(directory);
    rmdir(root);
    return 0;
}
