#ifndef PAPO_WINDOWS_FILES_H
#define PAPO_WINDOWS_FILES_H
#include <stdbool.h>
#include <stddef.h>
#include <windows.h>
/* UTF-8 paths. Returned paths/data have malloc ownership. */
char *papo_win_directory(bool state);
bool papo_win_secure_path(const char *path, bool directory);
int papo_win_read_private(const char *path, size_t limit, char **data, size_t *length);
bool papo_win_write_private(const char *path, const void *data, size_t length);
HANDLE papo_win_lock(const char *path);
#endif
