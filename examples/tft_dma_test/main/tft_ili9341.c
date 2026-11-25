/* tft_dma_test — draw color bars, a checkerboard and text on a 240x320
 * ILI9341-class SPI TFT (CYD-style wiring: SCLK=GPIO14, MOSI=GPIO13,
 * CS=GPIO15, DC=GPIO2, on the HSPI / SPI2 bus) using the esp-idf
 * spi_master driver. Exercises the espplay TFT model: ILI9341 commands
 * 0x2A/0x2B/0x2C/0x36/0x3A + RGB565 pixel streams. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "font8x8_basic.h"

#define TFT_HOST    SPI2_HOST
#define PIN_SCLK   14
#define PIN_MOSI   13
#define PIN_CS     15
#define PIN_DC      2
#define TFT_W     240
#define TFT_H     320

static spi_device_handle_t spi;

static void tft_cmd(uint8_t c)
{
    gpio_set_level(PIN_DC, 0);
    spi_transaction_t t = {
        .length = 8, .tx_buffer = &c,
    };
    spi_device_polling_transmit(spi, &t);
}

static void tft_data(const uint8_t *data, int len)
{
    gpio_set_level(PIN_DC, 1);
    while (len > 0) {
        int n = len > 1024 ? 1024 : len;
        spi_transaction_t t = {
            .length = n * 8, .tx_buffer = data,
        };
        spi_device_polling_transmit(spi, &t);
        data += n;
        len -= n;
    }
}

static void tft_data16(const uint16_t *data, int nwords)
{
    gpio_set_level(PIN_DC, 1);
    /* RGB565 goes out big-endian over SPI */
    uint8_t chunk[512];
    while (nwords > 0) {
        int n = nwords > 256 ? 256 : nwords;
        for (int i = 0; i < n; i++) {
            chunk[2 * i] = data[i] >> 8;
            chunk[2 * i + 1] = data[i] & 0xFF;
        }
        spi_transaction_t t = {
            .length = n * 16, .tx_buffer = chunk,
        };
        spi_device_polling_transmit(spi, &t);
        data += n;
        nwords -= n;
    }
}

static void tft_window(int x0, int y0, int x1, int y1)
{
    uint8_t b[4];
    tft_cmd(0x2A);
    b[0] = x0 >> 8; b[1] = x0 & 0xFF; b[2] = x1 >> 8; b[3] = x1 & 0xFF;
    tft_data(b, 4);
    tft_cmd(0x2B);
    b[0] = y0 >> 8; b[1] = y0 & 0xFF; b[2] = y1 >> 8; b[3] = y1 & 0xFF;
    tft_data(b, 4);
}

static void tft_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    tft_window(x, y, x + w - 1, y + h - 1);
    tft_cmd(0x2C);
    uint16_t line[256];
    int n = w > 256 ? 256 : w;
    for (int i = 0; i < n; i++) {
        line[i] = color;
    }
    for (int row = 0; row < h; row++) {
        int left = w;
        while (left > 0) {
            int c = left > n ? n : left;
            tft_data16(line, c);
            left -= c;
        }
    }
}

static void tft_text(int x, int y, int scale, const char *s, uint16_t fg)
{
    for (int i = 0; s[i]; i++) {
        const uint8_t *glyph = font8x8_basic_tr[(uint8_t)s[i]];
        for (int col = 0; col < 8; col++) {
            uint8_t bits = glyph[col];
            for (int row = 0; row < 8; row++) {
                if ((bits >> row) & 1) {
                    tft_fill_rect(x + (i * 8 + col) * scale, y + row * scale,
                                  scale, scale, fg);
                }
            }
        }
    }
}

static void tft_init(void)
{
    gpio_config_t dc = {
        .pin_bit_mask = 1ULL << PIN_DC,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&dc);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    /* Exercise ESP32 SPI DMA channel 1 and its linked descriptors. */
    spi_bus_initialize(TFT_HOST, &bus, 1);

    spi_device_interface_config_t dev = {
        .clock_speed_hz = 26 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 4,
    };
    spi_bus_add_device(TFT_HOST, &dev, &spi);

    tft_cmd(0x01); /* SWRESET */
    vTaskDelay(pdMS_TO_TICKS(120));
    tft_cmd(0x11); /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));
    tft_cmd(0x3A); uint8_t c55 = 0x55; tft_data(&c55, 1); /* COLMOD RGB565 */
    tft_cmd(0x36); uint8_t mad = 0x00; tft_data(&mad, 1); /* MADCTL portrait */
    tft_cmd(0x29); /* DISPON */
}

#define RGB565(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)))

void app_main(void)
{
    printf("tft_dma_test: init ILI9341 on SPI2 (SCLK=%d MOSI=%d CS=%d DC=%d)\n",
           PIN_SCLK, PIN_MOSI, PIN_CS, PIN_DC);
    tft_init();

    /* 8 vertical color bars */
    static const uint16_t bars[8] = {
        RGB565(255, 255, 255), RGB565(255, 255, 0), RGB565(0, 255, 255),
        RGB565(0, 255, 0), RGB565(255, 0, 255), RGB565(255, 0, 0),
        RGB565(0, 0, 255), RGB565(16, 16, 16),
    };
    for (int i = 0; i < 8; i++) {
        tft_fill_rect(i * 30, 0, 30, TFT_H, bars[i]);
    }

    /* checkerboard band */
    for (int y = 60; y < 124; y += 8) {
        for (int x = 0; x < TFT_W; x += 8) {
            tft_fill_rect(x, y, 8, 8, ((x + y) / 8) & 1 ? RGB565(255, 255, 255)
                                                        : RGB565(0, 0, 0));
        }
    }

    /* text banner */
    tft_fill_rect(0, 140, TFT_W, 64, RGB565(0, 0, 128));
    tft_text(24, 148, 2, "HELLO TFT", RGB565(255, 255, 0));
    tft_text(24, 172, 1, "ESP32 Playground ILI9341", RGB565(255, 255, 255));

    /* border + bottom label */
    tft_fill_rect(0, 230, TFT_W, 90, RGB565(32, 32, 32));
    tft_text(36, 250, 2, "TFT VIDEO", RGB565(0, 255, 128));
    tft_text(20, 280, 1, "240x320 RGB565 via SPI2 model", RGB565(180, 180, 255));

    printf("tft_dma_test: draw complete\n");
    int count = 0;
    while (1) {
        printf("tft_dma_test heartbeat %d\n", count++);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
