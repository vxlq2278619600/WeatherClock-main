#ifndef __WEATHER_API_H__
#define __WEATHER_API_H__

#include <stdint.h>

typedef struct
{
    char city[24];
    char weather[24];
    char temperature[12];
    char humidity[12];
    char winddirection[16];
    char windpower[16];
    char reporttime[24];
    uint8_t valid;
} weather_api_info_t;

int weather_api_init(void);
int weather_api_fetch(weather_api_info_t *out);
int weather_parse_amap_json(const char *json, weather_api_info_t *out);

#endif /* __WEATHER_API_H__ */
