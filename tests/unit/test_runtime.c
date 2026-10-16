#define _POSIX_C_SOURCE 200809L
#include "platform/runtime.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "Check failed: %s:%d\n", __FILE__, __LINE__);                          \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
int main(void)
{
    unsigned char a[32], b[32];
    CHECK(papo_random_bytes(a, sizeof(a)) && papo_random_bytes(b, sizeof(b)));
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    char *path = papo_executable_path();
    CHECK(path && g_path_is_absolute(path) && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE));
    free(path);
    struct tm local;
    time_t now = time(NULL);
    CHECK(papo_localtime(&now, &local));
    CHECK(local.tm_mon >= 0 && local.tm_mon < 12 && local.tm_mday > 0 && local.tm_mday <= 31);
    char *temporary = g_build_filename(g_get_tmp_dir(), "papo-runtime-XXXXXX", NULL);
    CHECK(papo_mkdtemp(temporary));
    path = g_build_filename(temporary, "file-XXXXXX", NULL);
    int fd = papo_mkstemp(path);
    CHECK(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    CHECK(file);
    CHECK(fwrite("fixture", 1, 7, file) == 7 && fclose(file) == 0);
    CHECK(g_file_test(path, G_FILE_TEST_IS_REGULAR));
    CHECK(remove(path) == 0);
    g_free(path);
    CHECK(g_rmdir(temporary) == 0);
    g_free(temporary);
    return 0;
}
