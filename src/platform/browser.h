#ifndef PAPO_BROWSER_H
#define PAPO_BROWSER_H
#include <stdbool.h>
/* Hands an http:// or https:// address to the system, which opens it in the
 * user's default browser. Explicit user action only: the address is remote
 * text, so any other scheme is refused and it reaches the system as a single
 * argument, never through a shell. False when nothing could be started; a
 * browser that fails later is not reported. */
bool browser_open(const char *url);
#endif
