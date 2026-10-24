#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default stop (藍地) — same as historical hard-coded timetable station. */
#define LRT_STATION_DEFAULT 350

typedef enum {
    LRT_DISTRICT_TUEN_MUN = 0,
    LRT_DISTRICT_YUEN_LONG,
    LRT_DISTRICT_TIN_SHUI_WAI,
} lrt_district_t;

typedef struct {
    int id;
    const char *name_zh;
    lrt_district_t district;
} lrt_station_info_t;

/** Full stop table (MTR LRT Next Train IDs), sorted by district then id. */
const lrt_station_info_t *lrt_stations_table(size_t *count);

/** Lookup by Stop ID; NULL if unknown. */
const lrt_station_info_t *lrt_station_find(int station_id);

/** Traditional Chinese name, or "藍地" if unknown. */
const char *lrt_station_name_zh(int station_id);

/** True if station_id is in the official table. */
bool lrt_station_id_valid(int station_id);

/** District section title for picker UI (Trad Chinese). */
const char *lrt_district_name_zh(lrt_district_t d);

#ifdef __cplusplus
}
#endif
