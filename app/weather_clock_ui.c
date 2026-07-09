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
    ui_fill_color(0, y, UI_WIDTH - 1, y + height, WC_BG_COLOR);
    ui_write_string(20, y, text, color, WC_BG_COLOR, font);
}

static void weather_clock_format_time(char *buf, size_t buf_size, const clock_time_t *time)
{
    snprintf(buf, buf_size, "%02d:%02d:%02d", time->hour, time->minute, time->second);
}

static void weather_clock_format_date(char *buf, size_t buf_size, const clock_time_t *time)
{
    snprintf(buf, buf_size, "%04d-%02d-%02d", time->year, time->month, time->day);
}

void weather_clock_show_boot(void)
{
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, WC_BG_COLOR);
    ui_write_string(20, 34, "Weather Clock", WC_FG_COLOR, WC_BG_COLOR, &font24_maple_bold);
    weather_clock_write_row(96, 24, "WiFi: ...", WC_WARN_COLOR, &font20_maple_bold);
    weather_clock_write_row(126, 24, "IP: ...", WC_FG_COLOR, &font20_maple_bold);
    weather_clock_write_row(176, 36, "--:--:--", WC_TIME_COLOR, &font32_maple_bold);
    weather_clock_write_row(242, 28, "----/--/--", WC_FG_COLOR, &font24_maple_bold);
    weather_clock_write_row(292, 24, "T: --.-C  H: --.-%", WC_OK_COLOR, &font20_maple_bold);
}

void weather_clock_show_wifi_status(bool ok)
{
    weather_clock_write_row(96, 24, ok ? "WiFi: OK" : "WiFi FAIL",
                            ok ? WC_OK_COLOR : WC_ERROR_COLOR, &font20_maple_bold);
}

void weather_clock_show_error(const char *message)
{
    weather_clock_write_row(176, 42, message, WC_ERROR_COLOR, &font32_maple_bold);
}

void weather_clock_show_main(const char *ip, const clock_time_t *time)
{
    char line[48];

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, WC_BG_COLOR);
    ui_write_string(20, 24, "Weather Clock", WC_FG_COLOR, WC_BG_COLOR, &font24_maple_bold);
    weather_clock_write_row(64, 24, "WiFi: OK", WC_OK_COLOR, &font20_maple_bold);
    snprintf(line, sizeof(line), "IP: %s", ip);
    weather_clock_write_row(94, 24, line, WC_OK_COLOR, &font20_maple_bold);
    weather_clock_update_time(time);
    weather_clock_write_row(292, 24, "T: --.-C  H: --.-%", WC_OK_COLOR, &font20_maple_bold);
}

void weather_clock_update_time(const clock_time_t *time)
{
    char line[24];

    weather_clock_format_time(line, sizeof(line), time);
    weather_clock_write_row(134, 36, line, WC_TIME_COLOR, &font32_maple_bold);

    weather_clock_format_date(line, sizeof(line), time);
    weather_clock_write_row(224, 28, line, WC_FG_COLOR, &font24_maple_bold);
}

void weather_clock_update_status(const char *status)
{
    weather_clock_write_row(260, 24, status, WC_WARN_COLOR, &font20_maple_bold);
}
