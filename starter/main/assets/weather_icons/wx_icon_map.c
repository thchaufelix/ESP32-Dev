#include "wx_icons.h"

#include <string.h>

wx_icon_style_t wx_icon_style_for_hko(int hko_icon)
{
    wx_icon_style_t s = { .img = &wx_icon_generic, .recolor = 0xe5e7eb };
    switch (hko_icon) {
    case 50:
    case 81:
        s.img = &wx_icon_sunny;
        s.recolor = 0xfbbf24;
        break;
    case 51:
    case 52:
        s.img = &wx_icon_partly_cloudy;
        s.recolor = 0x93c5fd;
        break;
    case 53:
    case 54:
    case 62:
    case 63:
        s.img = &wx_icon_rain;
        s.recolor = 0x3b82f6;
        break;
    case 60:
        s.img = &wx_icon_cloudy;
        s.recolor = 0x9ca3af;
        break;
    case 61:
        s.img = &wx_icon_overcast;
        s.recolor = 0x6b7280;
        break;
    case 64:
        s.img = &wx_icon_heavy_rain;
        s.recolor = 0x2563eb;
        break;
    case 65:
        s.img = &wx_icon_thunderstorm;
        s.recolor = 0xa78bfa;
        break;
    case 70:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75:
    case 77:
        s.img = &wx_icon_clear_night;
        s.recolor = 0xc4b5fd;
        break;
    case 76:
        s.img = &wx_icon_cloudy_night;
        s.recolor = 0x94a3b8;
        break;
    case 80:
        s.img = &wx_icon_windy;
        s.recolor = 0x67e8f9;
        break;
    case 82:
    case 83:
    case 84:
    case 85:
        s.img = &wx_icon_foggy;
        s.recolor = 0xd1d5db;
        break;
    case 90:
    case 91:
        s.img = &wx_icon_hot;
        s.recolor = 0xf87171;
        break;
    case 92:
    case 93:
        s.img = &wx_icon_cold;
        s.recolor = 0x7dd3fc;
        break;
    default:
        break;
    }
    return s;
}

wx_icon_style_t wx_icon_style_for_ha(const char *state, bool is_night)
{
    wx_icon_style_t s = { .img = &wx_icon_generic, .recolor = 0xe5e7eb };
    if (state == NULL || state[0] == '\0') {
        return s;
    }
    if (strcmp(state, "sunny") == 0) {
        s.img = &wx_icon_sunny;
        s.recolor = 0xfbbf24;
    } else if (strcmp(state, "clear-night") == 0) {
        s.img = &wx_icon_clear_night;
        s.recolor = 0xc4b5fd;
    } else if (strcmp(state, "partlycloudy") == 0) {
        if (is_night) {
            /* Same night cloud as cloudy (cloudy_night), not partly_cloudy_night. */
            s.img = &wx_icon_cloudy_night;
            s.recolor = 0x94a3b8;
        } else {
            s.img = &wx_icon_partly_cloudy;
            s.recolor = 0x93c5fd;
        }
    } else if (strcmp(state, "cloudy") == 0) {
        if (is_night) {
            s.img = &wx_icon_cloudy_night;
            s.recolor = 0x94a3b8;
        } else {
            s.img = &wx_icon_cloudy;
            s.recolor = 0x9ca3af;
        }
    } else if (strcmp(state, "rainy") == 0) {
        s.img = &wx_icon_rain;
        s.recolor = 0x3b82f6;
    } else if (strcmp(state, "pouring") == 0) {
        s.img = &wx_icon_heavy_rain;
        s.recolor = 0x2563eb;
    } else if (strcmp(state, "lightning") == 0 || strcmp(state, "lightning-rainy") == 0) {
        s.img = &wx_icon_thunderstorm;
        s.recolor = 0xa78bfa;
    } else if (strcmp(state, "fog") == 0) {
        s.img = &wx_icon_foggy;
        s.recolor = 0xd1d5db;
    } else if (strcmp(state, "hail") == 0) {
        s.img = &wx_icon_hail;
        s.recolor = 0xbae6fd;
    } else if (strcmp(state, "windy") == 0 || strcmp(state, "windy-variant") == 0) {
        s.img = &wx_icon_windy;
        s.recolor = 0x67e8f9;
    } else if (strcmp(state, "snowy") == 0) {
        s.img = &wx_icon_snow;
        s.recolor = 0xe0f2fe;
    } else if (strcmp(state, "snowy-rainy") == 0) {
        s.img = &wx_icon_snowy_rainy;
        s.recolor = 0x7dd3fc;
    } else if (strcmp(state, "exceptional") == 0) {
        s.img = &wx_icon_exceptional;
        s.recolor = 0xfbbf24;
    }
    return s;
}
