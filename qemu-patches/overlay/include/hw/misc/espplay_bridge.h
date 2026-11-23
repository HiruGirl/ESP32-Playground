/*
 * ESP32 Playground - host bridge device
 *
 * Copyright (c) 2026 ESP32 Playground contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#ifndef HW_MISC_ESPPLAY_BRIDGE_H
#define HW_MISC_ESPPLAY_BRIDGE_H

#include "hw/sysbus.h"
#include "qemu/sockets.h"
#include "chardev/char-fe.h"
#include "net/net.h"

#define TYPE_ESPPLAY_BRIDGE "espplay.bridge"
#define ESPPLAY_BRIDGE(obj) OBJECT_CHECK(EspplayBridgeState, (obj), TYPE_ESPPLAY_BRIDGE)

/* Wire protocol: u32-le length (of type+payload), u8 type, payload. */
enum EspplayMsgType {
    /* QEMU -> backend */
    ESPPLAY_MSG_HELLO           = 0x01, /* u32 proto, u8 chip */
    ESPPLAY_MSG_GPIO_LEVEL      = 0x02, /* u8 pin, u8 level */
    ESPPLAY_MSG_GPIO_MODE       = 0x03, /* u8 pin, u8 mode, u8 pull */
    ESPPLAY_MSG_LEDC            = 0x04, /* u8 timer, u32 freq, u8 channel, u32 duty_permyriad */
    ESPPLAY_MSG_RMT_FRAME       = 0x05, /* u8 channel, u16 n, bytes */
    ESPPLAY_MSG_I2C_WRITE       = 0x06, /* u8 bus, u8 addr, u16 n, bytes */
    ESPPLAY_MSG_UART_DATA       = 0x07, /* u8 uart, u16 n, bytes */
    ESPPLAY_MSG_WIFI_TX_FRAME   = 0x08, /* u16 n, frame */
    ESPPLAY_MSG_WIFI_RX_FRAME   = 0x09, /* u16 n, frame */
    ESPPLAY_MSG_HCI_UP          = 0x0A, /* u16 n, packet */
    ESPPLAY_MSG_WIFI_SCAN       = 0x0B,
    ESPPLAY_MSG_WIFI_CONNECT_REQ= 0x0C, /* u8 ssid_len, ssid, u8 pass_len, pass */
    ESPPLAY_MSG_WIFI_EVENT      = 0x0D, /* u8 event, u8 status */
    ESPPLAY_MSG_ADC_READ        = 0x0E, /* u8 channel */
    ESPPLAY_MSG_DHT22_DONE      = 0x0F, /* u8 pin */
    ESPPLAY_MSG_LOG             = 0x10, /* ascii text (debug) */
    ESPPLAY_MSG_DISPLAY_FB      = 0x11, /* u8 bus, u8 addr, u8 page, u8 col, 1024B fb */
    ESPPLAY_MSG_LCD_TEXT        = 0x12, /* u8 bus, u8 addr, u8 row, u8 col, 32B text */

    /* Backend -> QEMU */
    ESPPLAY_CMD_PING            = 0x80,
    ESPPLAY_CMD_GPIO_INPUT      = 0x81, /* u8 pin, u8 level */
    ESPPLAY_CMD_ADC_VALUE       = 0x82, /* u8 channel, u16 value */
    ESPPLAY_CMD_SENSOR_CFG      = 0x83, /* u8 kind, u8 bus, u8 addr, u16 n, params */
    ESPPLAY_CMD_I2CDEV_ATTACH   = 0x84, /* u8 bus, u8 addr, u8 kind */
    ESPPLAY_CMD_WIFI_CONFIG     = 0x85, /* u16 n, json */
    ESPPLAY_CMD_WIFI_RESULT     = 0x86, /* u8 ok, s8 rssi, u8 channel, u8 bssid[6], u8 authmode */
    ESPPLAY_CMD_WIFI_SIM        = 0x87, /* u8 type, u8 reason */
    ESPPLAY_CMD_HCI_DOWN        = 0x88, /* u16 n, packet */
    ESPPLAY_CMD_UART_INPUT      = 0x89, /* u8 uart, u16 n, bytes */
    ESPPLAY_CMD_STATE_RESET     = 0x8A,
};

/* Kinds for ESPPLAY_CMD_SENSOR_CFG */
enum EspplaySensorKind {
    ESPPLAY_SENSOR_BME280  = 1,
    ESPPLAY_SENSOR_MPU6050 = 2,
    ESPPLAY_SENSOR_DS3231  = 3,
    ESPPLAY_SENSOR_DHT22   = 4,
    ESPPLAY_SENSOR_HCSR04  = 5,
    ESPPLAY_SENSOR_SSD1306 = 6,
    ESPPLAY_SENSOR_LCD1602 = 7,
    ESPPLAY_SENSOR_NEOPIXEL= 8,
    ESPPLAY_SENSOR_BUTTON  = 9,
};

typedef struct EspplayBridgeState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/
    CharBackend chr;
    uint8_t chip_id;

    /* inbound reassembly buffer */
    uint8_t *rxbuf;
    uint32_t rxbuf_len;
    uint32_t rxbuf_cap;

    /* GPIO input drive from backend */
    uint32_t gpio_input_val;
    uint32_t gpio_input_mask;

    /* ADC values injected by backend */
    uint16_t adc_value[32];
    uint32_t adc_mask;

    /* NIC for WiFi frames */
    NICState *nic;
    NICConf nic_conf;

    /* WiFi policy state (fed from backend) */
    char wifi_json[2048];
    uint32_t wifi_json_len;

    /* HCI */
    bool vhci_enabled;

    /* hook framework */
    char *elf_path;
    uint32_t cave_addr;
    MemoryRegion cave_mr;
} EspplayBridgeState;

/* Global accessor used by patched peripheral models. NULL when the bridge
 * device is not present (plain QEMU use). */
EspplayBridgeState *espplay_bridge_get(void);

/* Send a wire-protocol message to the backend. */
void espplay_send_msg(EspplayBridgeState *s, uint8_t type,
                      const uint8_t *payload, uint32_t plen);

/* Notifications from peripheral models into the bridge (safe to call with
 * no bridge present). */
void espplay_gpio_out_update(int pin, int level);
void espplay_gpio_mode_update(int pin, int mode, int pull);
void espplay_ledc_update(int timer, uint32_t freq, int channel, uint32_t duty_permyriad);
void espplay_rmt_frame(int channel, const uint8_t *bytes, size_t len);
void espplay_i2c_write(int bus, int addr, const uint8_t *bytes, size_t len);
void espplay_uart_data(int uart, const uint8_t *bytes, size_t len);
void espplay_log(const char *fmt, ...);

/* Peripheral-model queries into bridge state. */
uint32_t espplay_gpio_input_val(EspplayBridgeState *s);
uint32_t espplay_gpio_input_mask(EspplayBridgeState *s);
uint16_t espplay_adc_value(EspplayBridgeState *s, int channel, bool *has);

#endif
