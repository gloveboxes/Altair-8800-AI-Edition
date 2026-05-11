#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Synchronize wall-clock time for TLS certificate validation.
 * Returns true once the system time is valid, whether it was already valid
 * before this call or became valid through SNTP. */
bool network_time_sync(void);

#ifdef __cplusplus
}
#endif
