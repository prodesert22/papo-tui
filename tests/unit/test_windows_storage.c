#include "app/session_store.h"
#include "config/profiles.h"
#include "platform/runtime.h"
#include "platform/windows/files.h"
#include <assert.h>
#include <curl/curl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>
#include <wincred.h>

typedef struct {
    char *path, *lock;
    gunichar2 *target;
} saved_slot;
/* Writes a committed marker and credential with the names of one slot; slot 0
 * keeps the names of the former single-session store. */
static saved_slot save_slot(const char *directory, const char *scope, unsigned sequence,
                            const char *url, char *snapshot)
{
    char *name = sequence ? g_strdup_printf("%s-%u", scope, sequence) : g_strdup(scope);
    char *file = g_strdup_printf("session-%s.json", name);
    char *target = g_strdup_printf("PapoTUI/%s", name);
    saved_slot slot = {g_build_filename(directory, file, NULL),
                       g_strdup_printf("%s/session-%s.lock", directory, name),
                       g_utf8_to_utf16(target, -1, NULL, NULL, NULL)};
    CREDENTIALW credential = {.Type = CRED_TYPE_GENERIC,
                              .TargetName = (LPWSTR)slot.target,
                              .CredentialBlobSize = (DWORD)strlen(snapshot),
                              .CredentialBlob = (LPBYTE)snapshot,
                              .Persist = CRED_PERSIST_LOCAL_MACHINE};
    assert(slot.target && CredWriteW(&credential, 0));
    char *marker = g_strdup_printf("{\"version\":1,\"url\":\"%s\",\"restore_allowed\":true,"
                                   "\"provider\":4,\"revision\":1}",
                                   url);
    assert(papo_win_write_private(slot.path, marker, strlen(marker)));
    g_free(marker);
    g_free(target);
    g_free(file);
    g_free(name);
    return slot;
}
static void remove_slot(saved_slot *slot)
{
    assert(CredDeleteW((LPCWSTR)slot->target, CRED_TYPE_GENERIC, 0));
    assert(g_unlink(slot->path) == 0 && g_unlink(slot->lock) == 0);
    g_free(slot->target);
    g_free(slot->lock);
    g_free(slot->path);
}
static void check_pending_restore(profile_config *config)
{
    char *scope = g_compute_checksum_for_string(G_CHECKSUM_SHA256, config->directory_path, -1);
    char legacy_snapshot[] = "synthetic-restore-fixture", added_snapshot[] = "synthetic-added";
    saved_slot legacy =
        save_slot(config->directory_path, scope, 0, "https://one.example.test", legacy_snapshot);
    saved_slot added =
        save_slot(config->directory_path, scope, 1, "https://two.example.test", added_snapshot);
    char *store_lock = g_strdup_printf("%s/sessions-%s.lock", config->directory_path, scope);
    session_store *store = session_store_create(config, true);
    assert(store && session_store_restoring(store));
    /* The lookup callbacks have not been dispatched yet. An empty take must
     * preserve the pending operations, regardless of worker completion timing. */
    const char *url = NULL;
    assert(!session_store_take_resume(store, &url));
    assert(session_store_restoring(store));
    gint64 deadline = g_get_monotonic_time() + 5000000;
    bool one = false, two = false;
    while (!(one && two) && g_get_monotonic_time() < deadline) {
        session_store_poll(store, NULL);
        char *resume = session_store_take_resume(store, &url);
        if (resume) {
            if (!strcmp(url, "https://one.example.test")) {
                assert(!one && !strcmp(resume, legacy_snapshot));
                one = true;
            } else {
                assert(!two && !strcmp(url, "https://two.example.test") &&
                       !strcmp(resume, added_snapshot));
                two = true;
            }
            auth_session_free(&resume);
        } else
            g_usleep(1000);
    }
    assert(one && two);
    assert(!strcmp(session_store_last_url(store), "https://two.example.test"));
    assert(!session_store_restoring(store) && !session_store_take_resume(store, &url));
    session_store_destroy(store);

    store = session_store_create(config, true);
    assert(store && session_store_restoring(store));
    session_store_cancel_resume(store);
    assert(!session_store_restoring(store) && !session_store_take_resume(store, &url));
    session_store_destroy(store);
    remove_slot(&legacy);
    remove_slot(&added);
    assert(g_unlink(store_lock) == 0);
    g_free(store_lock);
    g_free(scope);
}

int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    char *root = g_build_filename(g_get_tmp_dir(), "papo-config-á-XXXXXX", NULL);
    assert(papo_mkdtemp(root));
    g_setenv("XDG_CONFIG_HOME", root, TRUE);
    g_setenv("XDG_STATE_HOME", root, TRUE);
    profile_config *first = profiles_create(), *stale = profiles_create();
    assert(first && stale && first->writable && stale->writable);
    check_pending_restore(first);
    char id[PROFILE_ID_BYTES], other[PROFILE_ID_BYTES];
    assert(profiles_update(first, NULL, "Instância á 😀", "https://one.example.test", false, id));
    assert(first->count == 1 && profiles_find(first, id));
    assert(!profiles_update(stale, NULL, "Stale", "https://two.example.test", false, other));
    assert(stale->count == 0 && profiles_reload(stale) && stale->count == 1);
    assert(!strcmp(stale->profiles[0].name, "Instância á 😀"));
    assert(profiles_set_notifications(stale, true, false));
    assert(profiles_reload(first) && first->desktop_notifications && !first->notification_preview);
    char *lockpath = g_build_filename(first->directory_path, "config.lock", NULL);
    HANDLE lock = papo_win_lock(lockpath);
    assert(lock != INVALID_HANDLE_VALUE);
    assert(!profiles_remove(first, id) && first->count == 1);
    CloseHandle(lock);
    assert(profiles_remove(first, id) && first->count == 0);
    char *file = g_build_filename(first->directory_path, "config.json", NULL);
    char *data = NULL;
    size_t length = 0;
    assert(papo_win_read_private(file, 1, &data, &length) == -1 && !data);
    assert(papo_win_read_private(file, 262144, &data, &length) == 1 && length > 0);
    free(data);
    profiles_destroy(first);
    profiles_destroy(stale);
    g_unlink(file);
    g_unlink(lockpath);
    char *directory = g_build_filename(root, "papo-tui", NULL);
    assert(g_rmdir(directory) == 0 && g_rmdir(root) == 0);
    g_free(directory);
    g_free(file);
    g_free(lockpath);
    g_free(root);
    curl_global_cleanup();
    return 0;
}
