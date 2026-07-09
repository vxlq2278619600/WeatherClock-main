#ifndef __WEATHER_CLOCK_UI_H__
#define __WEATHER_CLOCK_UI_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} clock_time_t;

typedef struct
{
    int temperature_tenths;
    int humidity_tenths;
    uint8_t valid;
} weather_clock_sensor_t;

typedef enum
{
    WEATHER_CLOCK_PAGE_MAIN = 0,
    WEATHER_CLOCK_PAGE_NETWORK,
    WEATHER_CLOCK_PAGE_SENSOR,
    WEATHER_CLOCK_PAGE_COUNT
} weather_clock_page_t;

#define UI_SCREEN_W         240
#define UI_SCREEN_H         320
#define UI_MARGIN_X         16
#define UI_MARGIN_TOP       16
#define UI_MARGIN_BOTTOM    12
#define UI_LINE_H           24
#define UI_TEMP_ROW_Y       (UI_SCREEN_H - UI_MARGIN_BOTTOM - 20)

void weather_clock_show_boot(void);
void weather_clock_show_wifi_status(bool ok);
void weather_clock_show_error(const char *message);
void weather_clock_show_main(const clock_time_t *time, const weather_clock_sensor_t *sensor);
void weather_clock_show_network(const char *ip, bool wifi_ok, bool sntp_ok, const clock_time_t *last_sync);
void weather_clock_show_sensor(const weather_clock_sensor_t *sensor);
void weather_clock_show_page(weather_clock_page_t page, const char *ip, const clock_time_t *time,
                             const clock_time_t *last_sync, const weather_clock_sensor_t *sensor,
                             bool wifi_ok, bool sntp_ok);
void weather_clock_update_time(weather_clock_page_t page, const clock_time_t *time);
void weather_clock_update_sensor(weather_clock_page_t page, const weather_clock_sensor_t *sensor);
void weather_clock_update_status(const char *status);

#endif /* __WEATHER_CLOCK_UI_H__ */
