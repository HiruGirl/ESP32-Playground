/*
 * ESP32 Playground - host bridge device implementation
 *
 * Copyright (c) 2026 ESP32 Playground contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "chardev/char-fe.h"
#include "net/net.h"
#include "net/eth.h"
#include "exec/address-spaces.h"
#include "hw/misc/espplay_bridge.h"
#include "hw/misc/espplay_internal.h"

static EspplayBridgeState *espplay_singleton;

EspplayBridgeState *espplay_bridge_get(void)
{
    return espplay_singleton;
}

void espplay_send_msg(EspplayBridgeState *s, uint8_t type,
                      const uint8_t *payload, uint32_t plen)
{
    uint8_t hdr[5];
    uint32_t len = 1 + plen;
    hdr[0] = len & 0xFF;
    hdr[1] = (len >> 8) & 0xFF;
    hdr[2] = (len >> 16) & 0xFF;
    hdr[3] = (len >> 24) & 0xFF;
    hdr[4] = type;
    if (!s || !qemu_chr_fe_backend_connected(&s->chr)) {
        return;
    }
    qemu_chr_fe_write(&s->chr, hdr, 5);
    if (plen) {
        qemu_chr_fe_write(&s->chr, payload, plen);
    }
}

void espplay_gpio_out_update(int pin, int level)
{
    uint8_t p[2] = { (uint8_t)pin, (uint8_t)(level ? 1 : 0) };
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_GPIO_LEVEL, p, 2);
}

void espplay_gpio_mode_update(int pin, int mode, int pull)
{
    uint8_t p[3] = { (uint8_t)pin, (uint8_t)mode, (uint8_t)pull };
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_GPIO_MODE, p, 3);
}

void espplay_ledc_update(int timer, uint32_t freq, int channel, uint32_t duty_permyriad)
{
    uint8_t p[10];
    p[0] = (uint8_t)timer;
    p[1] = freq & 0xFF;
    p[2] = (freq >> 8) & 0xFF;
    p[3] = (freq >> 16) & 0xFF;
    p[4] = (freq >> 24) & 0xFF;
    p[5] = (uint8_t)channel;
    p[6] = duty_permyriad & 0xFF;
    p[7] = (duty_permyriad >> 8) & 0xFF;
    p[8] = (duty_permyriad >> 16) & 0xFF;
    p[9] = (duty_permyriad >> 24) & 0xFF;
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_LEDC, p, 10);
}

void espplay_rmt_frame(int channel, const uint8_t *bytes, size_t len)
{
    if (len > 1024) {
        len = 1024;
    }
    uint8_t *p = g_malloc(3 + len);
    p[0] = (uint8_t)channel;
    p[1] = len & 0xFF;
    p[2] = (len >> 8) & 0xFF;
    memcpy(p + 3, bytes, len);
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_RMT_FRAME, p, 3 + len);
    g_free(p);
}

void espplay_i2c_write(int bus, int addr, const uint8_t *bytes, size_t len)
{
    if (len > 2048) {
        len = 2048;
    }
    uint8_t *p = g_malloc(4 + len);
    p[0] = (uint8_t)bus;
    p[1] = (uint8_t)addr;
    p[2] = len & 0xFF;
    p[3] = (len >> 8) & 0xFF;
    memcpy(p + 4, bytes, len);
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_I2C_WRITE, p, 4 + len);
    g_free(p);
}

void espplay_uart_data(int uart, const uint8_t *bytes, size_t len)
{
    if (len > 2048) {
        len = 2048;
    }
    uint8_t *p = g_malloc(3 + len);
    p[0] = (uint8_t)uart;
    p[1] = len & 0xFF;
    p[2] = (len >> 8) & 0xFF;
    memcpy(p + 3, bytes, len);
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_UART_DATA, p, 3 + len);
    g_free(p);
}

void espplay_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_LOG,
                     (const uint8_t *)buf, strlen(buf));
}

uint32_t espplay_gpio_input_val(EspplayBridgeState *s)
{
    return s ? s->gpio_input_val : 0;
}

uint32_t espplay_gpio_input_mask(EspplayBridgeState *s)
{
    return s ? s->gpio_input_mask : 0;
}

uint16_t espplay_adc_value(EspplayBridgeState *s, int channel, bool *has)
{
    if (!s || channel < 0 || channel >= 32) {
        *has = false;
        return 0;
    }
    *has = (s->adc_mask >> channel) & 1;
    return s->adc_value[channel];
}

/* ------------------------------------------------------------------ NIC */

static ssize_t espplay_net_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    EspplayBridgeState *s = qemu_get_nic_opaque(nc);
    uint8_t *p = g_malloc(2 + size);
    p[0] = size & 0xFF;
    p[1] = (size >> 8) & 0xFF;
    memcpy(p + 2, buf, size);
    espplay_send_msg(s, ESPPLAY_MSG_WIFI_RX_FRAME, p, 2 + size);
    g_free(p);
    espplay_wifi_frame_rx(buf, size);
    return size;
}

static NetClientInfo espplay_net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = espplay_net_receive,
};

void espplay_wifi_frame_tx(const uint8_t *buf, size_t size)
{
    EspplayBridgeState *s = espplay_bridge_get();
    if (s && s->nic) {
        uint8_t *p = g_malloc(2 + size);
        p[0] = size & 0xFF;
        p[1] = (size >> 8) & 0xFF;
        memcpy(p + 2, buf, size);
        espplay_send_msg(s, ESPPLAY_MSG_WIFI_TX_FRAME, p, 2 + size);
        g_free(p);
        qemu_send_packet(qemu_get_queue(s->nic), buf, size);
    }
}

/* ------------------------------------------------------- command dispatch */

static void espplay_handle_cmd(EspplayBridgeState *s, uint8_t type,
                               const uint8_t *p, uint32_t len)
{
    switch (type) {
    case ESPPLAY_CMD_PING:
        espplay_send_msg(s, ESPPLAY_MSG_HELLO, (const uint8_t[]){ 1, 0, 0, 0, s->chip_id }, 5);
        break;
    case ESPPLAY_CMD_GPIO_INPUT:
        if (len >= 2 && p[0] < 32) {
            if (p[1] & 1) {
                s->gpio_input_val |= 1u << p[0];
            } else {
                s->gpio_input_val &= ~(1u << p[0]);
            }
            s->gpio_input_mask |= 1u << p[0];
            espplay_gpio_input_changed(s, p[0]);
        }
        break;
    case ESPPLAY_CMD_ADC_VALUE:
        if (len >= 3 && p[0] < 32) {
            s->adc_value[p[0]] = p[1] | (p[2] << 8);
            s->adc_mask |= 1u << p[0];
        }
        break;
    case ESPPLAY_CMD_SENSOR_CFG:
        espplay_sensor_cfg(p, len);
        break;
    case ESPPLAY_CMD_I2CDEV_ATTACH:
        if (len >= 3) {
            espplay_i2cdev_attach(p[0], p[1], p[2]);
        }
        break;
    case ESPPLAY_CMD_WIFI_CONFIG:
        if (len >= 2) {
            uint32_t n = p[0] | (p[1] << 8);
            if (n > sizeof(s->wifi_json) - 1) {
                n = sizeof(s->wifi_json) - 1;
            }
            if (len >= 2 + n) {
                memcpy(s->wifi_json, p + 2, n);
                s->wifi_json[n] = 0;
                s->wifi_json_len = n;
                espplay_wifi_config_updated(s);
            }
        }
        break;
    case ESPPLAY_CMD_WIFI_RESULT:
        if (len >= 3) {
            espplay_wifi_connect_result(p[0], (int8_t)p[1], p[2],
                                        len >= 9 ? p + 3 : NULL,
                                        len >= 10 ? p[9] : 0);
        }
        break;
    case ESPPLAY_MSG_WIFI_EVENT:
        if (len >= 1) {
            espplay_wifi_bridge_event(p[0], len >= 2 ? p[1] : 0);
        }
        break;
    case ESPPLAY_CMD_WIFI_SIM:
        if (len >= 2) {
            espplay_wifi_sim_event(p[0], p[1]);
        }
        break;
    case ESPPLAY_CMD_HCI_DOWN:
        if (len >= 2) {
            uint32_t n = p[0] | (p[1] << 8);
            if (len >= 2 + n) {
                espplay_hci_down(p + 2, n);
            }
        }
        break;
    case ESPPLAY_CMD_UART_INPUT:
        if (len >= 3) {
            uint32_t n = p[1] | (p[2] << 8);
            if (len >= 3 + n) {
                espplay_uart_input(p[0], p + 3, n);
            }
        }
        break;
    case ESPPLAY_CMD_STATE_RESET:
        s->gpio_input_mask = 0;
        s->adc_mask = 0;
        espplay_state_reset();
        break;
    default:
        break;
    }
}

static int espplay_chr_can_receive(void *opaque)
{
    return 4096;
}

static void espplay_chr_read(void *opaque, const uint8_t *buf, int size)
{
    EspplayBridgeState *s = ESPPLAY_BRIDGE(opaque);
    if (s->rxbuf_len + size > s->rxbuf_cap) {
        s->rxbuf_cap = (s->rxbuf_len + size + 4095) & ~4095;
        s->rxbuf = g_realloc(s->rxbuf, s->rxbuf_cap);
    }
    memcpy(s->rxbuf + s->rxbuf_len, buf, size);
    s->rxbuf_len += size;

    for (;;) {
        if (s->rxbuf_len < 5) {
            return;
        }
        uint32_t mlen = s->rxbuf[0] | (s->rxbuf[1] << 8) | (s->rxbuf[2] << 16) | (s->rxbuf[3] << 24);
        if (mlen == 0 || mlen > 65536) {
            memmove(s->rxbuf, s->rxbuf + 1, --s->rxbuf_len);
            continue;
        }
        if (s->rxbuf_len < 4 + mlen) {
            return;
        }
        uint8_t type = s->rxbuf[4];
        espplay_handle_cmd(s, type, s->rxbuf + 5, mlen - 1);
        uint32_t used = 4 + mlen;
        memmove(s->rxbuf, s->rxbuf + used, s->rxbuf_len - used);
        s->rxbuf_len -= used;
    }
}

/* ----------------------------------------------------------- device glue */

static void espplay_realize(DeviceState *dev, Error **errp)
{
    EspplayBridgeState *s = ESPPLAY_BRIDGE(dev);

    qemu_chr_fe_set_handlers(&s->chr, espplay_chr_can_receive,
                             espplay_chr_read, NULL, NULL, s, NULL, true);

    s->nic = qemu_new_nic(&espplay_net_info, &s->nic_conf,
                          object_get_typename(OBJECT(dev)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->nic_conf.macaddr.a);

    /* Map the qcall completion cave: executable RAM at a SoC address the
     * firmware never uses (IROM window gap on ESP32/S3, SRAM gap on C3). */
    if (s->cave_addr) {
        memory_region_init_ram(&s->cave_mr, OBJECT(dev), "espplay.cave", 256,
                               &error_fatal);
        memory_region_add_subregion(get_system_memory(), s->cave_addr,
                                    &s->cave_mr);
    }

    espplay_singleton = s;
    espplay_hooks_init(s);
    espplay_send_msg(s, ESPPLAY_MSG_HELLO, (const uint8_t[]){ 1, 0, 0, 0, s->chip_id }, 5);
}

static void espplay_unrealize(DeviceState *dev)
{
    EspplayBridgeState *s = ESPPLAY_BRIDGE(dev);
    if (espplay_singleton == s) {
        espplay_singleton = NULL;
    }
}

static Property espplay_properties[] = {
    DEFINE_PROP_CHR("chardev", EspplayBridgeState, chr),
    DEFINE_PROP_NETDEV("netdev", EspplayBridgeState, nic_conf.peers),
    DEFINE_PROP_UINT8("chip-id", EspplayBridgeState, chip_id, 0),
    DEFINE_PROP_STRING("elf", EspplayBridgeState, elf_path),
    DEFINE_PROP_UINT32("cave-addr", EspplayBridgeState, cave_addr, 0x400A0000),
    DEFINE_PROP_END_OF_LIST(),
};

static void espplay_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = espplay_realize;
    dc->unrealize = espplay_unrealize;
    dc->user_creatable = true;
    device_class_set_props(dc, espplay_properties);
}

static void espplay_init(Object *obj)
{
    EspplayBridgeState *s = ESPPLAY_BRIDGE(obj);
    s->nic_conf.macaddr.a[0] = 0x52;
    s->nic_conf.macaddr.a[1] = 0x54;
    s->nic_conf.macaddr.a[2] = 0x00;
    s->nic_conf.macaddr.a[3] = 0x12;
    s->nic_conf.macaddr.a[4] = 0x34;
    s->nic_conf.macaddr.a[5] = 0x56;
}

static const TypeInfo espplay_info = {
    .name = TYPE_ESPPLAY_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(EspplayBridgeState),
    .instance_init = espplay_init,
    .class_init = espplay_class_init,
};

static void espplay_register_types(void)
{
    type_register_static(&espplay_info);
}

type_init(espplay_register_types)
