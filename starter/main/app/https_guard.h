#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Serialize HTTPS so weather + LRT do not open TLS at the same time (heap / handshake flakes). */
void https_guard_init(void);

/**
 * Take the HTTPS mutex.
 * @param timeout_ms  Wait up to this many ms (portMAX_DELAY if < 0).
 * @return true if locked
 */
bool https_guard_lock(int timeout_ms);

void https_guard_unlock(void);

#ifdef __cplusplus
}
#endif
