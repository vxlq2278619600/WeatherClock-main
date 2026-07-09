#ifndef __WEATHER_CLOCK_UI_H__
#define __WEATHER_CLOCK_UI_H__

#include <stdbool.h>

typedef struct
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} clock_time_t;

void weather_clock_show_boot(void);
void weather_clock_show_wifi_status(bool ok);
void weather_clock_show_error(const char *message);
void weather_clock_show_main(const char *ip, const clock_time_t *time);
void weather_clock_update_time(const clock_time_t *time);
void weather_clock_update_status(const char *status);

#endif /* __WEATHER_CLOCK_UI_H__ */
