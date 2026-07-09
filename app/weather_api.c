#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_at.h"
#include "weather_api.h"
#include "weather_config.h"

#define WEATHER_API_HOST          "restapi.amap.com"
#define WEATHER_API_PATH          "/v3/weather/weatherInfo"
#define WEATHER_HTTP_TIMEOUT_MS   15000
#define WEATHER_HTTP_BUF_SIZE     4096

#ifndef WEATHER_USE_SSL
#define WEATHER_USE_SSL           0
#endif

#if WEATHER_USE_SSL
#define WEATHER_API_TRANSPORT     "SSL"
#define WEATHER_API_PORT          443
#else
#define WEATHER_API_TRANSPORT     "TCP"
#define WEATHER_API_PORT          80
#endif

static char weather_http_response[WEATHER_HTTP_BUF_SIZE];
static char weather_http_request[512];
static char weather_http_request_log[512];

static void weather_print_visible_text(const char *text, uint32_t max_len)
{
    uint32_t i;

    if (text == NULL)
        return;

    for (i = 0; text[i] != '\0' && i < max_len; i++)
    {
        char c = text[i];
        if (c == '\r')
            printf("\\r");
        else if (c == '\n')
            printf("\\n\r\n");
        else
            printf("%c", c);
    }
    printf("\r\n");
}

static bool weather_request_has_crlfcrlf(const char *request)
{
    uint32_t len;

    if (request == NULL)
        return false;

    len = strlen(request);
    return len >= 4 &&
        request[len - 4] == '\r' &&
        request[len - 3] == '\n' &&
        request[len - 2] == '\r' &&
        request[len - 1] == '\n';
}

static void weather_print_http_request_log(const char *request_log, uint32_t request_len)
{
    printf("[WEATHER] HTTP request len=%u\r\n", (unsigned int)request_len);
    printf("[WEATHER] HTTP request begin\r\n");
    weather_print_visible_text(request_log, 512);
    printf("[WEATHER] HTTP request end\r\n");
}

static int json_get_string(const char *json, const char *key, char *out, uint32_t out_size)
{
    const char *p;
    const char *start;
    uint32_t len = 0;

    if (json == NULL || key == NULL || out == NULL || out_size == 0)
        return -1;

    p = strstr(json, key);
    if (p == NULL)
        return -2;

    p = strchr(p, ':');
    if (p == NULL)
        return -3;

    start = strchr(p, '\"');
    if (start == NULL)
        return -4;
    start++;

    while (start[len] != '\0' && start[len] != '\"')
    {
        if (len + 1 < out_size)
        {
            out[len] = start[len];
        }
        len++;
    }

    if (start[len] != '\"')
        return -5;

    if (len >= out_size)
        len = out_size - 1;

    out[len] = '\0';
    return 0;
}

static const char *weather_find_json_body(const char *response)
{
    const char *json;

    if (response == NULL)
        return NULL;

    json = strstr(response, "\r\n\r\n");
    if (json != NULL)
    {
        json += 4;
        json = strchr(json, '{');
    }
    else
    {
        json = strchr(response, '{');
    }

    return json;
}

static bool weather_is_ascii_text(const char *text)
{
    if (text == NULL)
        return false;

    while (*text != '\0')
    {
        if ((unsigned char)*text < 0x20 || (unsigned char)*text > 0x7E)
            return false;
        text++;
    }

    return true;
}

int weather_api_init(void)
{
    return 1;
}

int weather_parse_amap_json(const char *json, weather_api_info_t *out)
{
    char status[8] = { 0 };
    char info[32] = { 0 };
    char infocode[12] = { 0 };

    if (json == NULL || out == NULL)
        return -1;

    memset(out, 0, sizeof(*out));

    if (json_get_string(json, "\"status\"", status, sizeof(status)) != 0 ||
        strcmp(status, "1") != 0)
    {
        json_get_string(json, "\"info\"", info, sizeof(info));
        json_get_string(json, "\"infocode\"", infocode, sizeof(infocode));
        printf("[WEATHER] API status FAIL\r\n");
        printf("[WEATHER] info=%s\r\n", info[0] != '\0' ? info : "unknown");
        printf("[WEATHER] infocode=%s\r\n", infocode[0] != '\0' ? infocode : "unknown");
        return -2;
    }

    if (json_get_string(json, "\"infocode\"", infocode, sizeof(infocode)) != 0 ||
        strcmp(infocode, "10000") != 0)
    {
        json_get_string(json, "\"info\"", info, sizeof(info));
        printf("[WEATHER] API status FAIL\r\n");
        printf("[WEATHER] info=%s\r\n", info[0] != '\0' ? info : "unknown");
        printf("[WEATHER] infocode=%s\r\n", infocode[0] != '\0' ? infocode : "unknown");
        return -3;
    }

    if (json_get_string(json, "\"city\"", out->city, sizeof(out->city)) != 0 ||
        json_get_string(json, "\"weather\"", out->weather, sizeof(out->weather)) != 0 ||
        json_get_string(json, "\"temperature\"", out->temperature, sizeof(out->temperature)) != 0 ||
        json_get_string(json, "\"humidity\"", out->humidity, sizeof(out->humidity)) != 0)
    {
        return -4;
    }

    json_get_string(json, "\"winddirection\"", out->winddirection, sizeof(out->winddirection));
    json_get_string(json, "\"windpower\"", out->windpower, sizeof(out->windpower));
    json_get_string(json, "\"reporttime\"", out->reporttime, sizeof(out->reporttime));

    if (!weather_is_ascii_text(out->city))
        snprintf(out->city, sizeof(out->city), "%s", WEATHER_CITY_NAME);
    if (!weather_is_ascii_text(out->weather))
        snprintf(out->weather, sizeof(out->weather), "OK");
    if (!weather_is_ascii_text(out->winddirection))
        snprintf(out->winddirection, sizeof(out->winddirection), "--");

    out->valid = 1;
    return 1;
}

int weather_api_fetch(weather_api_info_t *out)
{
    const char *json;
    int request_len;

    if (out == NULL)
        return -1;

    printf("[WEATHER] fetch start\r\n");

    request_len = snprintf(weather_http_request, sizeof(weather_http_request),
        "GET %s?city=%s&key=%s&extensions=base&output=JSON HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: WeatherClock/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        WEATHER_API_PATH,
        WEATHER_CITY_ADCODE,
        WEATHER_API_KEY,
        WEATHER_API_HOST);

    if (request_len <= 0 || request_len >= (int)sizeof(weather_http_request) ||
        !weather_request_has_crlfcrlf(weather_http_request))
    {
        printf("[WEATHER] fetch FAIL\r\n");
        return -2;
    }

    snprintf(weather_http_request_log, sizeof(weather_http_request_log),
        "GET %s?city=%s&key=***&extensions=base&output=JSON HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: WeatherClock/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        WEATHER_API_PATH,
        WEATHER_CITY_ADCODE,
        WEATHER_API_HOST);
    weather_print_http_request_log(weather_http_request_log, (uint32_t)strlen(weather_http_request));

    if (!esp_at_http_get_transport(WEATHER_API_TRANSPORT, WEATHER_API_HOST, WEATHER_API_PORT,
                                   weather_http_request, weather_http_response,
                                   sizeof(weather_http_response), WEATHER_HTTP_TIMEOUT_MS))
    {
        printf("[WEATHER] fetch FAIL\r\n");
        return -3;
    }

    printf("[WEATHER] HTTP response received, len=%u\r\n", (unsigned int)strlen(weather_http_response));

    json = weather_find_json_body(weather_http_response);
    if (json == NULL)
    {
        printf("[WEATHER] JSON not found\r\n");
        printf("[WEATHER] fetch FAIL\r\n");
        return -4;
    }

    printf("[WEATHER] JSON found\r\n");
    printf("[WEATHER] JSON preview:\r\n");
    weather_print_visible_text(json, 300);

    if (weather_parse_amap_json(json, out) <= 0)
    {
        printf("[WEATHER] parse FAIL\r\n");
        printf("[WEATHER] fetch FAIL\r\n");
        return -5;
    }

    printf("[WEATHER] parse OK: city=%s weather=%s temp=%s humi=%s\r\n",
           out->city, out->weather, out->temperature, out->humidity);
    printf("[WEATHER] fetch OK\r\n");
    printf("[WEATHER] next update in 30 min\r\n");
    return 1;
}
