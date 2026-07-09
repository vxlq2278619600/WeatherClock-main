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
static char rxbuf[1024];
static char txbuf[1024];
static uint32_t rxlen;
static at_ack_t rxack;
static SemaphoreHandle_t at_ack_sempahore;

static at_ack_t esp_at_execute_command(const char *command, uint32_t timeout);
static bool esp_at_write_command(const char *command, uint32_t timeout);
static bool esp_at_wait_boot(uint32_t timeout);
static void esp_at_usart_write(const char *data);
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

static void esp_at_usart_write(const char *data)
{
    uint32_t len = strlen(data);

    DMA1_Stream6->M0AR = (uint32_t)data;
    DMA1_Stream6->NDTR = len;

    DMA_ClearFlag(DMA1_Stream6, DMA_FLAG_TCIF6);
    DMA_Cmd(DMA1_Stream6, ENABLE);
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
    rxlen = 0;
    rxline = rxbuf;

    bool acked = xSemaphoreTake(at_ack_sempahore, pdMS_TO_TICKS(timeout)) == pdPASS;
    return acked ? rxack : AT_ACK_NONE;
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

bool esp_at_sntp_init(void)
{
    if (!esp_at_write_command("AT+CIPSNTPCFG=1,8", 2000))
        return false;

    return true;
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

static bool parse_cipsntptime_response(const char *response, esp_date_time_t *date)
{
    char weekday_str[8];
    char month_str[4];

    response = strstr(response, "+CIPSNTPTIME:");
    if (response == NULL)
        return false;

    if (sscanf(response, "+CIPSNTPTIME:%3s %3s %hhu %hhu:%hhu:%hhu %hu",
               weekday_str, month_str,
               &date->day, &date->hour, &date->minute, &date->second, &date->year) != 7)
        return false;

    date->weekday = weekday_str_to_num(weekday_str);
    date->month = month_str_to_num(month_str);

    return true;
}

bool esp_at_sntp_get_time(esp_date_time_t *date)
{
    if (!esp_at_write_command("AT+CIPSNTPTIME?", 2000))
        return false;

    if (!parse_cipsntptime_response(esp_at_last_response(), date))
        return false;

    return true;
}

const char *esp_at_http_get(const char *url)
{
    snprintf(txbuf, sizeof(txbuf), "AT+HTTPCLIENT=2,1,\"%s\",,,2", url);
    if (!esp_at_write_command(txbuf, 5000))
        return NULL;

    return esp_at_last_response();
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) == SET)
    {
        if (rxlen < sizeof(rxbuf) - 1)
        {
            rxbuf[rxlen++] = (char)USART_ReceiveData(USART2);
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
