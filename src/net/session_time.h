#ifndef PAPO_SESSION_TIME_H
#define PAPO_SESSION_TIME_H

#include <stdbool.h>
#include <stdint.h>

/* Calendar parsing/conversion belongs to libc; output is UTC epoch millis. */
bool session_time_parse(const char *value, uint64_t *epoch_ms);
/* Refuse expired/insufficient lifetimes and overflowing monotonic deadlines. */
bool session_time_deadline(uint64_t expires_ms, uint64_t wall_ms, uint64_t monotonic_ms,
                           uint64_t *deadline_ms);
bool session_time_schedule(const char *expires_at, uint64_t *deadline_ms);
bool session_time_expiration(const char *expires_at, uint64_t *deadline_ms);

#endif
