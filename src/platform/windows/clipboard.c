#include "platform/clipboard.h"
#include "net/clock.h"
#include <glib.h>
#include <windows.h>
static char pending[TEXT_UTF8_BYTES];
static uint64_t started, until, revision;
static const char *notice;
static bool busy;
static void feedback(const char *value)
{
    uint64_t now = 0;
    net_now_ms(&now);
    notice = value;
    until = now + 3000;
    ++revision;
}
static void wipe(void)
{
    SecureZeroMemory(pending, sizeof(pending));
    busy = false;
}
bool clipboard_copy(const text_buffer *buffer)
{
    size_t lo, hi;
    if (busy || !text_selection(buffer, &lo, &hi))
        return false;
    text_buffer selected = {0}, projected;
    selected.length = hi - lo;
    memcpy(selected.data, buffer->data + lo, selected.length * sizeof(selected.data[0]));
    selected.mentions = buffer->mentions;
    selected.resolver = buffer->resolver;
    selected.resolver_context = buffer->resolver_context;
    text_project(&selected, &projected);
    bool ok = text_encode(&projected, pending);
    SecureZeroMemory(&selected, sizeof(selected));
    SecureZeroMemory(&projected, sizeof(projected));
    if (!ok) {
        wipe();
        feedback("Falha ao copiar texto");
        return false;
    }
    net_now_ms(&started);
    busy = true;
    return true;
}
bool clipboard_poll(void)
{
    uint64_t now = 0;
    net_now_ms(&now);
    bool changed = false;
    if (busy && OpenClipboard(NULL)) {
        glong units = 0;
        gunichar2 *wide = g_utf8_to_utf16(pending, -1, NULL, &units, NULL);
        HGLOBAL memory =
            wide ? GlobalAlloc(GMEM_MOVEABLE, ((size_t)units + 1) * sizeof(wchar_t)) : NULL;
        void *copy = memory ? GlobalLock(memory) : NULL;
        if (copy) {
            memcpy(copy, wide, ((size_t)units + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
        }
        bool ok = copy && EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
        if (!ok && memory)
            GlobalFree(memory);
        CloseClipboard();
        if (wide)
            SecureZeroMemory(wide, ((size_t)units + 1) * sizeof(wchar_t));
        g_free(wide);
        wipe();
        feedback(ok ? "Texto copiado" : "Falha ao copiar texto");
        changed = true;
    } else if (busy && now - started >= 2000) {
        wipe();
        feedback("Clipboard indisponível");
        changed = true;
    }
    if (notice && now >= until) {
        notice = NULL;
        changed = true;
    }
    return changed;
}
const char *clipboard_notice(void)
{
    return notice;
}
uint64_t clipboard_revision(void)
{
    return revision;
}
void clipboard_shutdown(void)
{
    wipe();
    notice = NULL;
}
