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

/** Log internal DRAM free / largest block (mbedtls -0x008D is usually internal OOM). */
void https_guard_log_heap(const char *who);

/**
 * After esp_http_client_cleanup(): brief delay so TLS buffers return to the heap
 * before the next waiter starts ssl_setup.
 */
void https_guard_post_tls_settle(void);

/**
 * True if internal DRAM largest free block looks too small for a TLS session.
 * Prefer https_guard_wait_internal_room() under the lock.
 */
bool https_guard_internal_heap_tight(void);

/**
 * Under the HTTPS lock: poll until largest internal free block is big enough
 * for TLS, or timeout_ms elapses. Yields so LVGL/WiFi can free / coalesce.
 * @return true if room is available
 */
bool https_guard_wait_internal_room(int timeout_ms);

#ifdef __cplusplus
}
#endif
