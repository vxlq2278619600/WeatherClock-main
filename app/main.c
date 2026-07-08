#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "workqueue.h"
#include "app.h"
#include "ui.h"
#include "wifi.h"
#include "page.h"

#define ENABLE_LCD_SELF_TEST 1
#define LCD_TEST_PAGE_DELAY_MS 1000

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

    /* Rectangle border */
    ui_fill_color(10, 10, 229, 13, 0xFFFF);
    ui_fill_color(10, 306, 229, 309, 0xFFFF);
    ui_fill_color(10, 10, 13, 309, 0xFFFF);
    ui_fill_color(226, 10, 229, 309, 0xFFFF);

    /* Horizontal and vertical guide lines */
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

static void main_init(void *param)
{
    (void)param;

    board_init();
    ui_init();

#if ENABLE_LCD_SELF_TEST
    ST7789_DisplaySelfTest();
#endif

    welcome_page_display();

    wifi_init();
    wifi_page_display();
    wifi_wait_connect();

    main_page_display();
    app_init();

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
