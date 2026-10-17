#define COBJMACROS
#include <windows.h>

#include "platform/file_transfer.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>

typedef struct {
    IFileOpenDialog *dialog;
    GCancellable *cancel;
} picker_timer;
static _Thread_local picker_timer *active_picker;
static void CALLBACK cancel_picker(HWND window, UINT message, UINT_PTR timer, DWORD now)
{
    (void)window;
    (void)message;
    (void)timer;
    (void)now;
    if (active_picker && g_cancellable_is_cancelled(active_picker->cancel))
        IFileOpenDialog_Close(active_picker->dialog, HRESULT_FROM_WIN32(ERROR_CANCELLED));
}
GPtrArray *platform_pick_files(GCancellable *cancel, GError **error)
{
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog *dialog = NULL;
    HRESULT hr = SUCCEEDED(init)
                     ? CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                        &IID_IFileOpenDialog, (void **)&dialog)
                     : init;
    GPtrArray *paths = NULL;
    if (SUCCEEDED(hr)) {
        FILEOPENDIALOGOPTIONS options;
        hr = IFileOpenDialog_GetOptions(dialog, &options);
        if (SUCCEEDED(hr))
            hr = IFileOpenDialog_SetOptions(dialog, options | FOS_ALLOWMULTISELECT |
                                                        FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                                        FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
        IFileOpenDialog_SetTitle(dialog, L"Anexar arquivos");
        picker_timer state = {dialog, cancel};
        active_picker = &state;
        UINT_PTR timer = SetTimer(NULL, 0, 50, cancel_picker);
        if (SUCCEEDED(hr))
            hr = timer ? IFileOpenDialog_Show(dialog, NULL) : E_FAIL;
        if (timer)
            KillTimer(NULL, timer);
        active_picker = NULL;
        if (SUCCEEDED(hr)) {
            IShellItemArray *items = NULL;
            hr = IFileOpenDialog_GetResults(dialog, &items);
            DWORD count = 0;
            if (SUCCEEDED(hr))
                hr = IShellItemArray_GetCount(items, &count);
            if (SUCCEEDED(hr)) {
                paths = g_ptr_array_new_with_free_func(g_free);
                for (DWORD i = 0; i < count; ++i) {
                    IShellItem *item = NULL;
                    PWSTR wide = NULL;
                    if (SUCCEEDED(IShellItemArray_GetItemAt(items, i, &item))) {
                        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &wide))) {
                            char *path = g_utf16_to_utf8((gunichar2 *)wide, -1, NULL, NULL, NULL);
                            if (path)
                                g_ptr_array_add(paths, path);
                            CoTaskMemFree(wide);
                        }
                        IShellItem_Release(item);
                    }
                }
            }
            if (items)
                IShellItemArray_Release(items);
        }
        IFileOpenDialog_Release(dialog);
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    if (!paths)
        g_set_error_literal(error, G_IO_ERROR,
                            hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) ||
                                    g_cancellable_is_cancelled(cancel)
                                ? G_IO_ERROR_CANCELLED
                                : G_IO_ERROR_NOT_SUPPORTED,
                            "Seletor nativo indisponível ou fechado.");
    return paths;
}

static GBytes *bitmap_png(HBITMAP bitmap)
{
    HRESULT init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    IWICImagingFactory *factory = NULL;
    IWICBitmap *source = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IStream *stream = NULL;
    GBytes *bytes = NULL;
    HRESULT hr = SUCCEEDED(init)
                     ? CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                        &IID_IWICImagingFactory, (void **)&factory)
                     : init;
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateBitmapFromHBITMAP(factory, bitmap, NULL, WICBitmapIgnoreAlpha,
                                                        &source);
    if (SUCCEEDED(hr))
        hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder);
    if (SUCCEEDED(hr))
        hr = IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr))
        hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_Initialize(frame, NULL);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_WriteSource(frame, (IWICBitmapSource *)source, NULL);
    if (SUCCEEDED(hr))
        hr = IWICBitmapFrameEncode_Commit(frame);
    if (SUCCEEDED(hr))
        hr = IWICBitmapEncoder_Commit(encoder);
    STATSTG stat;
    HGLOBAL memory;
    if (SUCCEEDED(hr) && SUCCEEDED(IStream_Stat(stream, &stat, STATFLAG_NONAME)) &&
        stat.cbSize.QuadPart <= ATTACHMENT_CLIPBOARD_LIMIT &&
        SUCCEEDED(GetHGlobalFromStream(stream, &memory))) {
        void *data = GlobalLock(memory);
        if (data) {
            bytes = g_bytes_new(data, (gsize)stat.cbSize.QuadPart);
            GlobalUnlock(memory);
        }
    }
    if (frame)
        IWICBitmapFrameEncode_Release(frame);
    if (encoder)
        IWICBitmapEncoder_Release(encoder);
    if (source)
        IWICBitmap_Release(source);
    if (stream)
        IStream_Release(stream);
    if (factory)
        IWICImagingFactory_Release(factory);
    if (SUCCEEDED(init))
        CoUninitialize();
    return bytes;
}
clipboard_content *platform_read_clipboard(GCancellable *cancel, GError **error)
{
    bool opened = false;
    for (int i = 0; i < 100 && !g_cancellable_is_cancelled(cancel); ++i) {
        if (OpenClipboard(NULL)) {
            opened = true;
            break;
        }
        g_usleep(10000);
    }
    if (!opened) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "Clipboard indisponível.");
        return NULL;
    }
    clipboard_content *content = g_new0(clipboard_content, 1);
    UINT png = RegisterClipboardFormatW(L"PNG");
    HBITMAP bitmap = NULL;
    if (IsClipboardFormatAvailable(CF_HDROP)) {
        HDROP drop = GetClipboardData(CF_HDROP);
        UINT count = drop ? DragQueryFileW(drop, 0xffffffff, NULL, 0) : 0;
        content->paths = g_ptr_array_new_with_free_func(g_free);
        for (UINT i = 0; i < count && i < ATTACHMENT_LIMIT + 1; ++i) {
            UINT length = DragQueryFileW(drop, i, NULL, 0);
            if (length > 32767)
                continue;
            wchar_t *wide = g_new(wchar_t, (gsize)length + 1);
            if (DragQueryFileW(drop, i, wide, length + 1)) {
                char *path = g_utf16_to_utf8((gunichar2 *)wide, -1, NULL, NULL, NULL);
                if (path)
                    g_ptr_array_add(content->paths, path);
            }
            g_free(wide);
        }
    } else if (IsClipboardFormatAvailable(png)) {
        HGLOBAL memory = GetClipboardData(png);
        SIZE_T size = memory ? GlobalSize(memory) : 0;
        void *data = size && size <= ATTACHMENT_CLIPBOARD_LIMIT ? GlobalLock(memory) : NULL;
        if (data) {
            content->image = g_bytes_new(data, size);
            GlobalUnlock(memory);
        }
    } else if (IsClipboardFormatAvailable(CF_BITMAP) || IsClipboardFormatAvailable(CF_DIB) ||
               IsClipboardFormatAvailable(CF_DIBV5)) {
        HBITMAP original = GetClipboardData(CF_BITMAP);
        BITMAP info;
        if (original && GetObjectW(original, sizeof(info), &info) && info.bmHeight > 0 &&
            info.bmWidth > 0 &&
            (guint64)info.bmHeight * (guint64)info.bmWidth <= ATTACHMENT_CLIPBOARD_LIMIT / 4)
            bitmap = CopyImage(original, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    } else if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HGLOBAL memory = GetClipboardData(CF_UNICODETEXT);
        SIZE_T size = memory ? GlobalSize(memory) : 0;
        wchar_t *wide = size && size <= 1024 * 1024 ? GlobalLock(memory) : NULL;
        if (wide) {
            size_t units = size / sizeof(wchar_t), length = 0;
            while (length < units && wide[length])
                ++length;
            if (length < units)
                content->text = g_utf16_to_utf8((gunichar2 *)wide, (glong)length, NULL, NULL, NULL);
            GlobalUnlock(memory);
        }
    }
    CloseClipboard();
    if (bitmap) {
        content->image = bitmap_png(bitmap);
        DeleteObject(bitmap);
    }
    if (content->image)
        content->image_name = g_strdup("clipboard.png");
    if (!content->paths && !content->image && !content->text) {
        clipboard_content_free(content);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            "Clipboard sem arquivos, imagem ou texto compatível.");
        return NULL;
    }
    return content;
}
