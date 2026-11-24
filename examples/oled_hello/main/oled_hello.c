/* oled_hello — show a scrolling message on an SSD1306 OLED (I2C, 0x3C,
 * SDA=GPIO21, SCL=GPIO22) using an embedded 8x8 font. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "font8x8_basic.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA 21
#define I2C_SCL 22
#define OLED_ADDR 0x3C

static void oled_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

static void oled_data(const uint8_t *data, int len)
{
    uint8_t buf[1 + 16];
    buf[0] = 0x40;
    memcpy(buf + 1, data, len);
    i2c_master_write_to_device(I2C_PORT, OLED_ADDR, buf, 1 + len,
                               pdMS_TO_TICKS(100));
}

static void oled_init(void)
{
    oled_cmd(0xAE); /* display off */
    oled_cmd(0xD5); oled_cmd(0x80);
    oled_cmd(0xA8); oled_cmd(0x3F);
    oled_cmd(0xD3); oled_cmd(0x00);
    oled_cmd(0x40);
    oled_cmd(0x8D); oled_cmd(0x14); /* charge pump on */
    oled_cmd(0x20); oled_cmd(0x00); /* horizontal addressing */
    oled_cmd(0xA1);
    oled_cmd(0xC8);
    oled_cmd(0xDA); oled_cmd(0x12);
    oled_cmd(0x81); oled_cmd(0x7F);
    oled_cmd(0xD9); oled_cmd(0xF1);
    oled_cmd(0xDB); oled_cmd(0x40);
    oled_cmd(0xA4);
    oled_cmd(0xA6);
    oled_cmd(0xAF); /* display on */
}

static void oled_clear(void)
{
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127);
    oled_cmd(0x22); oled_cmd(0); oled_cmd(7);
    uint8_t z[16] = { 0 };
    for (int i = 0; i < 64; i++) {
        oled_data(z, 16);
    }
}

static void oled_text(int page, const char *text)
{
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127);
    oled_cmd(0x22); oled_cmd(page); oled_cmd(page);
    for (int i = 0; i < 16 && text[i]; i++) {
        oled_data(font8x8_basic_tr[(uint8_t)text[i]], 8);
    }
}

void app_main(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(I2C_PORT, &cfg);
    i2c_driver_install(I2C_PORT, cfg.mode, 0, 0, 0);
    oled_init();
    oled_clear();
    oled_text(2, "Hello, ESP32");
    oled_text(4, "Playground!");
    printf("oled_hello ready\n");
    int count = 0;
    char line[17];
    while (1) {
        snprintf(line, sizeof(line), "count: %6d", count++);
        oled_text(6, line);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
