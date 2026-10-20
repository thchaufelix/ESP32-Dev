#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LRT_STATION_LAM_TEI   350
#define LRT_MAX_ENTRIES       24
#define LRT_DEST_MAX          40
#define LRT_TIME_MAX          24
#define LRT_ROUTE_MAX         8
#define LRT_SYS_TIME_MAX      32

typedef struct {
    int platform_id;
    int train_length; /* cars */
    char route_no[LRT_ROUTE_MAX];
    char dest_ch[LRT_DEST_MAX];
    char dest_en[LRT_DEST_MAX];
    char time_ch[LRT_TIME_MAX];
    char time_en[LRT_TIME_MAX];
} lrt_entry_t;

typedef struct {
    lrt_entry_t entries[LRT_MAX_ENTRIES];
    size_t count;
    char system_time[LRT_SYS_TIME_MAX];
    int api_status;
} lrt_schedule_t;

/**
 * HTTPS fetch + parse LRT schedule for station_id.
 * Blocking; call from a worker task, not the LVGL task.
 * out must be non-NULL; cleared on entry.
 */
esp_err_t lrt_schedule_fetch(int station_id, lrt_schedule_t *out);

#ifdef __cplusplus
}
#endif
