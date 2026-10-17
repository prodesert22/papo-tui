#include "platform/runtime.h"
#include "platform/windows/files.h"
#include <bcrypt.h>
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

char *papo_executable_path(void)
{
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, 32768);
    if (!n || n >= 32768)
        return NULL;
    char *utf8 = g_utf16_to_utf8((const gunichar2 *)path, n, NULL, NULL, NULL);
    char *result = utf8 ? strdup(utf8) : NULL;
    g_free(utf8);
    return result;
}
bool papo_terminal_size(unsigned *rows, unsigned *columns, unsigned *pixels_x, unsigned *pixels_y)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    /* ncurses can activate its own buffer, leaving STD_OUTPUT_HANDLE attached
     * to the original one. CONOUT$ follows the currently displayed buffer. */
    HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                OPEN_EXISTING, 0, NULL);
    if (output == INVALID_HANDLE_VALUE)
        return false;
    BOOL ok = GetConsoleScreenBufferInfo(output, &info);
    CloseHandle(output);
    if (!ok)
        return false;
    *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    *columns = info.srWindow.Right - info.srWindow.Left + 1;
    /* Pixel size is negotiated with the terminal, not inferred from conhost. */
    *pixels_x = *pixels_y = 0;
    return *rows && *columns;
}
bool papo_localtime(const time_t *value, struct tm *result)
{
    return localtime_s(result, value) == 0;
}
char *papo_mkdtemp(char *pattern)
{
    char *result = g_mkdtemp(pattern);
    if (result && !papo_win_secure_path(result, true)) {
        g_rmdir(result);
        return NULL;
    }
    return result;
}
int papo_mkstemp(char *pattern)
{
    int fd = g_mkstemp_full(pattern, O_RDWR | O_BINARY, 0600);
    if (fd >= 0 && !papo_win_secure_path(pattern, false)) {
        _close(fd);
        g_unlink(pattern);
        return -1;
    }
    return fd;
}
bool papo_random_bytes(void *bytes, size_t length)
{
    return length <= ULONG_MAX &&
           BCryptGenRandom(NULL, bytes, (ULONG)length, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}
