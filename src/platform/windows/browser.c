#include "platform/browser.h"
#include <glib.h>
#include <stdint.h>
#include <windows.h>

#include <shellapi.h>

bool browser_open(const char *url)
{
    if (!url || (g_ascii_strncasecmp(url, "https://", 8) && g_ascii_strncasecmp(url, "http://", 7)))
        return false;
    /* Owned UTF-16 copy; NULL when the address is not valid UTF-8. */
    gunichar2 *wide = g_utf8_to_utf16(url, -1, NULL, NULL, NULL);
    if (!wide)
        return false;
    /* The shell opens the address with the program registered for its scheme,
     * the default browser. It reports success with a value above 32. */
    bool opened = (intptr_t)ShellExecuteW(NULL, L"open", (const wchar_t *)wide, NULL, NULL,
                                          SW_SHOWNORMAL) > 32;
    g_free(wide);
    return opened;
}
