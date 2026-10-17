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

static void check_pending_restore(profile_config *config)
{
    char *scope = g_compute_checksum_for_string(G_CHECKSUM_SHA256, config->directory_path, -1);
    char *name = g_strdup_printf("session-%s.json", scope);
    char *path = g_build_filename(config->directory_path, name, NULL);
    char *lock = g_strdup_printf("%s/session-%s.lock", config->directory_path, scope);
    char *target = g_strdup_printf("PapoTUI/%s", scope);
    gunichar2 *wide = g_utf8_to_utf16(target, -1, NULL, NULL, NULL);
    char snapshot[] = "synthetic-restore-fixture";
    CREDENTIALW credential = {.Type = CRED_TYPE_GENERIC,
                              .TargetName = (LPWSTR)wide,
                              .CredentialBlobSize = sizeof(snapshot) - 1,
                              .CredentialBlob = (LPBYTE)snapshot,
                              .Persist = CRED_PERSIST_LOCAL_MACHINE};
    assert(wide && CredWriteW(&credential, 0));
    const char marker[] = "{\"version\":1,\"url\":\"https://one.example.test\","
                          "\"restore_allowed\":true,\"provider\":4,\"revision\":1}";
    assert(papo_win_write_private(path, marker, sizeof(marker) - 1));
    session_store *store = session_store_create(config, true);
    assert(store && session_store_restoring(store));
    /* The lookup callback has not been dispatched yet. An empty take must
     * preserve the pending operation, regardless of worker completion timing. */
    assert(!session_store_take_resume(store));
    assert(session_store_restoring(store));
    gint64 deadline = g_get_monotonic_time() + 5000000;
    char *resume = NULL;
    while (!resume && g_get_monotonic_time() < deadline) {
        session_store_poll(store, NULL);
        resume = session_store_take_resume(store);
        if (!resume)
            g_usleep(1000);
    }
    assert(resume && !strcmp(resume, snapshot));
    assert(!session_store_restoring(store) && !session_store_take_resume(store));
    auth_session_free(&resume);
    session_store_destroy(store);

    store = session_store_create(config, true);
    assert(store && session_store_restoring(store));
    session_store_cancel_resume(store);
    assert(!session_store_restoring(store) && !session_store_take_resume(store));
    session_store_destroy(store);
    assert(CredDeleteW((LPCWSTR)wide, CRED_TYPE_GENERIC, 0));
    assert(g_unlink(path) == 0 && g_unlink(lock) == 0);
    g_free(wide);
    g_free(target);
    g_free(lock);
    g_free(path);
    g_free(name);
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
