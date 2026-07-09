#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "tim_delay.h"
#include "aht20.h"

#define AHT20_I2C                 I2C1
#define AHT20_ADDR_7BIT           0x38
#define AHT20_ADDR_8BIT           (AHT20_ADDR_7BIT << 1)
#define AHT20_CMD_INIT            0xBE
#define AHT20_CMD_TRIGGER         0xAC
#define AHT20_CMD_SOFT_RESET      0xBA
#define AHT20_CMD_STATUS          0x71
#define AHT20_STATUS_BUSY         0x80
#define AHT20_STATUS_CALIBRATED   0x08
#define AHT20_I2C_TIMEOUT_US      3000
#define AHT20_READ_RETRY          3

static int aht20_write_bytes(const uint8_t data[], uint32_t length);
static int aht20_read_bytes(uint8_t data[], uint32_t length);
static int aht20_read_status(uint8_t *status);
static int aht20_send_init_command(void);
static int aht20_trigger_measurement(void);
static int aht20_parse_measurement(const uint8_t data[6], aht20_data_t *out);

#define I2C_WAIT_EVENT(EVENT, TIMEOUT_US) \
    do { \
        uint32_t timeout = (TIMEOUT_US); \
        while (!I2C_CheckEvent(AHT20_I2C, (EVENT)) && timeout > 0) { \
            tim_delay_us(10); \
            timeout -= 10; \
        } \
        if (timeout == 0) { \
            I2C_GenerateSTOP(AHT20_I2C, ENABLE); \
            return -1; \
        } \
    } while (0)

static void aht20_i2c_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_AF;
    gpio.GPIO_OType = GPIO_OType_OD;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed = GPIO_High_Speed;
    gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
    GPIO_Init(GPIOB, &gpio);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_I2C1);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_I2C1);
}

static void aht20_i2c_init(void)
{
    I2C_InitTypeDef i2c;

    I2C_DeInit(AHT20_I2C);
    I2C_StructInit(&i2c);
    i2c.I2C_Ack = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2c.I2C_ClockSpeed = 100ul * 1000ul;
    i2c.I2C_DutyCycle = I2C_DutyCycle_2;
    i2c.I2C_Mode = I2C_Mode_I2C;
    i2c.I2C_OwnAddress1 = 0x00;
    I2C_Init(AHT20_I2C, &i2c);
    I2C_Cmd(AHT20_I2C, ENABLE);
}

int aht20_soft_reset(void)
{
    uint8_t cmd = AHT20_CMD_SOFT_RESET;
    int ret = aht20_write_bytes(&cmd, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    return ret == 0 ? 1 : 0;
}

int aht20_init(void)
{
    uint8_t status = 0;

    aht20_i2c_gpio_init();
    aht20_i2c_init();

    vTaskDelay(pdMS_TO_TICKS(40));

    if (aht20_read_status(&status) == 0 && (status & AHT20_STATUS_CALIBRATED) != 0)
        return 1;

    printf("[AHT20] not calibrated, try init command\r\n");
    if (aht20_send_init_command() != 0)
        return 0;

    vTaskDelay(pdMS_TO_TICKS(10));

    if (aht20_read_status(&status) != 0)
        return 0;

    return (status & AHT20_STATUS_CALIBRATED) != 0 ? 1 : 0;
}

int aht20_read(aht20_data_t *out)
{
    uint8_t data[6];

    if (out == NULL)
        return -1;

    out->valid = 0;

    for (uint32_t retry = 0; retry < AHT20_READ_RETRY; retry++)
    {
        if (aht20_trigger_measurement() != 0)
            return -2;

        vTaskDelay(pdMS_TO_TICKS(80));

        if (aht20_read_bytes(data, sizeof(data)) != 0)
            return -3;

        if ((data[0] & AHT20_STATUS_BUSY) != 0)
        {
            printf("[AHT20] busy\r\n");
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if ((data[0] & AHT20_STATUS_CALIBRATED) == 0)
        {
            printf("[AHT20] not calibrated, try init command\r\n");
            if (aht20_send_init_command() != 0)
                return -4;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        return aht20_parse_measurement(data, out);
    }

    return -5;
}

static int aht20_send_init_command(void)
{
    uint8_t cmd[] = { AHT20_CMD_INIT, 0x08, 0x00 };
    return aht20_write_bytes(cmd, sizeof(cmd));
}

static int aht20_trigger_measurement(void)
{
    uint8_t cmd[] = { AHT20_CMD_TRIGGER, 0x33, 0x00 };
    return aht20_write_bytes(cmd, sizeof(cmd));
}

static int aht20_write_bytes(const uint8_t data[], uint32_t length)
{
    if (data == NULL || length == 0)
        return -1;

    I2C_AcknowledgeConfig(AHT20_I2C, ENABLE);
    I2C_GenerateSTART(AHT20_I2C, ENABLE);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_MODE_SELECT, AHT20_I2C_TIMEOUT_US);
    I2C_Send7bitAddress(AHT20_I2C, AHT20_ADDR_8BIT, I2C_Direction_Transmitter);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED, AHT20_I2C_TIMEOUT_US);

    for (uint32_t i = 0; i < length; i++)
    {
        I2C_SendData(AHT20_I2C, data[i]);
        I2C_WAIT_EVENT(I2C_EVENT_MASTER_BYTE_TRANSMITTED, AHT20_I2C_TIMEOUT_US);
    }

    I2C_GenerateSTOP(AHT20_I2C, ENABLE);
    return 0;
}

static int aht20_read_bytes(uint8_t data[], uint32_t length)
{
    if (data == NULL || length == 0)
        return -1;

    I2C_AcknowledgeConfig(AHT20_I2C, ENABLE);
    I2C_GenerateSTART(AHT20_I2C, ENABLE);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_MODE_SELECT, AHT20_I2C_TIMEOUT_US);
    I2C_Send7bitAddress(AHT20_I2C, AHT20_ADDR_8BIT, I2C_Direction_Receiver);
    I2C_WAIT_EVENT(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED, AHT20_I2C_TIMEOUT_US);

    for (uint32_t i = 0; i < length; i++)
    {
        if (i == length - 1)
            I2C_AcknowledgeConfig(AHT20_I2C, DISABLE);

        I2C_WAIT_EVENT(I2C_EVENT_MASTER_BYTE_RECEIVED, AHT20_I2C_TIMEOUT_US);
        data[i] = I2C_ReceiveData(AHT20_I2C);
    }

    I2C_GenerateSTOP(AHT20_I2C, ENABLE);
    I2C_AcknowledgeConfig(AHT20_I2C, ENABLE);
    return 0;
}

static int aht20_read_status(uint8_t *status)
{
    uint8_t cmd = AHT20_CMD_STATUS;

    if (status == NULL)
        return -1;

    if (aht20_write_bytes(&cmd, 1) != 0)
        return -2;

    return aht20_read_bytes(status, 1);
}

static int aht20_parse_measurement(const uint8_t data[6], aht20_data_t *out)
{
    uint32_t raw_humidity;
    uint32_t raw_temperature;

    raw_humidity = ((uint32_t)data[1] << 12) |
                   ((uint32_t)data[2] << 4) |
                   ((uint32_t)data[3] >> 4);

    raw_temperature = (((uint32_t)data[3] & 0x0F) << 16) |
                      ((uint32_t)data[4] << 8) |
                      ((uint32_t)data[5]);

    out->humidity = raw_humidity * 100.0f / 1048576.0f;
    out->temperature = raw_temperature * 200.0f / 1048576.0f - 50.0f;
    out->valid = 1;
    return 1;
}

bool aht20_start_measurement(void)
{
    return aht20_trigger_measurement() == 0;
}

bool aht20_wait_for_measurement(void)
{
    uint8_t status = 0;

    for (uint32_t retry = 0; retry < AHT20_READ_RETRY; retry++)
    {
        vTaskDelay(pdMS_TO_TICKS(30));
        if (aht20_read_status(&status) == 0 && (status & AHT20_STATUS_BUSY) == 0)
            return true;
    }

    printf("[AHT20] busy\r\n");
    return false;
}

bool aht20_read_measurement(float *temperature, float *humidity)
{
    uint8_t data[6];
    aht20_data_t out = { 0 };

    if (temperature == NULL || humidity == NULL)
        return false;

    if (aht20_read_bytes(data, sizeof(data)) != 0)
        return false;

    if ((data[0] & AHT20_STATUS_BUSY) != 0)
    {
        printf("[AHT20] busy\r\n");
        return false;
    }

    if (aht20_parse_measurement(data, &out) <= 0 || out.valid == 0)
        return false;

    *temperature = out.temperature;
    *humidity = out.humidity;
    return true;
}
