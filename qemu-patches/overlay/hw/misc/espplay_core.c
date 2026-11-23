/*
 * ESP32 Playground - core state, virtual sensor/I2C slave models
 *
 * Copyright (c) 2026 ESP32 Playground contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/misc/espplay_internal.h"
#if defined(CONFIG_TARGET_XTENSA)
#include "target/xtensa/cpu.h"
#elif defined(CONFIG_TARGET_RISCV32)
#include "target/riscv/cpu.h"
#endif

void espplay_send_display_frame(const uint8_t *meta, const uint8_t *fb, int len);
void espplay_send_lcd_frame(const uint8_t *meta, const uint8_t *lines, int len);

/* --------------------------------------------------------- hook table */

EspplayHook espplay_hooks[] = {
    { "esp_wifi_init", espplay_hook_wifi_fn },
    { "esp_wifi_deinit", espplay_hook_wifi_fn },
    { "esp_wifi_set_mode", espplay_hook_wifi_fn },
    { "esp_wifi_get_mode", espplay_hook_wifi_fn },
    { "esp_wifi_set_config", espplay_hook_wifi_fn },
    { "esp_wifi_get_config", espplay_hook_wifi_fn },
    { "esp_wifi_start", espplay_hook_wifi_fn },
    { "esp_wifi_stop", espplay_hook_wifi_fn },
    { "esp_wifi_connect", espplay_hook_wifi_fn },
    { "esp_wifi_disconnect", espplay_hook_wifi_fn },
    { "esp_wifi_scan_start", espplay_hook_wifi_fn },
    { "esp_wifi_scan_stop", espplay_hook_wifi_fn },
    { "esp_wifi_scan_get_ap_num", espplay_hook_wifi_fn },
    { "esp_wifi_scan_get_ap_records", espplay_hook_wifi_fn },
    { "esp_wifi_scan_get_ap_info", espplay_hook_wifi_fn },
    { "esp_wifi_sta_get_ap_info", espplay_hook_wifi_fn },
    { "esp_wifi_get_ip_info", espplay_hook_wifi_fn },
    { "esp_wifi_set_ps", espplay_hook_wifi_fn },
    { "esp_wifi_set_max_tx_power", espplay_hook_wifi_fn },
    { "esp_wifi_set_channel", espplay_hook_wifi_fn },
    { "esp_wifi_get_mac", espplay_hook_wifi_fn },
    { "esp_wifi_set_mac", espplay_hook_wifi_fn },
    { "esp_wifi_internal_tx", espplay_hook_wifi_fn },
    { "esp_wifi_internal_reg_rxcb", espplay_hook_wifi_fn },
    { "esp_wifi_internal_free_rx_buffer", espplay_hook_wifi_fn },
    { "esp_wifi_set_log_level", espplay_hook_wifi_fn },
    { "esp_wifi_internal_set_log_level", espplay_hook_wifi_fn },
    { "esp_wifi_ap_get_sta_list", espplay_hook_wifi_fn },

    { "esp_vhci_host_register_callback", espplay_hook_ble_fn },
    { "esp_vhci_host_send_packet", espplay_hook_ble_fn },
    { "esp_vhci_host_check_send_available", espplay_hook_ble_fn },
    { "esp_bt_controller_init", espplay_hook_ble_fn },
    { "esp_bt_controller_enable", espplay_hook_ble_fn },
    { "esp_bt_controller_disable", espplay_hook_ble_fn },
    { "esp_bt_controller_deinit", espplay_hook_ble_fn },
    { "esp_bt_controller_mem_release", espplay_hook_ble_fn },
    { "esp_bt_controller_mem_acquire", espplay_hook_ble_fn },
    { "esp_bt_controller_get_status", espplay_hook_ble_fn },
};

const int espplay_hooks_count =
    (int)(sizeof(espplay_hooks) / sizeof(espplay_hooks[0]));

/* ------------------------------------------------------ gpio input reg */

void espplay_gpio_input_changed(EspplayBridgeState *s, int pin)
{
    /* Polled via espplay_gpio_input_val/mask in the GPIO model; a change
     * notification hook for interrupt-driven GPIO is registered by the
     * GPIO model through espplay_gpio_register() when needed. */
    (void)s;
    (void)pin;
}

/* -------------------------------------------------------- i2c plumbing */

typedef struct {
    int bus, addr;
    const struct EspplayI2cSlaveOps *ops;
    void *opaque;
    bool used;
} EspplayI2cSlave;

static EspplayI2cSlave i2c_slaves[16];

void espplay_i2c_register_slave(int bus, int addr,
                                const struct EspplayI2cSlaveOps *ops,
                                void *opaque)
{
    for (int i = 0; i < 16; i++) {
        if (!i2c_slaves[i].used) {
            i2c_slaves[i] = (EspplayI2cSlave){ bus, addr, ops, opaque, true };
            return;
        }
    }
}

void espplay_i2c_unregister_slave(int bus, int addr)
{
    for (int i = 0; i < 16; i++) {
        if (i2c_slaves[i].used && i2c_slaves[i].bus == bus &&
            i2c_slaves[i].addr == addr) {
            i2c_slaves[i].used = false;
        }
    }
}

int espplay_i2c_slave_write(int bus, int addr, const uint8_t *buf, int len)
{
    for (int i = 0; i < 16; i++) {
        if (i2c_slaves[i].used && i2c_slaves[i].bus == bus &&
            i2c_slaves[i].addr == addr) {
            return i2c_slaves[i].ops->write(i2c_slaves[i].opaque, buf, len);
        }
    }
    return -1;
}

int espplay_i2c_slave_read(int bus, int addr, uint8_t *buf, int len)
{
    for (int i = 0; i < 16; i++) {
        if (i2c_slaves[i].used && i2c_slaves[i].bus == bus &&
            i2c_slaves[i].addr == addr) {
            return i2c_slaves[i].ops->read(i2c_slaves[i].opaque, buf, len);
        }
    }
    return -1;
}

bool espplay_i2c_slave_exists(int bus, int addr)
{
    for (int i = 0; i < 16; i++) {
        if (i2c_slaves[i].used && i2c_slaves[i].bus == bus &&
            i2c_slaves[i].addr == addr) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------- virtual I2C devices */

typedef struct {
    uint8_t regs[256];
    uint8_t ptr;
} Bme280State;

static int bme280_write(void *opaque, const uint8_t *buf, int len)
{
    Bme280State *st = opaque;
    if (len >= 1) {
        st->ptr = buf[0];
    }
    for (int i = 1; i < len; i++) {
        st->regs[st->ptr] = buf[i];
        st->ptr++;
    }
    return 0;
}

static int bme280_read(void *opaque, uint8_t *buf, int len)
{
    Bme280State *st = opaque;
    for (int i = 0; i < len; i++) {
        buf[i] = st->regs[st->ptr];
        st->ptr++;
    }
    return len;
}

static const struct EspplayI2cSlaveOps bme280_ops = { bme280_read, bme280_write, NULL };

static void bme280_init(Bme280State *st, float temp_c, float hum, float press_pa)
{
    memset(st->regs, 0, sizeof(st->regs));
    st->regs[0xD0] = 0x60; /* chip id */
    /* dig_T1..3 */
    st->regs[0x88] = 0x6C; st->regs[0x89] = 0x6E;
    st->regs[0x8A] = 0x43; st->regs[0x8B] = 0x67;
    st->regs[0x8C] = 0x00; st->regs[0x8D] = 0x00;
    /* dig_P1..9 */
    st->regs[0x8E] = 0x8F; st->regs[0x8F] = 0x5B;
    st->regs[0x90] = 0xD8; st->regs[0x91] = 0xD6;
    st->regs[0x92] = 0x0B; st->regs[0x93] = 0x34;
    st->regs[0x94] = 0x2C; st->regs[0x95] = 0x89;
    st->regs[0x96] = 0x08; st->regs[0x97] = 0x00;
    st->regs[0x98] = 0xF9; st->regs[0x99] = 0xFF;
    st->regs[0x9A] = 0x8C; st->regs[0x9B] = 0x35;
    st->regs[0x9C] = 0xF8; st->regs[0x9D] = 0xF6;
    st->regs[0x9E] = 0x6B; st->regs[0x9F] = 0x00;
    /* dig_H1..H6 */
    st->regs[0xA1] = 49;
    st->regs[0xE1] = 0x01; st->regs[0xE2] = 0x0F;
    st->regs[0xE3] = 0x00;
    st->regs[0xE4] = 0x13;
    st->regs[0xE5] = 0x86;
    st->regs[0xE6] = 0x01;
    st->regs[0xF2] = 0x01;
    st->regs[0xF4] = 0x25;
    (void)temp_c; (void)hum; (void)press_pa;
    int32_t raw_t = 519888;
    int32_t raw_h = 30000;
    int32_t raw_p = 415148;
    st->regs[0xFA] = (raw_t >> 12) & 0xFF;
    st->regs[0xFB] = (raw_t >> 4) & 0xFF;
    st->regs[0xFC] = (raw_t << 4) & 0xF0;
    st->regs[0xFD] = (raw_h >> 8) & 0xFF;
    st->regs[0xFE] = raw_h & 0xFF;
    st->regs[0xF7] = (raw_p >> 12) & 0xFF;
    st->regs[0xF8] = (raw_p >> 4) & 0xFF;
    st->regs[0xF9] = (raw_p << 4) & 0xF0;
}

typedef struct {
    uint8_t regs[256];
    uint8_t ptr;
} Mpu6050State;

static int mpu_write(void *opaque, const uint8_t *buf, int len)
{
    Mpu6050State *st = opaque;
    if (len >= 1) {
        st->ptr = buf[0];
    }
    for (int i = 1; i < len; i++) {
        st->regs[st->ptr] = buf[i];
        st->ptr++;
    }
    return 0;
}

static int mpu_read(void *opaque, uint8_t *buf, int len)
{
    Mpu6050State *st = opaque;
    for (int i = 0; i < len; i++) {
        buf[i] = st->regs[st->ptr];
        st->ptr++;
    }
    return len;
}

static const struct EspplayI2cSlaveOps mpu_ops = { mpu_read, mpu_write, NULL };

static void mpu6050_init(Mpu6050State *st, int16_t ax, int16_t ay, int16_t az,
                         int16_t gx, int16_t gy, int16_t gz, int16_t temp_q16)
{
    memset(st->regs, 0, sizeof(st->regs));
    st->regs[0x75] = 0x68;
    int16_t v;
    v = ax; st->regs[0x3B] = v >> 8; st->regs[0x3C] = v & 0xFF;
    v = ay; st->regs[0x3D] = v >> 8; st->regs[0x3E] = v & 0xFF;
    v = az; st->regs[0x3F] = v >> 8; st->regs[0x40] = v & 0xFF;
    v = temp_q16; st->regs[0x41] = v >> 8; st->regs[0x42] = v & 0xFF;
    v = gx; st->regs[0x43] = v >> 8; st->regs[0x44] = v & 0xFF;
    v = gy; st->regs[0x45] = v >> 8; st->regs[0x46] = v & 0xFF;
    v = gz; st->regs[0x47] = v >> 8; st->regs[0x48] = v & 0xFF;
    st->regs[0x6B] = 0x01;
}

typedef struct {
    uint8_t regs[19];
    uint8_t ptr;
} Ds3231State;

static int ds_write(void *opaque, const uint8_t *buf, int len)
{
    Ds3231State *st = opaque;
    if (len >= 1) {
        st->ptr = buf[0] % sizeof(st->regs);
    }
    for (int i = 1; i < len; i++) {
        st->regs[st->ptr] = buf[i];
        st->ptr = (st->ptr + 1) % sizeof(st->regs);
    }
    return 0;
}

static int ds_read(void *opaque, uint8_t *buf, int len)
{
    Ds3231State *st = opaque;
    for (int i = 0; i < len; i++) {
        buf[i] = st->regs[st->ptr];
        st->ptr = (st->ptr + 1) % sizeof(st->regs);
    }
    return len;
}

static const struct EspplayI2cSlaveOps ds_ops = { ds_read, ds_write, NULL };

static void ds3231_init(Ds3231State *st, int y, int mo, int d, int h, int mi, int s)
{
    memset(st->regs, 0, sizeof(st->regs));
    st->regs[0] = ((s / 10) << 4) | (s % 10);
    st->regs[1] = ((mi / 10) << 4) | (mi % 10);
    st->regs[2] = ((h / 10) << 4) | (h % 10);
    st->regs[3] = 1;
    st->regs[4] = ((d / 10) << 4) | (d % 10);
    st->regs[5] = ((mo / 10) << 4) | (mo % 10);
    st->regs[6] = ((((y - 2000) / 10) << 4) | ((y - 2000) % 10));
    st->regs[0x11] = 0x19; /* temp hi: 25 C */
    st->regs[0x12] = 0x00;
}

typedef struct {
    uint8_t bus, addr;
    uint8_t fb[8][128];
    uint8_t col, page;
    bool active;
} Ssd1306State;

static Ssd1306State ssd1306;

static int ssd_write(void *opaque, const uint8_t *buf, int len)
{
    Ssd1306State *st = opaque;
    if (len < 1) {
        return 0;
    }
    if (buf[0] == 0x00) {
        for (int i = 1; i < len; i++) {
            uint8_t c = buf[i];
            if (c == 0x21 && i + 2 < len) {
                st->col = buf[i + 1] & 0x7F;
                i += 2;
            } else if (c == 0x22 && i + 2 < len) {
                st->page = buf[i + 1] & 0x07;
                i += 2;
            } else if ((c & 0xF8) == 0xB0) {
                st->page = c & 0x07;
            }
        }
    } else if (buf[0] == 0x40) {
        for (int i = 1; i < len; i++) {
            st->fb[st->page][st->col] = buf[i];
            st->col = (st->col + 1) & 0x7F;
        }
        uint8_t msg[4] = { st->bus, st->addr, st->page, st->col };
        espplay_send_display_frame(msg, &st->fb[0][0], 8 * 128);
    }
    return 0;
}

static int ssd_read(void *opaque, uint8_t *buf, int len)
{
    memset(buf, 0, len);
    return len;
}

static const struct EspplayI2cSlaveOps ssd_ops = { ssd_read, ssd_write, NULL };

typedef struct {
    int nibble;
    uint8_t lines[2][16];
    int col, row;
    uint8_t bus, addr;
    bool active;
} Lcd1602State;

static Lcd1602State lcd1602;

static int lcd_write(void *opaque, const uint8_t *buf, int len)
{
    Lcd1602State *st = opaque;
    for (int i = 0; i < len; i++) {
        uint8_t v = buf[i];
        if (v & 0x04) {
            continue;
        }
        if (st->nibble == 0) {
            st->nibble = (v >> 4) & 0x0F;
            st->nibble |= ((v & 0x01) ? 0x100 : 0);
        } else {
            int rs = st->nibble >> 8;
            uint8_t byte = ((st->nibble & 0xF) << 4) | ((v >> 4) & 0x0F);
            st->nibble = 0;
            if (rs) {
                if (byte == '\n') {
                    st->row ^= 1;
                    st->col = 0;
                } else if (st->col < 16) {
                    st->lines[st->row][st->col++] = byte;
                }
                uint8_t msg[6] = { st->bus, st->addr, (uint8_t)st->row,
                                   (uint8_t)st->col, 0, 0 };
                espplay_send_lcd_frame(msg, &st->lines[0][0], 32);
            } else {
                if (byte == 0x01) {
                    memset(st->lines, ' ', sizeof(st->lines));
                    st->row = st->col = 0;
                } else if (byte == 0x02) {
                    st->row = st->col = 0;
                } else if ((byte & 0xFC) == 0x80) {
                    int off = byte & 0x3F;
                    st->row = off >= 0x40 ? 1 : 0;
                    st->col = off & 0x3F;
                    if (st->col > 15) {
                        st->col = 15;
                    }
                }
            }
        }
    }
    return 0;
}

static int lcd_read(void *opaque, uint8_t *buf, int len)
{
    memset(buf, 0xFF, len);
    return len;
}

static const struct EspplayI2cSlaveOps lcd_ops = { lcd_read, lcd_write, NULL };

/* ------------------------------------------------------- sensor config */

typedef struct {
    int kind;
    int bus, addr;
    int p1, p2;
} SensorCfg;

static SensorCfg sensor_cfgs[16];

void espplay_sensor_cfg(const uint8_t *p, uint32_t len)
{
    if (len < 4) {
        return;
    }
    int kind = p[0], bus = p[1], addr = p[2];
    int idx = -1;
    for (int i = 0; i < 16; i++) {
        if (sensor_cfgs[i].kind == kind && sensor_cfgs[i].bus == bus &&
            sensor_cfgs[i].addr == addr) {
            idx = i;
            break;
        }
        if (idx < 0 && sensor_cfgs[i].kind == 0) {
            idx = i;
        }
    }
    if (idx < 0) {
        return;
    }
    SensorCfg *c = &sensor_cfgs[idx];
    c->kind = kind;
    c->bus = bus;
    c->addr = addr;
    const uint8_t *pl = p + 3;
    uint32_t plen = len - 3;

    switch (kind) {
    case ESPPLAY_SENSOR_BME280: {
        static Bme280State st;
        float t = 25.0f, h = 50.0f, pr = 101325.0f;
        if (plen >= 12) {
            memcpy(&t, pl, 4);
            memcpy(&h, pl + 4, 4);
            memcpy(&pr, pl + 8, 4);
        }
        bme280_init(&st, t, h, pr);
        espplay_i2c_register_slave(bus, addr, &bme280_ops, &st);
        break;
    }
    case ESPPLAY_SENSOR_MPU6050: {
        static Mpu6050State st;
        int16_t vals[7] = { 0, 0, 16384, 0, 0, 0, 25 * 256 };
        if (plen >= 14) {
            memcpy(vals, pl, 14);
        }
        mpu6050_init(&st, vals[0], vals[1], vals[2], vals[3], vals[4],
                     vals[5], vals[6]);
        espplay_i2c_register_slave(bus, addr, &mpu_ops, &st);
        break;
    }
    case ESPPLAY_SENSOR_DS3231: {
        static Ds3231State st;
        int y = 2026, mo = 1, d = 1, h = 12, mi = 0, s = 0;
        if (plen >= 12) {
            memcpy(&y, pl, 4);
            memcpy(&mo, pl + 4, 4);
            memcpy(&d, pl + 8, 4);
        }
        ds3231_init(&st, y, mo, d, h, mi, s);
        espplay_i2c_register_slave(bus, addr, &ds_ops, &st);
        break;
    }
    case ESPPLAY_SENSOR_SSD1306:
        ssd1306.bus = bus;
        ssd1306.addr = addr;
        memset(ssd1306.fb, 0, sizeof(ssd1306.fb));
        ssd1306.active = true;
        espplay_i2c_register_slave(bus, addr, &ssd_ops, &ssd1306);
        break;
    case ESPPLAY_SENSOR_LCD1602:
        lcd1602.bus = bus;
        lcd1602.addr = addr;
        memset(lcd1602.lines, ' ', sizeof(lcd1602.lines));
        lcd1602.active = true;
        espplay_i2c_register_slave(bus, addr, &lcd_ops, &lcd1602);
        break;
    default:
        c->p1 = plen > 0 ? p[3] : 0;
        c->p2 = plen > 1 ? p[4] : 0;
        break;
    }
}

void espplay_i2cdev_attach(int bus, int addr, int kind)
{
    uint8_t msg[4] = { (uint8_t)kind, (uint8_t)bus, (uint8_t)addr, 0 };
    espplay_sensor_cfg(msg, 4);
}

void espplay_state_reset(void)
{
    memset(sensor_cfgs, 0, sizeof(sensor_cfgs));
    for (int i = 0; i < 16; i++) {
        i2c_slaves[i].used = false;
    }
    ssd1306.active = false;
    lcd1602.active = false;
}

void espplay_uart_input(int uart, const uint8_t *data, int len)
{
    extern void esp32_uart_rx_feed(int uart, const uint8_t *data, int len);
    esp32_uart_rx_feed(uart, data, len);
}

void espplay_send_display_frame(const uint8_t *meta, const uint8_t *fb, int len)
{
    EspplayBridgeState *s = espplay_bridge_get();
    if (!s) {
        return;
    }
    uint8_t *p = g_malloc(4 + len);
    p[0] = meta[0];
    p[1] = meta[1];
    p[2] = meta[2];
    p[3] = meta[3];
    memcpy(p + 4, fb, len);
    espplay_send_msg(s, ESPPLAY_MSG_DISPLAY_FB, p, 4 + len);
    g_free(p);
}

void espplay_send_lcd_frame(const uint8_t *meta, const uint8_t *lines, int len)
{
    EspplayBridgeState *s = espplay_bridge_get();
    if (!s) {
        return;
    }
    uint8_t *p = g_malloc(4 + len);
    memcpy(p, meta, 4);
    memcpy(p + 4, lines, len);
    espplay_send_msg(s, ESPPLAY_MSG_LCD_TEXT, p, 4 + len);
    g_free(p);
}
