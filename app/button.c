#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx.h"
#include "button.h"

#define BUTTON_DEBOUNCE_MS    30

static bool stable_state;
static bool last_sample;
static TickType_t last_change_tick;

void button_init(void)
{
    GPIO_InitTypeDef gpio;

    GPIO_StructInit(&gpio);
    gpio.GPIO_Mode = GPIO_Mode_IN;
    gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
    gpio.GPIO_Speed = GPIO_Medium_Speed;
    gpio.GPIO_Pin = GPIO_Pin_0;
    GPIO_Init(GPIOA, &gpio);

    stable_state = false;
    last_sample = false;
    last_change_tick = xTaskGetTickCount();
}

bool button_poll_short_press(void)
{
    bool sample = GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_0) == Bit_SET;
    TickType_t now = xTaskGetTickCount();

    if (sample != last_sample)
    {
        last_sample = sample;
        last_change_tick = now;
        return false;
    }

    if ((now - last_change_tick) < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS))
        return false;

    if (sample != stable_state)
    {
        stable_state = sample;
        return stable_state;
    }

    return false;
}
