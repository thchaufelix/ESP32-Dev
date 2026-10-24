#include "lrt_stations.h"

static const lrt_station_info_t s_stations[] = {
    {1, "屯門碼頭", LRT_DISTRICT_TUEN_MUN},
    {10, "美樂", LRT_DISTRICT_TUEN_MUN},
    {15, "蝴蝶", LRT_DISTRICT_TUEN_MUN},
    {20, "輕鐵車廠", LRT_DISTRICT_TUEN_MUN},
    {30, "龍門", LRT_DISTRICT_TUEN_MUN},
    {40, "青山村", LRT_DISTRICT_TUEN_MUN},
    {50, "青雲", LRT_DISTRICT_TUEN_MUN},
    {60, "建安", LRT_DISTRICT_TUEN_MUN},
    {70, "河田", LRT_DISTRICT_TUEN_MUN},
    {75, "蔡意橋", LRT_DISTRICT_TUEN_MUN},
    {80, "澤豐", LRT_DISTRICT_TUEN_MUN},
    {90, "屯門醫院", LRT_DISTRICT_TUEN_MUN},
    {100, "兆康", LRT_DISTRICT_TUEN_MUN},
    {110, "麒麟", LRT_DISTRICT_TUEN_MUN},
    {120, "青松", LRT_DISTRICT_TUEN_MUN},
    {130, "建生", LRT_DISTRICT_TUEN_MUN},
    {140, "田景", LRT_DISTRICT_TUEN_MUN},
    {150, "良景", LRT_DISTRICT_TUEN_MUN},
    {160, "新圍", LRT_DISTRICT_TUEN_MUN},
    {170, "石排", LRT_DISTRICT_TUEN_MUN},
    {180, "山景(北)", LRT_DISTRICT_TUEN_MUN},
    {190, "山景(南)", LRT_DISTRICT_TUEN_MUN},
    {200, "鳴琴", LRT_DISTRICT_TUEN_MUN},
    {212, "大興(北)", LRT_DISTRICT_TUEN_MUN},
    {220, "大興(南)", LRT_DISTRICT_TUEN_MUN},
    {230, "銀圍", LRT_DISTRICT_TUEN_MUN},
    {240, "兆禧", LRT_DISTRICT_TUEN_MUN},
    {250, "海皇路", LRT_DISTRICT_TUEN_MUN},
    {260, "豐景園", LRT_DISTRICT_TUEN_MUN},
    {265, "兆麟", LRT_DISTRICT_TUEN_MUN},
    {270, "安定", LRT_DISTRICT_TUEN_MUN},
    {275, "友愛", LRT_DISTRICT_TUEN_MUN},
    {280, "市中心", LRT_DISTRICT_TUEN_MUN},
    {295, "屯門", LRT_DISTRICT_TUEN_MUN},
    {300, "杯渡", LRT_DISTRICT_TUEN_MUN},
    {310, "何福堂", LRT_DISTRICT_TUEN_MUN},
    {320, "新墟", LRT_DISTRICT_TUEN_MUN},
    {330, "景峰", LRT_DISTRICT_TUEN_MUN},
    {340, "鳳地", LRT_DISTRICT_TUEN_MUN},
    {350, "藍地", LRT_DISTRICT_TUEN_MUN},
    {920, "三聖", LRT_DISTRICT_TUEN_MUN},
    {360, "泥圍", LRT_DISTRICT_YUEN_LONG},
    {370, "鍾屋村", LRT_DISTRICT_YUEN_LONG},
    {380, "洪水橋", LRT_DISTRICT_YUEN_LONG},
    {390, "塘坊村", LRT_DISTRICT_YUEN_LONG},
    {400, "屏山", LRT_DISTRICT_YUEN_LONG},
    {425, "坑尾村", LRT_DISTRICT_YUEN_LONG},
    {560, "水邊圍", LRT_DISTRICT_YUEN_LONG},
    {570, "豐年路", LRT_DISTRICT_YUEN_LONG},
    {580, "康樂路", LRT_DISTRICT_YUEN_LONG},
    {590, "大棠路", LRT_DISTRICT_YUEN_LONG},
    {600, "元朗", LRT_DISTRICT_YUEN_LONG},
    {430, "天水圍", LRT_DISTRICT_TIN_SHUI_WAI},
    {435, "天慈", LRT_DISTRICT_TIN_SHUI_WAI},
    {445, "天耀", LRT_DISTRICT_TIN_SHUI_WAI},
    {448, "樂湖", LRT_DISTRICT_TIN_SHUI_WAI},
    {450, "天湖", LRT_DISTRICT_TIN_SHUI_WAI},
    {455, "銀座", LRT_DISTRICT_TIN_SHUI_WAI},
    {460, "天瑞", LRT_DISTRICT_TIN_SHUI_WAI},
    {468, "頌富", LRT_DISTRICT_TIN_SHUI_WAI},
    {480, "天富", LRT_DISTRICT_TIN_SHUI_WAI},
    {490, "翠湖", LRT_DISTRICT_TIN_SHUI_WAI},
    {500, "天榮", LRT_DISTRICT_TIN_SHUI_WAI},
    {510, "天悅", LRT_DISTRICT_TIN_SHUI_WAI},
    {520, "天秀", LRT_DISTRICT_TIN_SHUI_WAI},
    {530, "濕地公園", LRT_DISTRICT_TIN_SHUI_WAI},
    {540, "天恒", LRT_DISTRICT_TIN_SHUI_WAI},
    {550, "天逸", LRT_DISTRICT_TIN_SHUI_WAI},
};

const lrt_station_info_t *lrt_stations_table(size_t *count)
{
    if (count) {
        *count = sizeof(s_stations) / sizeof(s_stations[0]);
    }
    return s_stations;
}

const lrt_station_info_t *lrt_station_find(int station_id)
{
    size_t n = sizeof(s_stations) / sizeof(s_stations[0]);
    for (size_t i = 0; i < n; i++) {
        if (s_stations[i].id == station_id) {
            return &s_stations[i];
        }
    }
    return NULL;
}

const char *lrt_station_name_zh(int station_id)
{
    const lrt_station_info_t *info = lrt_station_find(station_id);
    if (info && info->name_zh) {
        return info->name_zh;
    }
    return "藍地";
}

bool lrt_station_id_valid(int station_id)
{
    return lrt_station_find(station_id) != NULL;
}

const char *lrt_district_name_zh(lrt_district_t d)
{
    switch (d) {
    case LRT_DISTRICT_TUEN_MUN:
        return "屯門";
    case LRT_DISTRICT_YUEN_LONG:
        return "元朗";
    case LRT_DISTRICT_TIN_SHUI_WAI:
        return "天水圍";
    default:
        return "";
    }
}
