#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stm32f4xx.h"
#include "esp_at.h"

#define ESP_AT_DEBUG             1
#define ESP_AT_BOOT_WAIT_MS      2000
#define ESP_AT_BOOT_RETRY_COUNT  5
#define ESP_AT_BOOT_RETRY_MS     500

typedef enum
{
    AT_ACK_NONE,
    AT_ACK_OK,
    AT_ACK_ERROR,
    AT_ACK_BUSY,
    AT_ACK_READY,
} at_ack_t;

static char *rxline;
static char rxbuf[4096];
static char txbuf[1024];
static uint32_t rxlen;
static at_ack_t rxack;
static SemaphoreHandle_t at_ack_sempahore;

static at_ack_t esp_at_execute_command(const char *command, uint32_t timeout);
static bool esp_at_write_command(const char *command, uint32_t timeout);
static bool esp_at_wait_boot(uint32_t timeout);
static void esp_at_usart_write(const char *data);
static void esp_at_usart_write_len(const char *data, uint32_t len);
static void esp_at_rx_reset(void);
static void esp_at_ack_drain(void);
static bool esp_at_wait_pattern(const char *pattern, uint32_t timeout);
static void esp_at_copy_response(char *response, uint32_t response_size);
static bool esp_at_send_raw_wait(const char *command, const char *pattern, uint32_t timeout);
static bool esp_at_wait_http_response(char *response, uint32_t response_size, uint32_t timeout);
static void esp_at_print_preview(const char *title, const char *data, uint32_t max_len);
static bool esp_at_parse_ipv4(const char *response, char *ip, uint32_t ip_size);
static at_ack_t match_internal_ack(const char *str);
static at_ack_t esp_at_usart_wait_receive(uint32_t timeout);

static void esp_at_io_init(void)
{
    /* STM32 USART2:
     * PA2 -> USART2_TX -> ESP32-C3 GPIO6 / AT_RX
     * PA3 -> USART2_RX -> ESP32-C3 GPIO7 / AT_TX
     */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_USART2);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_USART2);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

static void esp_at_usart_init(void)
{
    USART_InitTypeDef USART_InitStructure;
    USART_StructInit(&USART_InitStructure);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;

    USART_Init(USART2, &USART_InitStructure);
    USART_DMACmd(USART2, USART_DMAReq_Tx, ENABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART2, ENABLE);
}

static void esp_at_dma_init(void)
{
    DMA_InitTypeDef DMA_InitStruct;
    DMA_StructInit(&DMA_InitStruct);
    DMA_InitStruct.DMA_Channel = DMA_Channel_4;
    DMA_InitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
    DMA_InitStruct.DMA_DIR = DMA_DIR_MemoryToPeripheral;
    DMA_InitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStruct.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStruct.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStruct.DMA_FIFOMode = DMA_FIFOMode_Enable;
    DMA_InitStruct.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStruct.DMA_MemoryBurst = DMA_MemoryBurst_INC8;
    DMA_InitStruct.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream6, &DMA_InitStruct);
}

static void esp_at_int_init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    NVIC_SetPriority(USART2_IRQn, 5);
}

static void esp_at_lowlevel_init(void)
{
    esp_at_usart_init();
    esp_at_dma_init();
    esp_at_int_init();
    esp_at_io_init();
}

bool esp_at_init(void)
{
    if (at_ack_sempahore == NULL)
    {
        at_ack_sempahore = xSemaphoreCreateBinary();
        configASSERT(at_ack_sempahore);
    }

    esp_at_lowlevel_init();
    return esp_at_wait_boot(ESP_AT_BOOT_WAIT_MS + ESP_AT_BOOT_RETRY_COUNT * ESP_AT_BOOT_RETRY_MS);
}

static int esp_at_append_crlf(char *dst, size_t dst_size, const char *command)
{
    size_t len;

    if (dst == NULL || command == NULL || dst_size == 0)
        return 0;

    len = strlen(command);
    if (len >= 2 && command[len - 2] == '\r' && command[len - 1] == '\n')
        return snprintf(dst, dst_size, "%s", command);

    return snprintf(dst, dst_size, "%s\r\n", command);
}

static at_ack_t esp_at_execute_command(const char *command, uint32_t timeout)
{
    if (esp_at_append_crlf(txbuf, sizeof(txbuf), command) <= 0)
        return AT_ACK_NONE;

#if ESP_AT_DEBUG
    printf("[DEBUG] Send: %s", txbuf);
#endif

    esp_at_ack_drain();
    esp_at_rx_reset();
    esp_at_usart_write(txbuf);
    rxack = AT_ACK_NONE;
    at_ack_t ack = esp_at_usart_wait_receive(timeout);

#if ESP_AT_DEBUG
    printf("[DEBUG] Response:\r\n%s\r\n", rxbuf);
#endif

    return ack;
}

static bool esp_at_write_command(const char *command, uint32_t timeout)
{
    return esp_at_execute_command(command, timeout) == AT_ACK_OK;
}

const char *esp_at_last_response(void)
{
    return rxbuf;
}

static void esp_at_rx_reset(void)
{
    taskENTER_CRITICAL();
    rxlen = 0;
    rxbuf[0] = '\0';
    rxline = rxbuf;
    rxack = AT_ACK_NONE;
    taskEXIT_CRITICAL();
}

static void esp_at_ack_drain(void)
{
    if (at_ack_sempahore == NULL)
        return;

    while (xSemaphoreTake(at_ack_sempahore, 0) == pdPASS)
    {
        ;
    }
}

static void esp_at_usart_write(const char *data)
{
    esp_at_usart_write_len(data, strlen(data));
}

static void esp_at_usart_write_len(const char *data, uint32_t len)
{
    DMA1_Stream6->M0AR = (uint32_t)data;
    DMA1_Stream6->NDTR = len;

    DMA_ClearFlag(DMA1_Stream6, DMA_FLAG_TCIF6);
    DMA_Cmd(DMA1_Stream6, ENABLE);
}

bool esp_at_send_raw(const char *data, uint32_t len)
{
    if (data == NULL || len == 0)
        return false;

    esp_at_usart_write_len(data, len);
    return true;
}

static at_ack_t match_internal_ack(const char *str)
{
    if (strstr(str, "OK\r\n") != NULL)
        return AT_ACK_OK;
    if (strstr(str, "ERROR\r\n") != NULL)
        return AT_ACK_ERROR;
    if (strstr(str, "busy p") != NULL)
        return AT_ACK_BUSY;
    if (strstr(str, "ready") != NULL)
        return AT_ACK_READY;

    return AT_ACK_NONE;
}

static at_ack_t esp_at_usart_wait_receive(uint32_t timeout)
{
    bool acked = xSemaphoreTake(at_ack_sempahore, pdMS_TO_TICKS(timeout)) == pdPASS;
    return acked ? rxack : AT_ACK_NONE;
}

static bool esp_at_wait_pattern(const char *pattern, uint32_t timeout)
{
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout);

    while ((xTaskGetTickCount() - start_tick) < timeout_ticks)
    {
        if (strstr(rxbuf, pattern) != NULL)
            return true;

        if (strstr(rxbuf, "ERROR\r\n") != NULL || strstr(rxbuf, "busy p") != NULL)
            return false;

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    return false;
}

static void esp_at_copy_response(char *response, uint32_t response_size)
{
    uint32_t len;

    if (response == NULL || response_size == 0)
        return;

    taskENTER_CRITICAL();
    len = rxlen;
    if (len >= response_size)
        len = response_size - 1;
    memcpy(response, rxbuf, len);
    response[len] = '\0';
    taskEXIT_CRITICAL();
}

static bool esp_at_send_raw_wait(const char *command, const char *pattern, uint32_t timeout)
{
    bool ok;

    if (command == NULL || pattern == NULL)
        return false;

#if ESP_AT_DEBUG
    printf("[DEBUG] Send: %s", command);
#endif

    esp_at_ack_drain();
    esp_at_rx_reset();
    esp_at_usart_write(command);
    ok = esp_at_wait_pattern(pattern, timeout);

#if ESP_AT_DEBUG
    printf("[DEBUG] Response:\r\n%s\r\n", rxbuf);
#endif

    return ok;
}

static void esp_at_print_preview(const char *title, const char *data, uint32_t max_len)
{
    uint32_t i;

    if (title != NULL)
        printf("%s\r\n", title);

    if (data == NULL)
        return;

    for (i = 0; data[i] != '\0' && i < max_len; i++)
    {
        char c = data[i];
        if (c == '\r')
            printf("\\r");
        else if (c == '\n')
            printf("\\n\r\n");
        else
            printf("%c", c);
    }
    printf("\r\n");
}

static bool esp_at_wait_http_response(char *response, uint32_t response_size, uint32_t timeout)
{
    TickType_t start_tick = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout);
    TickType_t last_rx_tick = start_tick;
    uint32_t last_len = 0;
    bool has_http_data = false;

    printf("[WEATHER] wait HTTP response...\r\n");

    while ((xTaskGetTickCount() - start_tick) < timeout_ticks)
    {
        uint32_t current_len;

        taskENTER_CRITICAL();
        current_len = rxlen;
        taskEXIT_CRITICAL();

        if (current_len != last_len)
        {
            printf("[WEATHER] recv chunk len=%u\r\n", (unsigned int)(current_len - last_len));
            last_len = current_len;
            last_rx_tick = xTaskGetTickCount();
        }

        if (strstr(rxbuf, "+IPD") != NULL ||
            strstr(rxbuf, "HTTP/1.1") != NULL ||
            strchr(rxbuf, '{') != NULL)
        {
            has_http_data = true;
        }

        if (strstr(rxbuf, "CLOSED") != NULL)
        {
            esp_at_copy_response(response, response_size);
            printf("[WEATHER] HTTP response received, len=%u\r\n", (unsigned int)strlen(response));
            esp_at_print_preview("[WEATHER] HTTP response preview:", response, 300);
            return has_http_data;
        }

        if (has_http_data && (xTaskGetTickCount() - last_rx_tick) >= pdMS_TO_TICKS(1000))
        {
            esp_at_copy_response(response, response_size);
            printf("[WEATHER] HTTP response received, len=%u\r\n", (unsigned int)strlen(response));
            esp_at_print_preview("[WEATHER] HTTP response preview:", response, 300);
            return true;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    esp_at_copy_response(response, response_size);
    printf("[WEATHER] HTTP response timeout\r\n");
    printf("[WEATHER] partial response len=%u\r\n", (unsigned int)strlen(response));
    esp_at_print_preview("[WEATHER] partial response preview:", response, 300);
    return false;
}

static bool esp_at_parse_ipv4(const char *response, char *ip, uint32_t ip_size)
{
    const char *p;
    unsigned int a, b, c, d;

    if (response == NULL || ip == NULL || ip_size == 0)
        return false;

    for (p = response; *p != '\0'; p++)
    {
        if ((*p >= '0' && *p <= '9') &&
            sscanf(p, "%u.%u.%u.%u", &a, &b, &c, &d) == 4 &&
            a <= 255 && b <= 255 && c <= 255 && d <= 255)
        {
            snprintf(ip, ip_size, "%u.%u.%u.%u", a, b, c, d);
            return true;
        }
    }

    return false;
}

static bool esp_at_wait_boot(uint32_t timeout)
{
    (void)timeout;

    vTaskDelay(pdMS_TO_TICKS(ESP_AT_BOOT_WAIT_MS));

    for (int attempt = 0; attempt < ESP_AT_BOOT_RETRY_COUNT; attempt++)
    {
        at_ack_t ack = esp_at_execute_command("AT", 1000);
        if (ack == AT_ACK_OK)
            return true;

        vTaskDelay(pdMS_TO_TICKS(ESP_AT_BOOT_RETRY_MS));
    }

    return false;
}

bool esp_at_command(const char *command, uint32_t timeout)
{
    return esp_at_write_command(command, timeout);
}

bool esp_at_echo_off(void)
{
    return esp_at_write_command("ATE0", 2000);
}

bool esp_at_wifi_init(void)
{
    return esp_at_write_command("AT+CWMODE=1", 2000);
}

bool esp_at_connect_wifi(const char *ssid, const char *pwd, const char *mac)
{
    int len;

    if (ssid == NULL || pwd == NULL)
        return false;

    len = snprintf(txbuf, sizeof(txbuf), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
    if (mac != NULL)
        snprintf(txbuf + len, sizeof(txbuf) - len, ",\"%s\"", mac);

    return esp_at_write_command(txbuf, 20000);
}

static bool parse_cifsr_response(const char *response, char *ip, uint32_t ip_size)
{
    const char *staip = strstr(response, "+CIFSR:STAIP,");
    const char *start;
    const char *end;
    uint32_t len;

    if (staip == NULL || ip == NULL || ip_size == 0)
        return false;

    start = strchr(staip, '\"');
    if (start == NULL)
        return false;
    start++;

    end = strchr(start, '\"');
    if (end == NULL || end <= start)
        return false;

    len = (uint32_t)(end - start);
    if (len >= ip_size)
        len = ip_size - 1;

    memcpy(ip, start, len);
    ip[len] = '\0';
    return true;
}

bool esp_at_get_ip(char *ip, uint32_t ip_size)
{
    if (!esp_at_write_command("AT+CIFSR", 3000))
        return false;

    return parse_cifsr_response(esp_at_last_response(), ip, ip_size);
}

static bool parse_cwstate_response(const char *response, esp_wifi_info_t *info)
{
    response = strstr(response, "+CWSTATE:");
    if (response == NULL)
        return false;

    int wifi_state;
    if (sscanf(response, "+CWSTATE:%d,\"%63[^\"]", &wifi_state, info->ssid) != 2)
        return false;

    info->connected = (wifi_state == 2);

    return true;
}

static bool parse_cwjap_response(const char *response, esp_wifi_info_t *info)
{
    response = strstr(response, "+CWJAP:");
    if (response == NULL)
        return false;

    if (sscanf(response, "+CWJAP:\"%63[^\"]\",\"%17[^\"]\",%d,%d", info->ssid, info->bssid, &info->channel, &info->rssi) != 4)
        return false;

    return true;
}

bool esp_at_get_wifi_info(esp_wifi_info_t *info)
{
    if (!esp_at_write_command("AT+CWSTATE?", 2000))
        return false;

    if (!parse_cwstate_response(esp_at_last_response(), info))
        return false;

    if (info->connected == true)
    {
        if (!esp_at_write_command("AT+CWJAP?", 2000))
            return false;

        if (!parse_cwjap_response(esp_at_last_response(), info))
            return false;
    }

    return true;
}

bool wifi_is_connected(void)
{
    esp_wifi_info_t info;
    if (esp_at_get_wifi_info(&info))
        return info.connected;
    return false;
}

bool esp_at_sntp_config(void)
{
    if (!esp_at_write_command("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.ntp.org.cn\",\"pool.ntp.org\"", 3000))
        return false;

    return true;
}

bool esp_at_sntp_init(void)
{
    return esp_at_sntp_config();
}

static uint8_t month_str_to_num(const char *month_str)
{
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    for (uint8_t i = 0; i < 12; i++)
    {
        if (strcmp(month_str, months[i]) == 0)
            return i + 1;
    }

    return 0;
}

static uint8_t weekday_str_to_num(const char *weekday_str)
{
    const char *weekdays[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

    for (uint8_t i = 0; i < 7; i++)
    {
        if (strcmp(weekday_str, weekdays[i]) == 0)
            return i + 1;
    }

    return 0;
}

bool parse_sntp_time(const char *response, esp_date_time_t *date)
{
    char weekday_str[8];
    char month_str[4];

    if (response == NULL || date == NULL)
        return false;

    response = strstr(response, "+CIPSNTPTIME:");
    if (response == NULL)
        return false;

    if (sscanf(response, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %hu",
               weekday_str, month_str,
               &date->day, &date->hour, &date->minute, &date->second, &date->year) != 7)
        return false;

    date->weekday = weekday_str_to_num(weekday_str);
    date->month = month_str_to_num(month_str);

    return date->year >= 2000 && date->month >= 1 && date->month <= 12 &&
        date->day >= 1 && date->day <= 31 &&
        date->hour <= 23 && date->minute <= 59 && date->second <= 59;
}

bool esp_at_get_time(esp_date_time_t *date)
{
    if (!esp_at_write_command("AT+CIPSNTPTIME?", 3000))
        return false;

    if (!parse_sntp_time(esp_at_last_response(), date))
        return false;

    return true;
}

bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    return esp_at_get_time(date);
}

const char *esp_at_http_get(const char *url)
{
    snprintf(txbuf, sizeof(txbuf), "AT+HTTPCLIENT=2,1,\"%s\",,,2", url);
    if (!esp_at_write_command(txbuf, 5000))
        return NULL;

    return esp_at_last_response();
}

bool esp_at_http_get_transport(const char *transport, const char *host, uint16_t port,
                               const char *request, char *response,
                               uint32_t response_size, uint32_t timeout_ms)
{
    int request_len;
    char dns_ip[16] = { 0 };
    bool pre_close_ok;
    bool dns_ok;

    if (transport == NULL || host == NULL || request == NULL || response == NULL || response_size == 0)
        return false;

    response[0] = '\0';

    pre_close_ok = esp_at_command("AT+CIPCLOSE", 2000);
    printf("[WEATHER] pre-close %s\r\n", pre_close_ok ? "OK" : "ignored");

    if (esp_at_command("AT+CIPMUX=0", 3000))
        printf("[WEATHER] CIPMUX=0 OK\r\n");
    else
        printf("[WEATHER] CIPMUX=0 FAIL\r\n");

    if (esp_at_command("AT+CIPMODE=0", 3000))
        printf("[WEATHER] CIPMODE=0 OK\r\n");
    else
        printf("[WEATHER] CIPMODE=0 FAIL\r\n");

    if (esp_at_command("AT+CIPRECVMODE=0", 3000))
        printf("[WEATHER] CIPRECVMODE=0 OK\r\n");
    else
        printf("[WEATHER] CIPRECVMODE=0 FAIL\r\n");

    snprintf(txbuf, sizeof(txbuf), "AT+CIPDOMAIN=\"%s\"", host);
    dns_ok = esp_at_command(txbuf, 5000);
    if (dns_ok && esp_at_parse_ipv4(esp_at_last_response(), dns_ip, sizeof(dns_ip)))
        printf("[WEATHER] DNS OK: %s\r\n", dns_ip);
    else
        printf("[WEATHER] DNS FAIL\r\n");

    if (strcmp(transport, "SSL") == 0)
    {
        snprintf(txbuf, sizeof(txbuf), "AT+CIPSSLCSNI=1,\"%s\"", host);
        if (esp_at_command(txbuf, 3000))
            printf("[WEATHER] SSL SNI OK\r\n");
        else
            printf("[WEATHER] SSL SNI FAIL\r\n");
    }

    printf("[WEATHER] TCP connect start\r\n");
    snprintf(txbuf, sizeof(txbuf), "AT+CIPSTART=\"%s\",\"%s\",%u\r\n", transport, host, port);
    if (!esp_at_send_raw_wait(txbuf, "CONNECT", 10000) &&
        strstr(esp_at_last_response(), "ALREADY CONNECTED") == NULL)
    {
        esp_at_copy_response(response, response_size);
        printf("[WEATHER] TCP connect FAIL\r\n");
        printf("[WEATHER] CIPSTART response: %s\r\n", esp_at_last_response());
        return false;
    }
    printf("[WEATHER] TCP connect OK\r\n");

    request_len = strlen(request);
    snprintf(txbuf, sizeof(txbuf), "AT+CIPSEND=%d\r\n", request_len);
    if (!esp_at_send_raw_wait(txbuf, ">", 3000))
    {
        esp_at_copy_response(response, response_size);
        printf("[WEATHER] CIPSEND prompt FAIL\r\n");
        esp_at_command("AT+CIPCLOSE", 2000);
        return false;
    }
    printf("[WEATHER] CIPSEND OK\r\n");

    esp_at_ack_drain();
    esp_at_rx_reset();
    printf("[DEBUG] Send: HTTP request, len=%d\r\n", request_len);
    esp_at_send_raw(request, (uint32_t)request_len);

    if (!esp_at_wait_pattern("SEND OK", 5000))
    {
        esp_at_copy_response(response, response_size);
        printf("[WEATHER] raw send FAIL\r\n");
        printf("[WEATHER] raw send response: %s\r\n", response);
        if (esp_at_command("AT+CIPCLOSE", 2000))
            printf("[WEATHER] close OK\r\n");
        else
            printf("[WEATHER] close ignored\r\n");
        return false;
    }
    printf("[WEATHER] raw send OK\r\n");

    if (!esp_at_wait_http_response(response, response_size, timeout_ms))
    {
        if (esp_at_command("AT+CIPCLOSE", 2000))
            printf("[WEATHER] close OK\r\n");
        else
            printf("[WEATHER] close ignored\r\n");
        return false;
    }

    if (esp_at_command("AT+CIPCLOSE", 2000))
        printf("[WEATHER] close OK\r\n");
    else
        printf("[WEATHER] close ignored\r\n");
    return true;
}

bool esp_at_tcp_http_get(const char *host, uint16_t port, const char *request,
                         char *response, uint32_t response_size, uint32_t timeout_ms)
{
    return esp_at_http_get_transport("TCP", host, port, request,
                                     response, response_size, timeout_ms);
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        if (rxlen < sizeof(rxbuf) - 1)
        {
            rxbuf[rxlen++] = (char)USART_ReceiveData(USART2);
            rxbuf[rxlen] = '\0';
            if (rxbuf[rxlen - 1] == '\n')
            {
                rxbuf[rxlen] = '\0';
                at_ack_t ack = match_internal_ack(rxline);
                if (ack != AT_ACK_NONE)
                {
                    BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
                    rxack = ack;
                    xSemaphoreGiveFromISR(at_ack_sempahore, &pxHigherPriorityTaskWoken);
                    portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
                }
                rxline = rxbuf + rxlen;
            }
        }

        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}
