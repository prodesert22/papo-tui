#include "platform/windows/files.h"
#include <aclapi.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <sddl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PSECURITY_DESCRIPTOR descriptor(void)
{
    HANDLE token = NULL;
    DWORD length = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return NULL;
    GetTokenInformation(token, TokenUser, NULL, 0, &length);
    TOKEN_USER *user = malloc(length);
    PSECURITY_DESCRIPTOR result = NULL;
    LPWSTR sid = NULL;
    if (user && GetTokenInformation(token, TokenUser, user, length, &length) &&
        ConvertSidToStringSidW(user->User.Sid, &sid)) {
        wchar_t dacl[256];
        if (swprintf(dacl, 256, L"D:P(A;;FA;;;SY)(A;;FA;;;%ls)", sid) > 0)
            ConvertStringSecurityDescriptorToSecurityDescriptorW(dacl, SDDL_REVISION_1, &result,
                                                                 NULL);
    }
    if (sid)
        LocalFree(sid);
    free(user);
    CloseHandle(token);
    return result;
}

static bool secure_handle(HANDLE file, bool directory)
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        (!!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory) ||
        (!directory && info.nNumberOfLinks != 1))
        return false;
    PSID owner = NULL;
    PSECURITY_DESCRIPTOR existing = NULL, desired = descriptor();
    HANDLE token = NULL;
    TOKEN_USER *user = NULL;
    DWORD size = 0;
    bool ok = desired &&
              GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, NULL, NULL,
                              NULL, &existing) == ERROR_SUCCESS &&
              OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    if (ok) {
        GetTokenInformation(token, TokenUser, NULL, 0, &size);
        user = malloc(size);
        ok = user && GetTokenInformation(token, TokenUser, user, size, &size) &&
             EqualSid(owner, user->User.Sid);
    }
    PACL acl = NULL;
    BOOL present = FALSE, defaulted = FALSE;
    if (ok)
        ok = GetSecurityDescriptorDacl(desired, &present, &acl, &defaulted) && present &&
             SetSecurityInfo(file, SE_FILE_OBJECT,
                             DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, NULL,
                             NULL, acl, NULL) == ERROR_SUCCESS;
    if (token)
        CloseHandle(token);
    if (existing)
        LocalFree(existing);
    if (desired)
        LocalFree(desired);
    free(user);
    return ok;
}

bool papo_win_secure_path(const char *path, bool directory)
{
    gunichar2 *wide = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    if (!wide)
        return false;
    HANDLE file = CreateFileW(
        (LPCWSTR)wide, READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0), NULL);
    g_free(wide);
    bool ok = file != INVALID_HANDLE_VALUE && secure_handle(file, directory);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    return ok;
}

char *papo_win_directory(bool state)
{
    const char *override = g_getenv(state ? "XDG_STATE_HOME" : "XDG_CONFIG_HOME");
    char *path = NULL;
    if (override && g_path_is_absolute(override))
        path = g_build_filename(override, "papo-tui", NULL);
    else {
        const char *base = g_getenv("LOCALAPPDATA");
        if (!base || !g_path_is_absolute(base))
            return NULL;
        path = state ? g_build_filename(base, "papo-tui", "state", NULL)
                     : g_build_filename(base, "papo-tui", NULL);
    }
    char *result = NULL;
    if (g_mkdir_with_parents(path, 0700) == 0 && papo_win_secure_path(path, true))
        result = strdup(path);
    g_free(path);
    return result;
}

int papo_win_read_private(const char *path, size_t limit, char **data, size_t *length)
{
    *data = NULL;
    *length = 0;
    gunichar2 *wide = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    if (!wide)
        return -1;
    HANDLE file = CreateFileW((LPCWSTR)wide, GENERIC_READ | READ_CONTROL | WRITE_DAC,
                              FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, NULL);
    DWORD error = GetLastError();
    g_free(wide);
    if (file == INVALID_HANDLE_VALUE)
        return error == ERROR_FILE_NOT_FOUND ? 0 : -1;
    LARGE_INTEGER size;
    bool ok = secure_handle(file, false) && GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
              (ULONGLONG)size.QuadPart <= limit && limit < MAXDWORD;
    char *buffer = ok ? malloc((size_t)size.QuadPart + 1) : NULL;
    DWORD read = 0;
    ok = buffer && ReadFile(file, buffer, (DWORD)size.QuadPart, &read, NULL) &&
         read == size.QuadPart;
    CloseHandle(file);
    if (!ok) {
        free(buffer);
        return -1;
    }
    buffer[read] = 0;
    *length = read;
    *data = buffer;
    return 1;
}

bool papo_win_write_private(const char *path, const void *data, size_t length)
{
    if (length > MAXDWORD)
        return false;
    char *uuid = g_uuid_string_random();
    char *temporary = g_strconcat(path, ".", uuid, ".tmp", NULL);
    g_free(uuid);
    gunichar2 *target = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    gunichar2 *wide = g_utf8_to_utf16(temporary, -1, NULL, NULL, NULL);
    PSECURITY_DESCRIPTOR sd = descriptor();
    SECURITY_ATTRIBUTES sa = {sizeof(sa), sd, FALSE};
    HANDLE file = wide && sd
                      ? CreateFileW((LPCWSTR)wide, GENERIC_WRITE, 0, &sa, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL)
                      : INVALID_HANDLE_VALUE;
    DWORD written = 0;
    bool ok = file != INVALID_HANDLE_VALUE &&
              WriteFile(file, data, (DWORD)length, &written, NULL) && written == length &&
              FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE && !CloseHandle(file))
        ok = false;
    if (ok)
        ok = target && MoveFileExW((LPCWSTR)wide, (LPCWSTR)target,
                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok && wide)
        DeleteFileW((LPCWSTR)wide);
    if (sd)
        LocalFree(sd);
    g_free(target);
    g_free(wide);
    g_free(temporary);
    return ok;
}

HANDLE papo_win_lock(const char *path)
{
    gunichar2 *wide = g_utf8_to_utf16(path, -1, NULL, NULL, NULL);
    PSECURITY_DESCRIPTOR sd = descriptor();
    SECURITY_ATTRIBUTES sa = {sizeof(sa), sd, FALSE};
    HANDLE file = wide && sd ? CreateFileW((LPCWSTR)wide,
                                           GENERIC_READ | GENERIC_WRITE | READ_CONTROL | WRITE_DAC,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS,
                                           FILE_FLAG_OPEN_REPARSE_POINT, NULL)
                             : INVALID_HANDLE_VALUE;
    if (sd)
        LocalFree(sd);
    g_free(wide);
    OVERLAPPED offset = {0};
    if (file != INVALID_HANDLE_VALUE &&
        (!secure_handle(file, false) ||
         !LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                     &offset))) {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
    return file;
}
