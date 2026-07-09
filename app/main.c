#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "app.h"
#include "ui.h"
#include "wifi.h"
#include "page.h"
#include "aht20.h"
#include "esp_at.h"
#include "rtc.h"
#include "weather_clock_ui.h"
#include "button.h"
#include "weather_api.h"
#include "weather_config.h"

#define ENABLE_LCD_SELF_TEST       0
#define ENABLE_AHT20_LCD_TEST      0
#define ENABLE_WIFI_SNTP_LCD_TEST  0
#define ENABLE_ESP_AT_BASIC_TEST   1

#define LCD_TEST_PAGE_DELAY_MS         1000
#define AHT20_LCD_REFRESH_MS           2000
#define WIFI_TEST_LOOP_MS              1000
#define WIFI_RETRY_DELAY_MS            3000
#define WIFI_CONNECT_TIMEOUT_MS        15000
#define SNTP_SYNC_TIMEOUT_MS           15000
#define WIFI_TEST_AHT20_REFRESH_MS     5000
#define WIFI_TEST_LINK_CHECK_MS        5000
#define ESP_AT_TEST_RETRY_DELAY_MS     3000
#define ESP_AT_TEST_SHORT_TIMEOUT_MS   2000
#define ESP_AT_TEST_GMR_TIMEOUT_MS     3000
#define ESP_AT_TIME_REFRESH_MS         1000
#define ESP_AT_BUTTON_POLL_MS          50
#define AHT20_SENSOR_REFRESH_MS        2000
#define ESP_AT_TIME_RESYNC_MS          (30 * 60 * 1000)
#define WEATHER_UPDATE_MS              (30 * 60 * 1000)

extern void board_lowlevel_init(void);
extern void board_init(void);

static void lcd_test_fill_page(uint16_t bg_color, uint16_t fg_color, const char *label)
{
    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);
    ui_write_string(70, 140, label, fg_color, bg_color, &font32_maple_bold);
    vTaskDelay(pdMS_TO_TICKS(LCD_TEST_PAGE_DELAY_MS));
}

static void lcd_test_draw_graphics_page(void)
{
    const uint16_t bg_color = 0x0000;

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);

    ui_fill_color(10, 10, 229, 13, 0xFFFF);
    ui_fill_color(10, 306, 229, 309, 0xFFFF);
    ui_fill_color(10, 10, 13, 309, 0xFFFF);
    ui_fill_color(226, 10, 229, 309, 0xFFFF);

    ui_fill_color(20, 70, 219, 73, 0xF800);
    ui_fill_color(20, 140, 219, 143, 0x07E0);
    ui_fill_color(20, 210, 219, 213, 0x001F);
    ui_fill_color(118, 20, 121, 299, mkcolor(255, 255, 0));

    ui_write_string(24, 26, "TOP LEFT", 0xFFFF, bg_color, &font20_maple_bold);
    ui_write_string(72, 150, "CENTER", 0xFFFF, bg_color, &font24_maple_bold);
    ui_write_string(32, 274, "Weather Clock", 0xFFFF, bg_color, &font20_maple_bold);

    vTaskDelay(pdMS_TO_TICKS(LCD_TEST_PAGE_DELAY_MS));
}

static void lcd_test_draw_text_page(void)
{
    const uint16_t bg_color = 0x0000;
    const uint16_t fg_color = 0xFFFF;

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);
    ui_write_string(20, 30, "ST7789 OK", fg_color, bg_color, &font32_maple_bold);
    ui_write_string(20, 90, "SPI LCD TEST", mkcolor(0, 255, 234), bg_color, &font24_maple_bold);
    ui_write_string(20, 145, "Hello STM32", fg_color, bg_color, &font24_maple_bold);
    ui_write_string(20, 195, "1234567890", mkcolor(255, 255, 0), bg_color, &font24_maple_bold);
    ui_write_string(20, 245, "Weather Clock", fg_color, bg_color, &font24_maple_bold);
    vTaskDelay(pdMS_TO_TICKS(LCD_TEST_PAGE_DELAY_MS));
}

static void ST7789_DisplaySelfTest(void)
{
    printf("[LCD TEST] ST7789 display self test start\r\n");

    lcd_test_fill_page(0x0000, 0xFFFF, "BLACK");
    lcd_test_fill_page(0xFFFF, 0x0000, "WHITE");
    lcd_test_fill_page(0xF800, 0xFFFF, "RED");
    lcd_test_fill_page(0x07E0, 0x0000, "GREEN");
    lcd_test_fill_page(0x001F, 0xFFFF, "BLUE");

    lcd_test_draw_text_page();
    lcd_test_draw_graphics_page();

    printf("[LCD TEST] ST7789 display self test done\r\n");
}

static int to_tenths(float value)
{
    if (value >= 0.0f)
        return (int)(value * 10.0f + 0.5f);
    return (int)(value * 10.0f - 0.5f);
}

static void format_tenths(char *buf, size_t buf_size, int value)
{
    int abs_value = value < 0 ? -value : value;
    snprintf(buf, buf_size, "%s%d.%d", value < 0 ? "-" : "", abs_value / 10, abs_value % 10);
}

static void AHT20_LCD_ShowError(const char *line2, const char *line3)
{
    const uint16_t bg_color = 0x0000;
    const uint16_t fg_color = 0xFFFF;

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);
    ui_write_string(20, 40, "Weather Clock", fg_color, bg_color, &font24_maple_bold);
    ui_write_string(20, 130, line2, mkcolor(255, 80, 80), bg_color, &font24_maple_bold);
    ui_write_string(20, 190, line3, fg_color, bg_color, &font24_maple_bold);
}

static void AHT20_LCD_ShowData(int temperature_tenths, int humidity_tenths)
{
    char temp_str[32];
    char humi_str[32];
    char value_str[16];
    const uint16_t bg_color = 0x0000;
    const uint16_t fg_color = 0xFFFF;

    format_tenths(value_str, sizeof(value_str), temperature_tenths);
    snprintf(temp_str, sizeof(temp_str), "%s C", value_str);

    format_tenths(value_str, sizeof(value_str), humidity_tenths);
    snprintf(humi_str, sizeof(humi_str), "%s %%", value_str);

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);
    ui_write_string(20, 28, "Weather Clock", fg_color, bg_color, &font24_maple_bold);
    ui_write_string(20, 90, "Indoor Temp:", mkcolor(255, 255, 0), bg_color, &font24_maple_bold);
    ui_write_string(20, 126, temp_str, fg_color, bg_color, &font32_maple_bold);
    ui_write_string(20, 192, "Humidity:", mkcolor(0, 255, 234), bg_color, &font24_maple_bold);
    ui_write_string(20, 228, humi_str, fg_color, bg_color, &font32_maple_bold);
    ui_write_string(20, 286, "AHT20 OK", mkcolor(120, 255, 120), bg_color, &font20_maple_bold);
}

static bool aht20_read_tenths(bool *sensor_ready, int *temperature_tenths, int *humidity_tenths)
{
    float temperature = 0.0f;
    float humidity = 0.0f;

    if (!*sensor_ready)
    {
        *sensor_ready = aht20_init() > 0;
        if (!*sensor_ready)
        {
            return false;
        }
    }

    if (!aht20_start_measurement() || !aht20_wait_for_measurement())
    {
        *sensor_ready = false;
        return false;
    }

    if (!aht20_read_measurement(&temperature, &humidity))
    {
        *sensor_ready = false;
        return false;
    }

    *temperature_tenths = to_tenths(temperature);
    *humidity_tenths = to_tenths(humidity);
    return true;
}

static void AHT20_LCD_Test(void)
{
    bool sensor_ready = false;

    while (1)
    {
        int temperature_tenths = 0;
        int humidity_tenths = 0;

        if (!sensor_ready)
        {
                sensor_ready = aht20_init() > 0;
            if (!sensor_ready)
            {
                printf("[AHT20] init failed\r\n");
                AHT20_LCD_ShowError("AHT20 INIT ERROR", "Check Sensor");
                vTaskDelay(pdMS_TO_TICKS(AHT20_LCD_REFRESH_MS));
                continue;
            }
        }

        if (!aht20_read_tenths(&sensor_ready, &temperature_tenths, &humidity_tenths))
        {
            printf("[AHT20] read failed\r\n");
            AHT20_LCD_ShowError("AHT20 ERROR", "Check Sensor");
            vTaskDelay(pdMS_TO_TICKS(AHT20_LCD_REFRESH_MS));
            continue;
        }

        printf("[AHT20] Temp: %d.%d C, Humi: %d.%d %%\r\n",
            temperature_tenths / 10, abs(temperature_tenths % 10),
            humidity_tenths / 10, abs(humidity_tenths % 10));

        AHT20_LCD_ShowData(temperature_tenths, humidity_tenths);
        vTaskDelay(pdMS_TO_TICKS(AHT20_LCD_REFRESH_MS));
    }
}

static void lcd_write_row(uint16_t y, uint16_t clear_height, const char *text, uint16_t color, const font_t *font)
{
    const uint16_t bg_color = 0x0000;

    ui_fill_color(0, y, UI_WIDTH - 1, y + clear_height, bg_color);
    ui_write_string(20, y, text, color, bg_color, font);
}

static void WiFi_SNTP_LCD_DrawTemplate(void)
{
    const uint16_t bg_color = 0x0000;
    const uint16_t fg_color = 0xFFFF;

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);
    ui_write_string(20, 18, "Weather Clock", fg_color, bg_color, &font24_maple_bold);

    lcd_write_row(68, 28, "WiFi: Idle", mkcolor(255, 255, 0), &font24_maple_bold);
    lcd_write_row(102, 28, "SNTP: Idle", mkcolor(255, 255, 0), &font24_maple_bold);
    lcd_write_row(140, 24, "Time:", fg_color, &font20_maple_bold);
    lcd_write_row(170, 36, "--:--:--", mkcolor(0, 255, 234), &font32_maple_bold);
    lcd_write_row(220, 24, "Date:", fg_color, &font20_maple_bold);
    lcd_write_row(248, 28, "----/--/--", fg_color, &font24_maple_bold);
    lcd_write_row(288, 24, "Indoor: --.- C  --.- %", mkcolor(120, 255, 120), &font20_maple_bold);
}

static void WiFi_SNTP_LCD_ShowWiFi(const char *status, uint16_t color)
{
    char line[48];
    snprintf(line, sizeof(line), "WiFi: %s", status);
    lcd_write_row(68, 28, line, color, &font24_maple_bold);
}

static void WiFi_SNTP_LCD_ShowSNTP(const char *status, uint16_t color)
{
    char line[48];
    snprintf(line, sizeof(line), "SNTP: %s", status);
    lcd_write_row(102, 28, line, color, &font24_maple_bold);
}

static void WiFi_SNTP_LCD_ShowTime(const rtc_date_time_t *date)
{
    char line[16];
    snprintf(line, sizeof(line), "%02u:%02u:%02u", date->hour, date->minute, date->second);
    lcd_write_row(170, 36, line, mkcolor(0, 255, 234), &font32_maple_bold);
}

static void WiFi_SNTP_LCD_ShowDate(const rtc_date_time_t *date)
{
    char line[24];
    snprintf(line, sizeof(line), "%04u-%02u-%02u", date->year, date->month, date->day);
    lcd_write_row(248, 28, line, 0xFFFF, &font24_maple_bold);
}

static void WiFi_SNTP_LCD_ShowIndoorError(void)
{
    lcd_write_row(288, 24, "Indoor: AHT20 ERROR", mkcolor(255, 80, 80), &font20_maple_bold);
}

static void WiFi_SNTP_LCD_ShowIndoorData(int temperature_tenths, int humidity_tenths)
{
    char temp_str[16];
    char humi_str[16];
    char line[48];

    format_tenths(temp_str, sizeof(temp_str), temperature_tenths);
    format_tenths(humi_str, sizeof(humi_str), humidity_tenths);
    snprintf(line, sizeof(line), "Indoor: %s C  %s %%", temp_str, humi_str);
    lcd_write_row(288, 24, line, mkcolor(120, 255, 120), &font20_maple_bold);
}

static void ESP_AT_LCD_ShowLine(uint16_t y, const char *text, uint16_t color)
{
    const uint16_t bg_color = 0x0000;

    ui_fill_color(0, y, UI_WIDTH - 1, y + 24, bg_color);
    ui_write_string(20, y, text, color, bg_color, &font20_maple_bold);
}

static void ESP_AT_LCD_ShowProgress(const char *text, uint16_t color)
{
    ESP_AT_LCD_ShowLine(120, text, color);
}

static void ESP_AT_LCD_ShowStatus(const char *label, const char *value, uint16_t y, uint16_t color)
{
    char line[48];

    snprintf(line, sizeof(line), "%s: %s", label, value);
    ESP_AT_LCD_ShowLine(y, line, color);
}

static void ESP_AT_LCD_ShowTemplate(void)
{
    const uint16_t bg_color = 0x0000;
    const uint16_t fg_color = 0xFFFF;

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);
    ui_write_string(20, 36, "Weather Clock", fg_color, bg_color, &font24_maple_bold);
    ui_write_string(20, 92, "ESP AT TEST", mkcolor(255, 255, 0), bg_color, &font32_maple_bold);
    ESP_AT_LCD_ShowProgress("Testing...", mkcolor(0, 255, 234));
    ESP_AT_LCD_ShowStatus("ESP AT", "...", 160, fg_color);
    ESP_AT_LCD_ShowStatus("GMR", "...", 186, fg_color);
    ESP_AT_LCD_ShowStatus("MODE", "...", 212, fg_color);
    ESP_AT_LCD_ShowStatus("WIFI", "...", 238, fg_color);
    ESP_AT_LCD_ShowStatus("IP", "...", 264, fg_color);
}

static void ESP_AT_LCD_ShowClockTemplate(const char *ip)
{
    const uint16_t bg_color = 0x0000;
    const uint16_t fg_color = 0xFFFF;

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, bg_color);
    ui_write_string(20, 34, "Weather Clock", fg_color, bg_color, &font24_maple_bold);
    ESP_AT_LCD_ShowStatus("WIFI", "OK", 96, mkcolor(120, 255, 120));
    ESP_AT_LCD_ShowStatus("IP", ip, 126, mkcolor(120, 255, 120));
    ESP_AT_LCD_ShowStatus("TIME", "--:--:--", 176, mkcolor(0, 255, 234));
    ESP_AT_LCD_ShowStatus("DATE", "----/--/--", 206, fg_color);
}

static void ESP_AT_LCD_ShowClockTime(const rtc_date_time_t *date)
{
    char time_text[16];
    char date_text[16];

    snprintf(time_text, sizeof(time_text), "%02u:%02u:%02u", date->hour, date->minute, date->second);
    snprintf(date_text, sizeof(date_text), "%04u-%02u-%02u", date->year, date->month, date->day);
    ESP_AT_LCD_ShowStatus("TIME", time_text, 176, mkcolor(0, 255, 234));
    ESP_AT_LCD_ShowStatus("DATE", date_text, 206, 0xFFFF);
}

static void esp_test_log_response(void)
{
    printf("[ESP TEST] Response:\r\n%s\r\n", esp_at_last_response());
}

static void clock_time_from_rtc(const rtc_date_time_t *rtc_time, clock_time_t *clock_time)
{
    clock_time->year = rtc_time->year;
    clock_time->month = rtc_time->month;
    clock_time->day = rtc_time->day;
    clock_time->hour = rtc_time->hour;
    clock_time->minute = rtc_time->minute;
    clock_time->second = rtc_time->second;
}

static void rtc_time_set_from_sntp(const esp_date_time_t *esp_time, rtc_date_time_t *rtc_time)
{
    rtc_time->year = esp_time->year;
    rtc_time->month = esp_time->month;
    rtc_time->day = esp_time->day;
    rtc_time->hour = esp_time->hour;
    rtc_time->minute = esp_time->minute;
    rtc_time->second = esp_time->second;
    rtc_time->weekday = esp_time->weekday;
    rtc_set_time(rtc_time);
}

static void rtc_time_tick(clock_time_t *clock_time)
{
    rtc_date_time_t rtc_time = { 0 };

    rtc_get_time(&rtc_time);
    if (rtc_time.year >= 2000)
    {
        clock_time_from_rtc(&rtc_time, clock_time);
    }
}

static int sensor_to_tenths(float value)
{
    if (value >= 0.0f)
        return (int)(value * 10.0f + 0.5f);
    return (int)(value * 10.0f - 0.5f);
}

static void weather_clock_read_sensor(weather_clock_sensor_t *sensor)
{
    aht20_data_t data = { 0 };

    if (sensor == NULL)
        return;

    if (aht20_read(&data) > 0 && data.valid != 0)
    {
        sensor->temperature_tenths = sensor_to_tenths(data.temperature);
        sensor->humidity_tenths = sensor_to_tenths(data.humidity);
        sensor->valid = 1;
        printf("[AHT20] read OK: T=%d.%d H=%d.%d\r\n",
               sensor->temperature_tenths / 10, abs(sensor->temperature_tenths % 10),
               sensor->humidity_tenths / 10, abs(sensor->humidity_tenths % 10));
        return;
    }

    sensor->valid = 0;
    printf("[AHT20] read FAIL\r\n");
}

static void weather_clock_copy_weather(weather_clock_weather_t *dst, const weather_api_info_t *src)
{
    if (dst == NULL || src == NULL)
        return;

    snprintf(dst->city, sizeof(dst->city), "%s", src->city);
    snprintf(dst->weather, sizeof(dst->weather), "%s", src->weather);
    snprintf(dst->temperature, sizeof(dst->temperature), "%s", src->temperature);
    snprintf(dst->humidity, sizeof(dst->humidity), "%s", src->humidity);
    snprintf(dst->winddirection, sizeof(dst->winddirection), "%s", src->winddirection);
    snprintf(dst->windpower, sizeof(dst->windpower), "%s", src->windpower);
    snprintf(dst->reporttime, sizeof(dst->reporttime), "%s", src->reporttime);
    dst->valid = src->valid;
    dst->last_fail = 0;
}

static bool weather_clock_fetch_weather(weather_clock_weather_t *weather)
{
    weather_api_info_t api_weather = { 0 };

    if (weather_api_fetch(&api_weather) > 0 && api_weather.valid != 0)
    {
        weather_clock_copy_weather(weather, &api_weather);
        return true;
    }

    if (weather != NULL)
    {
        if (weather->city[0] == '\0')
            snprintf(weather->city, sizeof(weather->city), "%s", WEATHER_CITY_NAME);
        weather->last_fail = 1;
    }

    return false;
}

static bool esp_at_self_test(void)
{
    printf("[ESP TEST] wait ESP boot...\r\n");
    if (!esp_at_init())
    {
        printf("[ESP TEST] ESP init failed\r\n");
        return false;
    }

    weather_clock_update_status("ESP AT...");
    printf("[ESP TEST] Send: AT\r\n");
    if (!esp_at_command("AT", ESP_AT_TEST_SHORT_TIMEOUT_MS))
    {
        esp_test_log_response();
        printf("[ESP TEST] ESP AT FAIL\r\n");
        return false;
    }
    esp_test_log_response();
    printf("[ESP TEST] ESP AT OK\r\n");

    weather_clock_update_status("ESP AT OK");
    printf("[ESP TEST] Send: AT+GMR\r\n");
    if (!esp_at_command("AT+GMR", ESP_AT_TEST_GMR_TIMEOUT_MS))
    {
        esp_test_log_response();
        printf("[ESP TEST] GMR FAIL\r\n");
        return false;
    }
    esp_test_log_response();
    printf("[ESP TEST] GMR OK\r\n");

    weather_clock_update_status("GMR OK");
    return true;
}

static bool esp_at_wifi_connect(char *ip, uint32_t ip_size, const char **failed_stage)
{
    if (failed_stage != NULL)
        *failed_stage = "WIFI FAIL";

    weather_clock_update_status("ATE0...");
    printf("[ESP TEST] Send: ATE0\r\n");
    if (!esp_at_echo_off())
    {
        esp_test_log_response();
        printf("[ESP TEST] ATE0 FAIL\r\n");
        if (failed_stage != NULL)
            *failed_stage = "ATE0 FAIL";
        return false;
    }
    esp_test_log_response();

    weather_clock_update_status("WiFi Mode...");
    printf("[ESP TEST] Send: AT+CWMODE=1\r\n");
    if (!esp_at_wifi_init())
    {
        esp_test_log_response();
        printf("[ESP TEST] WIFI MODE FAIL\r\n");
        if (failed_stage != NULL)
            *failed_stage = "MODE FAIL";
        return false;
    }
    esp_test_log_response();
    printf("[ESP TEST] WIFI MODE OK\r\n");
    weather_clock_update_status("WiFi Mode OK");

    weather_clock_update_status("WiFi Join...");
    printf("[ESP TEST] Send: AT+CWJAP=\"%s\",\"******\"\r\n", WIFI_SSID);
    if (!esp_at_connect_wifi(WIFI_SSID, WIFI_PASSWORD, NULL))
    {
        esp_test_log_response();
        printf("[ESP TEST] WIFI JOIN FAIL\r\n");
        if (failed_stage != NULL)
            *failed_stage = "WIFI FAIL";
        return false;
    }
    esp_test_log_response();
    printf("[ESP TEST] WIFI JOIN OK\r\n");
    weather_clock_show_wifi_status(true);

    weather_clock_update_status("Getting IP...");
    printf("[ESP TEST] Send: AT+CIFSR\r\n");
    if (!esp_at_get_ip(ip, ip_size))
    {
        esp_test_log_response();
        printf("[ESP TEST] GET IP FAIL\r\n");
        if (failed_stage != NULL)
            *failed_stage = "IP FAIL";
        return false;
    }
    esp_test_log_response();
    printf("[ESP TEST] IP OK\r\n");
    printf("[ESP TEST] IP: %s\r\n", ip);

    return true;
}

static bool esp_at_sntp_sync_clock(rtc_date_time_t *date_time)
{
    const TickType_t retry_ticks = pdMS_TO_TICKS(1000);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS);
    TickType_t start_tick = xTaskGetTickCount();

    weather_clock_update_status("SNTP Sync...");
    printf("[ESP TEST] Send: AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.ntp.org.cn\",\"pool.ntp.org\"\r\n");
    if (!esp_at_sntp_config())
    {
        esp_test_log_response();
        printf("[ESP TEST] SNTP CFG FAIL\r\n");
        weather_clock_show_error("TIME FAIL");
        return false;
    }
    esp_test_log_response();
    printf("[ESP TEST] SNTP CFG OK\r\n");
    printf("[ESP TEST] SNTP OK\r\n");

    weather_clock_update_status("Time Sync...");
    while ((xTaskGetTickCount() - start_tick) < timeout_ticks)
    {
        esp_date_time_t esp_date = { 0 };

        printf("[ESP TEST] Send: AT+CIPSNTPTIME?\r\n");
        if (esp_at_get_time(&esp_date))
        {
            esp_test_log_response();

            rtc_time_set_from_sntp(&esp_date, date_time);

            printf("[ESP TEST] TIME OK: %04u-%02u-%02u %02u:%02u:%02u\r\n",
                date_time->year, date_time->month, date_time->day,
                date_time->hour, date_time->minute, date_time->second);
            printf("[ESP TEST] TIME SYNC OK\r\n");
            return true;
        }

        esp_test_log_response();
        vTaskDelay(retry_ticks);
    }

    printf("[ESP TEST] TIME FAIL\r\n");
    weather_clock_show_error("TIME FAIL");
    return false;
}

static void ESP_AT_BasicTest(void)
{
    button_init();

    while (1)
    {
        char ip[32];
        rtc_date_time_t date_time = { 0 };
        clock_time_t clock_time = { 0 };
        clock_time_t last_sync_time = { 0 };
        weather_clock_sensor_t sensor = { 0 };
        weather_clock_weather_t weather = { 0 };
        weather_clock_page_t current_page = WEATHER_CLOCK_PAGE_MAIN;
        const char *failed_stage = "WIFI FAIL";
        bool wifi_ok = false;
        bool sntp_ok = false;
        bool sensor_init_ok = false;

        weather_clock_show_boot();
        weather_api_init();

        printf("[ESP TEST] start\r\n");
        printf("[ESP TEST] UART2 init: 115200 8N1\r\n");
        printf("[ESP TEST] STM32 PA2 / USART2_TX -> ESP32-C3 GPIO6 / AT_RX\r\n");
        printf("[ESP TEST] STM32 PA3 / USART2_RX -> ESP32-C3 GPIO7 / AT_TX\r\n");

        sensor_init_ok = aht20_init() > 0;
        printf(sensor_init_ok ? "[AHT20] init OK\r\n" : "[AHT20] init FAIL\r\n");

        if (!esp_at_self_test())
        {
            weather_clock_show_error("AT FAIL");
            vTaskDelay(pdMS_TO_TICKS(ESP_AT_TEST_RETRY_DELAY_MS));
            continue;
        }

        if (!esp_at_wifi_connect(ip, sizeof(ip), &failed_stage))
        {
            weather_clock_show_wifi_status(false);
            weather_clock_show_error(failed_stage);
            vTaskDelay(pdMS_TO_TICKS(ESP_AT_TEST_RETRY_DELAY_MS));
            continue;
        }
        printf("[ESP TEST] WiFi OK\r\n");
        printf("[ESP TEST] IP OK\r\n");
        wifi_ok = true;
        weather_clock_show_wifi_status(true);
        weather_clock_update_status("IP OK");

        if (!esp_at_sntp_sync_clock(&date_time))
        {
            vTaskDelay(pdMS_TO_TICKS(ESP_AT_TEST_RETRY_DELAY_MS));
            continue;
        }

        clock_time_from_rtc(&date_time, &clock_time);
        last_sync_time = clock_time;
        sntp_ok = true;
        if (sensor_init_ok)
        {
            weather_clock_read_sensor(&sensor);
        }
        weather_clock_show_page(current_page, ip, &clock_time, &last_sync_time, &sensor, &weather, wifi_ok, sntp_ok);
        printf("[ESP TEST] MAIN UI START\r\n");

        weather_clock_fetch_weather(&weather);
        weather_clock_show_page(current_page, ip, &clock_time, &last_sync_time, &sensor, &weather, wifi_ok, sntp_ok);

        TickType_t last_sync_tick = xTaskGetTickCount();
        TickType_t last_time_update_tick = 0;
        TickType_t last_sensor_update_tick = xTaskGetTickCount();
        TickType_t last_weather_update_tick = xTaskGetTickCount();

        while (1)
        {
            TickType_t now_tick = xTaskGetTickCount();

            if (button_poll_short_press())
            {
                current_page = (weather_clock_page_t)((current_page + 1) % WEATHER_CLOCK_PAGE_COUNT);
                printf("[UI] Switch page: %d\r\n", current_page);
                weather_clock_show_page(current_page, ip, &clock_time, &last_sync_time, &sensor, &weather, wifi_ok, sntp_ok);
            }

            if (last_time_update_tick == 0 ||
                (now_tick - last_time_update_tick) >= pdMS_TO_TICKS(ESP_AT_TIME_REFRESH_MS))
            {
                rtc_time_tick(&clock_time);
                if (clock_time.year >= 2000)
                {
                    weather_clock_update_time(current_page, &clock_time);
                }
                last_time_update_tick = now_tick;
            }

            if ((now_tick - last_sensor_update_tick) >= pdMS_TO_TICKS(AHT20_SENSOR_REFRESH_MS))
            {
                if (!sensor_init_ok)
                {
                    sensor_init_ok = aht20_init() > 0;
                    if (sensor_init_ok)
                    {
                        printf("[AHT20] init OK\r\n");
                    }
                }

                if (sensor_init_ok)
                {
                    weather_clock_read_sensor(&sensor);
                }
                else
                {
                    sensor.valid = 0;
                    printf("[AHT20] read FAIL\r\n");
                }
                weather_clock_update_sensor(current_page, &sensor);
                last_sensor_update_tick = now_tick;
            }

            if ((now_tick - last_weather_update_tick) >= pdMS_TO_TICKS(WEATHER_UPDATE_MS))
            {
                if (weather_clock_fetch_weather(&weather))
                {
                    weather_clock_show_page(current_page, ip, &clock_time, &last_sync_time, &sensor, &weather, wifi_ok, sntp_ok);
                }
                else
                {
                    weather_clock_show_page(current_page, ip, &clock_time, &last_sync_time, &sensor, &weather, wifi_ok, sntp_ok);
                }
                last_weather_update_tick = xTaskGetTickCount();
            }

            if ((now_tick - last_sync_tick) >= pdMS_TO_TICKS(ESP_AT_TIME_RESYNC_MS))
            {
                printf("[ESP TEST] periodic time resync start\r\n");
                if (esp_at_sntp_sync_clock(&date_time))
                {
                    clock_time_from_rtc(&date_time, &clock_time);
                    last_sync_time = clock_time;
                    sntp_ok = true;
                    weather_clock_show_page(current_page, ip, &clock_time, &last_sync_time, &sensor, &weather, wifi_ok, sntp_ok);
                    printf("[ESP TEST] periodic time resync ok\r\n");
                }
                else
                {
                    sntp_ok = false;
                    weather_clock_show_page(current_page, ip, &clock_time, &last_sync_time, &sensor, &weather, wifi_ok, sntp_ok);
                    printf("[ESP TEST] periodic time resync failed\r\n");
                }
                last_sync_tick = xTaskGetTickCount();
            }

            vTaskDelay(pdMS_TO_TICKS(ESP_AT_BUTTON_POLL_MS));
        }
    }
}

static bool sntp_sync_rtc(void)
{
    const TickType_t retry_ticks = pdMS_TO_TICKS(1000);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(SNTP_SYNC_TIMEOUT_MS);
    TickType_t start_tick = xTaskGetTickCount();
    esp_date_time_t esp_date = { 0 };
    rtc_date_time_t rtc_date = { 0 };

    printf("[SNTP] syncing...\r\n");
    WiFi_SNTP_LCD_ShowSNTP("Syncing...", mkcolor(255, 255, 0));

    while ((xTaskGetTickCount() - start_tick) < timeout_ticks)
    {
        if (esp_at_sntp_get_time(&esp_date) && esp_date.year >= 2000)
        {
            rtc_date.year = esp_date.year;
            rtc_date.month = esp_date.month;
            rtc_date.day = esp_date.day;
            rtc_date.hour = esp_date.hour;
            rtc_date.minute = esp_date.minute;
            rtc_date.second = esp_date.second;
            rtc_date.weekday = esp_date.weekday;
            rtc_set_time(&rtc_date);

            printf("[SNTP] sync ok: %04u-%02u-%02u %02u:%02u:%02u\r\n",
                rtc_date.year, rtc_date.month, rtc_date.day,
                rtc_date.hour, rtc_date.minute, rtc_date.second);
            WiFi_SNTP_LCD_ShowSNTP("OK", mkcolor(120, 255, 120));
            WiFi_SNTP_LCD_ShowDate(&rtc_date);
            WiFi_SNTP_LCD_ShowTime(&rtc_date);
            return true;
        }

        vTaskDelay(retry_ticks);
    }

    printf("[SNTP] sync failed\r\n");
    WiFi_SNTP_LCD_ShowSNTP("ERROR", mkcolor(255, 80, 80));
    return false;
}

static bool wait_wifi_connected(esp_wifi_info_t *wifi_info)
{
    const TickType_t retry_ticks = pdMS_TO_TICKS(1000);
    const TickType_t timeout_ticks = pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS);
    TickType_t start_tick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_tick) < timeout_ticks)
    {
        esp_wifi_info_t info = { 0 };

        if (esp_at_get_wifi_info(&info) && info.connected)
        {
            if (wifi_info != NULL)
            {
                *wifi_info = info;
            }
            return true;
        }

        vTaskDelay(retry_ticks);
    }

    return false;
}

static void WiFi_SNTP_LCD_Test(void)
{
    bool sensor_ready = false;
    TickType_t last_sensor_tick = 0;
    TickType_t last_link_check_tick = 0;

    printf("[WIFI TEST] start\r\n");
    WiFi_SNTP_LCD_DrawTemplate();

    while (1)
    {
        esp_wifi_info_t wifi_info = { 0 };

        WiFi_SNTP_LCD_ShowWiFi("ESP init...", mkcolor(255, 255, 0));
        printf("[WIFI TEST] ESP init...\r\n");
        if (!esp_at_init())
        {
            printf("[WIFI TEST] ESP init failed\r\n");
            WiFi_SNTP_LCD_ShowWiFi("ESP INIT ERROR", mkcolor(255, 80, 80));
            WiFi_SNTP_LCD_ShowSNTP("Check ESP/Power", 0xFFFF);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
            continue;
        }

        printf("[WIFI TEST] WiFi stack init...\r\n");
        if (!esp_at_wifi_init())
        {
            printf("[WIFI TEST] WiFi stack init failed\r\n");
            WiFi_SNTP_LCD_ShowWiFi("AT WIFI ERROR", mkcolor(255, 80, 80));
            WiFi_SNTP_LCD_ShowSNTP("Check ESP AT FW", 0xFFFF);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
            continue;
        }

        printf("[WIFI TEST] SNTP init...\r\n");
        if (!esp_at_sntp_init())
        {
            printf("[WIFI TEST] SNTP init failed\r\n");
            WiFi_SNTP_LCD_ShowWiFi("OK", mkcolor(120, 255, 120));
            WiFi_SNTP_LCD_ShowSNTP("INIT ERROR", mkcolor(255, 80, 80));
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
            continue;
        }

        WiFi_SNTP_LCD_ShowWiFi("Connecting...", mkcolor(255, 255, 0));
        printf("[WIFI TEST] WiFi connecting...\r\n");
        if (!esp_at_connect_wifi(WIFI_SSID, WIFI_PASSWORD, NULL))
        {
            printf("[WIFI TEST] AT command no response\r\n");
            WiFi_SNTP_LCD_ShowWiFi("CONNECT CMD ERR", mkcolor(255, 80, 80));
            WiFi_SNTP_LCD_ShowSNTP("Check ESP/WiFi", 0xFFFF);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
            continue;
        }

        if (!wait_wifi_connected(&wifi_info))
        {
            printf("[WIFI TEST] WiFi connect timeout\r\n");
            WiFi_SNTP_LCD_ShowWiFi("ERROR", mkcolor(255, 80, 80));
            WiFi_SNTP_LCD_ShowSNTP("Check ESP/WiFi", 0xFFFF);
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
            continue;
        }

        printf("[WIFI TEST] WiFi connected\r\n");
        printf("[WIFI TEST] SSID: %s, RSSI: %d\r\n", wifi_info.ssid, wifi_info.rssi);
        WiFi_SNTP_LCD_ShowWiFi("OK", mkcolor(120, 255, 120));

        if (!sntp_sync_rtc())
        {
            vTaskDelay(pdMS_TO_TICKS(WIFI_RETRY_DELAY_MS));
            continue;
        }

        last_sensor_tick = 0;
        last_link_check_tick = 0;

        while (1)
        {
            TickType_t now_tick = xTaskGetTickCount();
            rtc_date_time_t now = { 0 };

            rtc_get_time(&now);
            if (now.year >= 2000)
            {
                WiFi_SNTP_LCD_ShowTime(&now);
                WiFi_SNTP_LCD_ShowDate(&now);
            }

            if (last_sensor_tick == 0 || (now_tick - last_sensor_tick) >= pdMS_TO_TICKS(WIFI_TEST_AHT20_REFRESH_MS))
            {
                int temperature_tenths = 0;
                int humidity_tenths = 0;

                if (aht20_read_tenths(&sensor_ready, &temperature_tenths, &humidity_tenths))
                {
                    printf("[AHT20] Temp: %d.%d C, Humi: %d.%d %%\r\n",
                        temperature_tenths / 10, abs(temperature_tenths % 10),
                        humidity_tenths / 10, abs(humidity_tenths % 10));
                    WiFi_SNTP_LCD_ShowIndoorData(temperature_tenths, humidity_tenths);
                }
                else
                {
                    printf("[AHT20] read failed\r\n");
                    WiFi_SNTP_LCD_ShowIndoorError();
                }

                last_sensor_tick = now_tick;
            }

            if (last_link_check_tick == 0 || (now_tick - last_link_check_tick) >= pdMS_TO_TICKS(WIFI_TEST_LINK_CHECK_MS))
            {
                esp_wifi_info_t link_info = { 0 };

                if (!esp_at_get_wifi_info(&link_info) || !link_info.connected)
                {
                    printf("[WIFI TEST] WiFi lost\r\n");
                    WiFi_SNTP_LCD_ShowWiFi("LOST, RETRY...", mkcolor(255, 80, 80));
                    WiFi_SNTP_LCD_ShowSNTP("WAIT RECONNECT", mkcolor(255, 255, 0));
                    break;
                }

                last_link_check_tick = now_tick;
            }

            vTaskDelay(pdMS_TO_TICKS(WIFI_TEST_LOOP_MS));
        }
    }
}

static void main_init(void *param)
{
    (void)param;
#if !ENABLE_LCD_SELF_TEST
    (void)ST7789_DisplaySelfTest;
#endif
#if !ENABLE_AHT20_LCD_TEST
    (void)AHT20_LCD_Test;
#endif
#if !ENABLE_WIFI_SNTP_LCD_TEST
    (void)WiFi_SNTP_LCD_Test;
#endif

    board_init();
    ui_init();

#if ENABLE_LCD_SELF_TEST
    ST7789_DisplaySelfTest();
#endif

#if ENABLE_ESP_AT_BASIC_TEST
    ESP_AT_BasicTest();
#elif ENABLE_WIFI_SNTP_LCD_TEST
    WiFi_SNTP_LCD_Test();
#elif ENABLE_AHT20_LCD_TEST
    AHT20_LCD_Test();
#else
    welcome_page_display();

    wifi_init();
    wifi_page_display();
    wifi_wait_connect();

    main_page_display();
    app_init();
#endif

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void)
{
    board_lowlevel_init();
    workqueue_init();

    xTaskCreate(main_init, "init", 1024, NULL, 9, NULL);

    vTaskStartScheduler();

    while (1)
    {
        ; // code should not run here
    }
}
