/* neopixel_rainbow — animated rainbow on 8 WS2812 (NeoPixel) LEDs on GPIO5
 * using the RMT peripheral. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt.h"

#define PIXEL_GPIO 5
#define NUM_PIXELS 8
#define RMT_CHANNEL RMT_CHANNEL_0

/* RMT source clock is 80 MHz APB; clk_div=2 -> 40 MHz tick = 25 ns. */
#define NS_TO_TICKS(ns) ((ns) / 25)
#define T0H 400  /* ns */
#define T1H 800
#define T1L 850
#define T0L 850

static rmt_item32_t to_rmt(uint8_t byte, rmt_item32_t *items)
{
    for (int i = 7; i >= 0; i--) {
        int bit = (byte >> i) & 1;
        items[7 - i].level0 = 1;
        items[7 - i].duration0 = NS_TO_TICKS(bit ? T1H : T0H);
        items[7 - i].level1 = 0;
        items[7 - i].duration1 = NS_TO_TICKS(bit ? T1L : T0L);
    }
    return items[0];
}

static void pixels_show(const uint8_t grb[NUM_PIXELS][3])
{
    rmt_item32_t items[NUM_PIXELS * 24 + 1];
    for (int p = 0; p < NUM_PIXELS; p++) {
        to_rmt(grb[p][0], &items[p * 24 + 0]);
        to_rmt(grb[p][1], &items[p * 24 + 8]);
        to_rmt(grb[p][2], &items[p * 24 + 16]);
    }
    items[NUM_PIXELS * 24].level0 = 0;
    items[NUM_PIXELS * 24].duration0 = 2000; /* 50 us at 25 ns tick */
    items[NUM_PIXELS * 24].level1 = 0;
    items[NUM_PIXELS * 24].duration1 = 0;
    rmt_write_items(RMT_CHANNEL, items, NUM_PIXELS * 24 + 1, true);
}

static void wheel(int pos, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (pos < 85) {
        *r = 255 - pos * 3;
        *g = pos * 3;
        *b = 0;
    } else if (pos < 170) {
        pos -= 85;
        *r = 0;
        *g = 255 - pos * 3;
        *b = pos * 3;
    } else {
        pos -= 170;
        *r = pos * 3;
        *g = 0;
        *b = 255 - pos * 3;
    }
}

void app_main(void)
{
    rmt_config_t cfg = RMT_DEFAULT_CONFIG_TX(PIXEL_GPIO, RMT_CHANNEL);
    cfg.clk_div = 2;
    rmt_config(&cfg);
    rmt_driver_install(cfg.channel, 0, 0);
    printf("neopixel_rainbow ready\n");
    uint8_t pixels[NUM_PIXELS][3];
    int offset = 0;
    while (1) {
        for (int i = 0; i < NUM_PIXELS; i++) {
            uint8_t r, g, b;
            wheel((i * 256 / NUM_PIXELS + offset) & 0xFF, &r, &g, &b);
            pixels[i][0] = g;
            pixels[i][1] = r;
            pixels[i][2] = b;
        }
        pixels_show(pixels);
        offset = (offset + 4) & 0xFF;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
