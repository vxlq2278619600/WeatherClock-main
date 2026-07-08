#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "ui.h"

extern void board_lowlevel_init(void);
extern void board_init(void);

static void main_init(void *param)
{
    (void)param;

    board_init();
    ui_init();

    printf("[LCD_TEST] start\r\n");

    ui_fill_color(0, 0, UI_WIDTH - 1, UI_HEIGHT - 1, mkcolor(0, 0, 0));
    ui_fill_color(20, 20, 69, 89, mkcolor(255, 0, 0));
    ui_fill_color(75, 20, 124, 89, mkcolor(0, 255, 0));
    ui_fill_color(130, 20, 179, 89, mkcolor(0, 0, 255));
    ui_fill_color(185, 20, 234, 89, mkcolor(255, 255, 255));

    ui_write_string(20, 120, "ST7789 OK", mkcolor(255, 255, 255), mkcolor(0, 0, 0), &font32_maple_bold);
    ui_write_string(20, 170, "SPI LCD TEST", mkcolor(0, 255, 234), mkcolor(0, 0, 0), &font24_maple_bold);
    ui_write_string(20, 220, "If you see this, LCD works", mkcolor(255, 255, 0), mkcolor(0, 0, 0), &font16_maple);

    printf("[LCD_TEST] pattern sent\r\n");

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
