#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Serialize HTTPS so weather + LRT do not open TLS at the same time (heap / handshake flakes). */
void https_guard_init(void);

/**
 * Optional RGB panel handle (API compat). PCLK is fixed at panel init — we no
 * longer drop PCLK for the whole HTTPS session (that hurt WiFi / TLS).
 */
void https_guard_set_rgb_panel(esp_lcd_panel_handle_t panel);

/**
 * Take the HTTPS mutex.
 * @param timeout_ms  Wait up to this many ms (portMAX_DELAY if < 0).
 * @return true if locked
 */
bool https_guard_lock(int timeout_ms);

void https_guard_unlock(void);

/** Log internal DRAM free / largest / PSRAM (-0x008D=alloc, -0x0087=bad input). */
void https_guard_log_heap(const char *who);

/**
 * After esp_http_client_cleanup(): brief delay so TLS buffers return to the heap
 * before the next waiter starts ssl_setup.
 */
void https_guard_post_tls_settle(void);

/**
 * True if internal DRAM largest free block is below the soft preference.
 * With EXTERNAL mbedtls most TLS is in PSRAM; soft warning != hard fail.
 */
bool https_guard_internal_heap_tight(void);

/**
 * Under the HTTPS lock: prefer soft internal room, else proceed when hard floor
 * is met and PSRAM is healthy (avoids endless wait when largest stuck ~7–8K).
 * @return true if TLS may start
 */
bool https_guard_wait_internal_room(int timeout_ms);

#ifdef __cplusplus
}
#endif
