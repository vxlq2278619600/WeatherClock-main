#include <stdio.h>
#include <stdint.h>
#include "ui.h"
#include "weather_clock_ui.h"

#define WC_BG_COLOR       0x0000
#define WC_FG_COLOR       0xFFFF
#define WC_OK_COLOR       mkcolor(120, 255, 120)
#define WC_WARN_COLOR     mkcolor(255, 255, 0)
#define WC_ERROR_COLOR    mkcolor(255, 80, 80)
#define WC_TIME_COLOR     mkcolor(0, 255, 234)

static void weather_clock_write_row(uint16_t y, uint16_t height, const char *text,
                                    uint16_t color, const font_t *font)
{
    uint16_t y2 = y + height - 1;
    if (y2 >= UI_SCREEN_H)
        y2 = UI_SCREEN_H - 1;

    ui_fill_color(0, y, UI_SCREEN_W - 1, y2, WC_BG_COLOR);
    ui_write_string(UI_MARGIN_X, y, text, color, WC_BG_COLOR, font);
}

static void weather_clock_format_time(char *buf, size_t buf_size, const clock_time_t *time)
{
    snprintf(buf, buf_size, "%02d:%02d:%02d", time->hour, time->minute, time->second);
}

static void weather_clock_format_date(char *buf, size_t buf_size, const clock_time_t *time)
{
    snprintf(buf, buf_size, "%04d-%02d-%02d", time->year, time->month, time->day);
}

static void weather_clock_format_tenths(char *buf, size_t buf_size, int value)
{
    int abs_value = value < 0 ? -value : value;
    snprintf(buf, buf_size, "%s%d.%d", value < 0 ? "-" : "", abs_value / 10, abs_value % 10);
}

static void weather_clock_format_sensor_line(char *buf, size_t buf_size, const weather_clock_sensor_t *sensor)
{
    char temperature[12];
    char humidity[12];

    if (sensor == NULL || sensor->valid == 0)
    {
        snprintf(buf, buf_size, "T:--.-C H:--.-%%");
        return;
    }

    weather_clock_format_tenths(temperature, sizeof(temperature), sensor->temperature_tenths);
    weather_clock_format_tenths(humidity, sizeof(humidity), sensor->humidity_tenths);
    snprintf(buf, buf_size, "T:%sC H:%s%%", temperature, humidity);
}

void weather_clock_show_boot(void)
{
    ui_fill_color(0, 0, UI_SCREEN_W - 1, UI_SCREEN_H - 1, WC_BG_COLOR);
    ui_write_string(UI_MARGIN_X, UI_MARGIN_TOP, "Weather Clock", WC_FG_COLOR, WC_BG_COLOR, &font24_maple_bold);
    weather_clock_write_row(58, UI_LINE_H, "WiFi: ...", WC_WARN_COLOR, &font20_maple_bold);
    weather_clock_write_row(88, UI_LINE_H, "SNTP: ...", WC_WARN_COLOR, &font20_maple_bold);
    weather_clock_write_row(132, 36, "--:--:--", WC_TIME_COLOR, &font32_maple_bold);
    weather_clock_write_row(180, 28, "----/--/--", WC_FG_COLOR, &font24_maple_bold);
    weather_clock_write_row(UI_TEMP_ROW_Y, 20, "T:--.-C H:--.-%", WC_OK_COLOR, &font16_maple);
}

void weather_clock_show_wifi_status(bool ok)
{
    weather_clock_write_row(58, UI_LINE_H, ok ? "WiFi: OK" : "WiFi FAIL",
                            ok ? WC_OK_COLOR : WC_ERROR_COLOR, &font20_maple_bold);
}

void weather_clock_show_error(const char *message)
{
    weather_clock_write_row(176, 42, message, WC_ERROR_COLOR, &font32_maple_bold);
}

void weather_clock_show_main(const clock_time_t *time, const weather_clock_sensor_t *sensor)
{
    ui_fill_color(0, 0, UI_SCREEN_W - 1, UI_SCREEN_H - 1, WC_BG_COLOR);
    ui_write_string(UI_MARGIN_X, UI_MARGIN_TOP, "Weather Clock", WC_FG_COLOR, WC_BG_COLOR, &font24_maple_bold);
    weather_clock_write_row(58, UI_LINE_H, "WiFi: OK", WC_OK_COLOR, &font20_maple_bold);
    weather_clock_update_time(WEATHER_CLOCK_PAGE_MAIN, time);
    weather_clock_update_sensor(WEATHER_CLOCK_PAGE_MAIN, sensor);
}

void weather_clock_show_network(const char *ip, bool wifi_ok, bool sntp_ok, const clock_time_t *last_sync)
{
    char line[48];

    ui_fill_color(0, 0, UI_SCREEN_W - 1, UI_SCREEN_H - 1, WC_BG_COLOR);
    ui_write_string(UI_MARGIN_X, UI_MARGIN_TOP, "WiFi Status", WC_FG_COLOR, WC_BG_COLOR, &font24_maple_bold);
    weather_clock_write_row(64, UI_LINE_H, wifi_ok ? "WiFi: OK" : "WiFi: FAIL",
                            wifi_ok ? WC_OK_COLOR : WC_ERROR_COLOR, &font20_maple_bold);
    snprintf(line, sizeof(line), "IP: %s", ip != NULL ? ip : "--");
    weather_clock_write_row(96, UI_LINE_H, line, WC_FG_COLOR, &font20_maple_bold);
    weather_clock_write_row(128, UI_LINE_H, sntp_ok ? "SNTP: OK" : "SNTP: FAIL",
                            sntp_ok ? WC_OK_COLOR : WC_ERROR_COLOR, &font20_maple_bold);
    if (last_sync != NULL && last_sync->year >= 2000)
    {
        char time_text[16];
        weather_clock_format_time(time_text, sizeof(time_text), last_sync);
        snprintf(line, sizeof(line), "Last Sync: %s", time_text);
    }
    else
    {
        snprintf(line, sizeof(line), "Last Sync: --:--:--");
    }
    weather_clock_write_row(164, UI_LINE_H, line, WC_WARN_COLOR, &font20_maple_bold);
}

void weather_clock_show_sensor(const weather_clock_sensor_t *sensor)
{
    char line[32];
    char value[12];

    ui_fill_color(0, 0, UI_SCREEN_W - 1, UI_SCREEN_H - 1, WC_BG_COLOR);
    ui_write_string(UI_MARGIN_X, UI_MARGIN_TOP, "Sensor", WC_FG_COLOR, WC_BG_COLOR, &font24_maple_bold);
    weather_clock_write_row(72, UI_LINE_H,
                            (sensor != NULL && sensor->valid != 0) ? "AHT20: OK" : "AHT20: FAIL",
                            (sensor != NULL && sensor->valid != 0) ? WC_OK_COLOR : WC_ERROR_COLOR,
                            &font20_maple_bold);

    if (sensor != NULL && sensor->valid != 0)
    {
        weather_clock_format_tenths(value, sizeof(value), sensor->temperature_tenths);
        snprintf(line, sizeof(line), "Temp: %s C", value);
    }
    else
    {
        snprintf(line, sizeof(line), "Temp: --.- C");
    }
    weather_clock_write_row(116, UI_LINE_H, line, WC_FG_COLOR, &font20_maple_bold);

    if (sensor != NULL && sensor->valid != 0)
    {
        weather_clock_format_tenths(value, sizeof(value), sensor->humidity_tenths);
        snprintf(line, sizeof(line), "Humi: %s %%", value);
    }
    else
    {
        snprintf(line, sizeof(line), "Humi: --.- %%");
    }
    weather_clock_write_row(148, UI_LINE_H, line, WC_FG_COLOR, &font20_maple_bold);
}

void weather_clock_show_page(weather_clock_page_t page, const char *ip, const clock_time_t *time,
                             const clock_time_t *last_sync, const weather_clock_sensor_t *sensor,
                             bool wifi_ok, bool sntp_ok)
{
    switch (page)
    {
    case WEATHER_CLOCK_PAGE_MAIN:
        weather_clock_show_main(time, sensor);
        break;
    case WEATHER_CLOCK_PAGE_NETWORK:
        weather_clock_show_network(ip, wifi_ok, sntp_ok, last_sync);
        break;
    case WEATHER_CLOCK_PAGE_SENSOR:
        weather_clock_show_sensor(sensor);
        break;
    default:
        weather_clock_show_main(time, sensor);
        break;
    }
}

void weather_clock_update_time(weather_clock_page_t page, const clock_time_t *time)
{
    char line[24];

    if (page != WEATHER_CLOCK_PAGE_MAIN)
        return;

    weather_clock_format_time(line, sizeof(line), time);
    weather_clock_write_row(118, 36, line, WC_TIME_COLOR, &font32_maple_bold);

    weather_clock_format_date(line, sizeof(line), time);
    weather_clock_write_row(168, 28, line, WC_FG_COLOR, &font24_maple_bold);
}

void weather_clock_update_sensor(weather_clock_page_t page, const weather_clock_sensor_t *sensor)
{
    char line[32];

    if (page == WEATHER_CLOCK_PAGE_MAIN)
    {
        weather_clock_format_sensor_line(line, sizeof(line), sensor);
        weather_clock_write_row(UI_TEMP_ROW_Y, 20, line, WC_OK_COLOR, &font16_maple);
        return;
    }

    if (page == WEATHER_CLOCK_PAGE_SENSOR)
    {
        weather_clock_show_sensor(sensor);
    }
}

void weather_clock_update_status(const char *status)
{
    weather_clock_write_row(218, UI_LINE_H, status, WC_WARN_COLOR, &font20_maple_bold);
}
