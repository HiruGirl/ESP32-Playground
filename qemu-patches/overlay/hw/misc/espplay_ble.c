/*
 * ESP32 Playground - VHCI (virtual HCI) seam for BLE
 *
 * Firmware-side esp_vhci_host_* / esp_bt_controller_* calls are intercepted;
 * HCI packets flow to the backend virtual controller.
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
#if defined(TARGET_XTENSA)
#include "target/xtensa/cpu.h"
#elif defined(TARGET_RISCV)
#include "target/riscv/cpu.h"
#endif

#define ESP_OK 0
#define ESP_BT_CONTROLLER_STATUS_ENABLED 2

static struct {
    bool callback_registered;
    uint32_t recv_cb;
    bool send_available;
    int controller_status;
} B = { .send_available = true, .controller_status = 0 };

typedef struct {
    uint8_t *data;
    int len;
} HciCtx;

void espplay_hci_call_done(uint32_t r0, uint32_t r1, void *ctx)
{
    g_free(ctx);
}

void espplay_hci_malloc_done(uint32_t r0, uint32_t r1, void *ctx)
{
    HciCtx *c = ctx;
    CPUState *cpu = qemu_get_cpu(0);
    if (r0 == 0 || !cpu) {
        g_free(c);
        return;
    }
    cpu_memory_rw_debug(cpu, r0, c->data, c->len, true);
    uint32_t args[2] = { r0, (uint32_t)c->len };
    espplay_qcall_request(B.recv_cb, args, 2, espplay_hci_call_done, c);
}

void espplay_hci_down(const uint8_t *data, int len)
{
    if (!B.recv_cb || len <= 0) {
        return;
    }
    uint32_t malloc_addr = 0;
    if (!espplay_sym_lookup("malloc", &malloc_addr)) {
        return;
    }
    HciCtx *c = g_malloc(sizeof(*c) + len);
    c->data = (uint8_t *)(c + 1);
    c->len = len;
    memcpy(c->data, data, len);
    uint32_t args[1] = { (uint32_t)len };
    espplay_qcall_request(malloc_addr, args, 1, espplay_hci_malloc_done, c);
}

bool espplay_hook_ble_fn(CPUState *cpu, uint32_t pc, const char *sym)
{
    if (strcmp(sym, "esp_vhci_host_register_callback") == 0) {
        B.recv_cb = espplay_get_arg(cpu, 0);
        B.callback_registered = true;
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_vhci_host_send_packet") == 0) {
        uint32_t buf = espplay_get_arg(cpu, 0);
        int len = espplay_get_arg(cpu, 1);
        if (len > 0 && len <= 512) {
            uint8_t pkt[512];
            cpu_memory_rw_debug(cpu, buf, pkt, len, false);
            EspplayBridgeState *s = espplay_bridge_get();
            uint8_t *p = g_malloc(2 + len);
            p[0] = len & 0xFF;
            p[1] = (len >> 8) & 0xFF;
            memcpy(p + 2, pkt, len);
            espplay_send_msg(s, ESPPLAY_MSG_HCI_UP, p, 2 + len);
            g_free(p);
        }
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_vhci_host_check_send_available") == 0) {
        espplay_set_ret(cpu, B.send_available ? 1 : 0, 0);
        return true;
    }
    if (strcmp(sym, "esp_bt_controller_init") == 0 ||
        strcmp(sym, "esp_bt_controller_enable") == 0 ||
        strcmp(sym, "esp_bt_controller_mem_release") == 0 ||
        strcmp(sym, "esp_bt_controller_mem_acquire") == 0) {
        if (strcmp(sym, "esp_bt_controller_enable") == 0) {
            B.controller_status = ESP_BT_CONTROLLER_STATUS_ENABLED;
        }
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_bt_controller_disable") == 0) {
        B.controller_status = 0;
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_bt_controller_deinit") == 0) {
        B.controller_status = 0;
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_bt_controller_get_status") == 0) {
        espplay_set_ret(cpu, B.controller_status, 0);
        return true;
    }
    return false;
}
