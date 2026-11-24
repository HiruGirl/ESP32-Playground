/*
 * ESP32 Playground - internal interfaces between bridge, hooks and models
 *
 * Copyright (c) 2026 ESP32 Playground contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#ifndef HW_MISC_ESPPLAY_INTERNAL_H
#define HW_MISC_ESPPLAY_INTERNAL_H

#include "hw/misc/espplay_bridge.h"

typedef bool (*EspplayHookFn)(CPUState *cpu, uint32_t pc, const char *symbol);

typedef struct {
    const char *symbol;
    EspplayHookFn fn;
    uint32_t addr;
    bool armed;
} EspplayHook;

extern EspplayHook espplay_hooks[];
extern const int espplay_hooks_count;
#define NUM_ESPPLAY_HOOKS espplay_hooks_count

/* espplay_hooks.c */
void espplay_hooks_init(EspplayBridgeState *s);
bool espplay_debug_check(CPUState *cpu);
bool espplay_qcall_active(void);
void espplay_qcall_request(uint32_t func, const uint32_t *args, int nargs,
                           void (*done)(uint32_t, uint32_t, void *), void *ctx);
bool espplay_sym_lookup(const char *name, uint32_t *addr);
uint32_t espplay_get_pc(CPUState *cpu);
void espplay_set_pc(CPUState *cpu, uint32_t pc);
uint32_t espplay_get_arg(CPUState *cpu, int idx);
uint32_t espplay_get_ra(CPUState *cpu);
void espplay_set_ret(CPUState *cpu, uint32_t v0, uint32_t v1);

/* handler entry points */
bool espplay_hook_wifi_fn(CPUState *cpu, uint32_t pc, const char *symbol);
bool espplay_hook_ble_fn(CPUState *cpu, uint32_t pc, const char *symbol);

/* espplay_core.c */
void espplay_sensor_cfg(const uint8_t *p, uint32_t len);
void espplay_i2cdev_attach(int bus, int addr, int kind);
void espplay_state_reset(void);
void espplay_uart_input(int uart, const uint8_t *data, int len);
void espplay_gpio_input_changed(EspplayBridgeState *s, int pin);
void espplay_wifi_frame_rx(const uint8_t *buf, size_t size);
void espplay_wifi_config_updated(EspplayBridgeState *s);
void espplay_wifi_connect_result(int ok, int8_t rssi, int channel,
                                 const uint8_t *bssid, int authmode);
void espplay_wifi_sim_event(int type, int reason);
void espplay_wifi_bridge_event(int ev, int reason);
void espplay_wifi_ev_pump(void);
void espplay_hci_down(const uint8_t *data, int len);
void espplay_send_display_frame(const uint8_t *meta, const uint8_t *fb, int len);
void espplay_send_lcd_frame(const uint8_t *meta, const uint8_t *lines, int len);

/* I2C slave model registration (patched hw/i2c/esp32_i2c.c hooks these) */
struct EspplayI2cSlaveOps {
    int (*read)(void *opaque, uint8_t *buf, int len);
    int (*write)(void *opaque, const uint8_t *buf, int len);
    void (*stop)(void *opaque);
};
void espplay_i2c_register_slave(int bus, int addr,
                                const struct EspplayI2cSlaveOps *ops,
                                void *opaque);
void espplay_i2c_unregister_slave(int bus, int addr);
int espplay_i2c_slave_write(int bus, int addr, const uint8_t *buf, int len);
int espplay_i2c_slave_read(int bus, int addr, uint8_t *buf, int len);
bool espplay_i2c_slave_exists(int bus, int addr);

#endif
