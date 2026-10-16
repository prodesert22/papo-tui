#ifndef PAPO_RUNTIME_H
#define PAPO_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Returned paths use malloc ownership. No search of PATH or cwd. */
char *papo_executable_path(void);
bool papo_terminal_size(unsigned *rows, unsigned *columns, unsigned *pixels_x, unsigned *pixels_y);
bool papo_localtime(const time_t *value, struct tm *result);
char *papo_mkdtemp(char *pattern);
int papo_mkstemp(char *pattern);
bool papo_random_bytes(void *bytes, size_t length);

#endif
