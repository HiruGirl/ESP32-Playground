/*
 * ESP32 Playground - esp_wifi replacement hooks + frame path
 *
 * Implements the emulated WiFi MAC/PHY as an API-level shim: firmware calls
 * to esp_wifi_* are intercepted at function entry (ELF symbol breakpoints)
 * and served from the virtual router configuration maintained by the
 * backend. 802.3 frames flow through QEMU user-mode networking (slirp).
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

#define WIFI_EVENT_STA_START      2
#define WIFI_EVENT_STA_STOP       3
#define WIFI_EVENT_STA_CONNECTED  4
#define WIFI_EVENT_STA_DISCONNECTED 5
#define WIFI_EVENT_SCAN_DONE      8

#define ESP_OK 0

static struct {
    bool inited;
    bool started;
    int mode;
    uint8_t sta_ssid[33];
    uint8_t sta_pass[65];
    bool sta_configured;
    bool connecting;
    bool connected;
    int8_t rssi;
    uint8_t channel;
    uint8_t bssid[6];
    int authmode;

    struct {
        uint8_t ssid[33];
        uint8_t bssid[6];
        int8_t rssi;
        uint8_t auth;
        uint8_t ch;
    } aps[16];
    int ap_count;

    uint32_t rxcb;
    uint32_t ev_base_wifi;
    uint32_t ev_base_ip;
    uint32_t p_event_post;

    uint8_t mac[6];
} W;

static void wifi_mem_write(CPUState *cpu, uint32_t addr, const void *buf, int len)
{
    cpu_memory_rw_debug(cpu, addr, (uint8_t *)buf, len, true);
}

static void wifi_mem_read(CPUState *cpu, uint32_t addr, void *buf, int len)
{
    cpu_memory_rw_debug(cpu, addr, buf, len, false);
}

/* -------------------------------------------------------- event posting */

typedef struct {
    uint32_t base;
    int32_t id;
    int remaining;
} EvPost;

static void ev_post_done(uint32_t r0, uint32_t r1, void *ctx)
{
    EvPost *ev = ctx;
    if (ev->remaining > 0 && W.p_event_post) {
        uint32_t args[5] = { ev->base, (uint32_t)ev->id, 0, 0, 0 };
        ev->remaining--;
        espplay_qcall_request(W.p_event_post, args, 5, ev_post_done, ev);
    } else {
        g_free(ev);
    }
}

static void wifi_post_event(uint32_t base, int32_t id)
{
    if (!W.p_event_post || !base) {
        return;
    }
    EvPost *ev = g_malloc0(sizeof(*ev));
    ev->base = base;
    ev->id = id;
    ev->remaining = 1;
    ev_post_done(0, 0, ev);
}

/* ------------------------------------------------------- config (JSON) */

static void json_get_str(const char *json, const char *key, char *out, int outsz)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return;
    }
    p = strchr(p + strlen(pat), ':');
    if (!p) {
        return;
    }
    p = strchr(p, '"');
    if (!p) {
        return;
    }
    p++;
    const char *e = strchr(p, '"');
    if (!e) {
        return;
    }
    int n = e - p;
    if (n >= outsz) {
        n = outsz - 1;
    }
    memcpy(out, p, n);
    out[n] = 0;
}

static int json_get_int(const char *json, const char *key, int defv)
{
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (!p) {
        return defv;
    }
    p = strchr(p + strlen(pat), ':');
    return p ? atoi(p + 1) : defv;
}

static void wifi_parse_config(const char *json)
{
    W.ap_count = 0;
    const char *p = strstr(json, "\"aps\"");
    if (!p) {
        return;
    }
    while (W.ap_count < 16 && (p = strchr(p, '{')) != NULL) {
        const char *end = strchr(p, '}');
        if (!end) {
            break;
        }
        char chunk[256];
        int n = end - p + 1;
        if (n >= (int)sizeof(chunk)) {
            n = sizeof(chunk) - 1;
        }
        memcpy(chunk, p, n);
        chunk[n] = 0;
        json_get_str(chunk, "ssid", (char *)W.aps[W.ap_count].ssid, 33);
        char bssid[24] = "";
        json_get_str(chunk, "bssid", bssid, sizeof(bssid));
        unsigned b[6] = { 0x02, 0, 0, 0, 0, (unsigned)W.ap_count };
        sscanf(bssid, "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
        for (int i = 0; i < 6; i++) {
            W.aps[W.ap_count].bssid[i] = b[i];
        }
        W.aps[W.ap_count].rssi = json_get_int(chunk, "rssi", -50);
        W.aps[W.ap_count].auth = json_get_int(chunk, "auth", 3);
        W.aps[W.ap_count].ch = json_get_int(chunk, "ch", 6);
        W.ap_count++;
        p = end + 1;
    }
}

void espplay_wifi_config_updated(EspplayBridgeState *s)
{
    wifi_parse_config(s->wifi_json);
    espplay_log("wifi config: %d APs", W.ap_count);
}

void espplay_wifi_connect_result(int ok, int8_t rssi, int channel,
                                 const uint8_t *bssid, int authmode)
{
    if (ok) {
        W.connected = true;
        W.connecting = false;
        W.rssi = rssi;
        W.channel = channel;
        W.authmode = authmode;
        if (bssid) {
            memcpy(W.bssid, bssid, 6);
        }
        wifi_post_event(W.ev_base_wifi, WIFI_EVENT_STA_CONNECTED);
    } else {
        W.connected = false;
        W.connecting = false;
        wifi_post_event(W.ev_base_wifi, WIFI_EVENT_STA_DISCONNECTED);
    }
}

void espplay_wifi_sim_event(int type, int reason)
{
    if (type == 1) {
        W.connected = false;
        wifi_post_event(W.ev_base_wifi, WIFI_EVENT_STA_DISCONNECTED);
    } else if (type == 2) {
        wifi_post_event(W.ev_base_wifi, WIFI_EVENT_STA_START);
    }
}

/* --------------------------------------------------------- frame path */

typedef struct {
    uint8_t *data;
    int len;
} RxCtx;

void espplay_wifi_rx_call_done(uint32_t r0, uint32_t r1, void *ctx)
{
    g_free(ctx);
}

void espplay_wifi_rx_malloc_done(uint32_t r0, uint32_t r1, void *ctx)
{
    RxCtx *c = ctx;
    CPUState *cpu = qemu_get_cpu(0);
    if (r0 == 0 || !cpu) {
        g_free(c);
        return;
    }
    wifi_mem_write(cpu, r0, c->data, c->len);
    uint32_t args[3] = { r0, (uint32_t)c->len, r0 };
    espplay_qcall_request(W.rxcb, args, 3, espplay_wifi_rx_call_done, c);
}

void espplay_wifi_frame_rx(const uint8_t *buf, size_t size)
{
    if (!W.rxcb || !W.connected || espplay_qcall_active()) {
        return;
    }
    uint32_t malloc_addr = 0;
    if (!espplay_sym_lookup("malloc", &malloc_addr)) {
        return;
    }
    RxCtx *c = g_malloc(sizeof(*c) + size);
    c->data = (uint8_t *)(c + 1);
    c->len = size;
    memcpy(c->data, buf, size);
    uint32_t args[1] = { (uint32_t)size };
    espplay_qcall_request(malloc_addr, args, 1, espplay_wifi_rx_malloc_done, c);
}

/* ------------------------------------------------------- hook handlers */

void espplay_wifi_send_connect_req(const uint8_t *p, int n)
{
    espplay_send_msg(espplay_bridge_get(), ESPPLAY_MSG_WIFI_CONNECT_REQ, p, n);
}

bool espplay_hook_wifi_fn(CPUState *cpu, uint32_t pc, const char *sym)
{
    if (strcmp(sym, "esp_wifi_init") == 0) {
        W.inited = true;
        uint32_t a;
        if (espplay_sym_lookup("WIFI_EVENT", &a)) {
            wifi_mem_read(cpu, a, &W.ev_base_wifi, 4);
        }
        if (espplay_sym_lookup("IP_EVENT", &a)) {
            wifi_mem_read(cpu, a, &W.ev_base_ip, 4);
        }
        espplay_sym_lookup("esp_event_post", &W.p_event_post);
        if (!W.mac[0] && !W.mac[5]) {
            W.mac[0] = 0x52; W.mac[1] = 0x54; W.mac[2] = 0x00;
            W.mac[3] = 0x12; W.mac[4] = 0x34; W.mac[5] = 0x56;
        }
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_deinit") == 0) {
        W.inited = false;
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_set_mode") == 0) {
        W.mode = espplay_get_arg(cpu, 0);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_get_mode") == 0) {
        int mode = W.mode;
        wifi_mem_write(cpu, espplay_get_arg(cpu, 0), &mode, 4);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_set_config") == 0) {
        int ifx = espplay_get_arg(cpu, 0);
        uint32_t cfg = espplay_get_arg(cpu, 1);
        if (ifx == 0) {
            uint8_t buf[96];
            wifi_mem_read(cpu, cfg, buf, sizeof(buf));
            memcpy(W.sta_ssid, buf, 32);
            W.sta_ssid[32] = 0;
            memcpy(W.sta_pass, buf + 32, 64);
            W.sta_pass[64] = 0;
            W.sta_configured = true;
        }
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_get_config") == 0) {
        int ifx = espplay_get_arg(cpu, 0);
        uint32_t cfg = espplay_get_arg(cpu, 1);
        if (ifx == 0) {
            uint8_t buf[96] = { 0 };
            memcpy(buf, W.sta_ssid, 32);
            memcpy(buf + 32, W.sta_pass, 64);
            wifi_mem_write(cpu, cfg, buf, sizeof(buf));
        }
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_start") == 0) {
        W.started = true;
        espplay_set_ret(cpu, ESP_OK, 0);
        wifi_post_event(W.ev_base_wifi, WIFI_EVENT_STA_START);
        return true;
    }
    if (strcmp(sym, "esp_wifi_stop") == 0) {
        W.started = false;
        W.connected = false;
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_connect") == 0) {
        W.connecting = true;
        uint8_t msg[2 + 33 + 66] = { 0 };
        int sl = strlen((char *)W.sta_ssid);
        int pl = strlen((char *)W.sta_pass);
        msg[0] = sl;
        memcpy(msg + 1, W.sta_ssid, sl);
        msg[1 + sl] = pl;
        memcpy(msg + 2 + sl, W.sta_pass, pl);
        espplay_wifi_send_connect_req(msg, 2 + sl + pl);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_disconnect") == 0) {
        W.connected = false;
        wifi_post_event(W.ev_base_wifi, WIFI_EVENT_STA_DISCONNECTED);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_scan_start") == 0) {
        espplay_set_ret(cpu, ESP_OK, 0);
        wifi_post_event(W.ev_base_wifi, WIFI_EVENT_SCAN_DONE);
        return true;
    }
    if (strcmp(sym, "esp_wifi_scan_stop") == 0) {
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_scan_get_ap_num") == 0) {
        uint32_t n = W.ap_count;
        wifi_mem_write(cpu, espplay_get_arg(cpu, 0), &n, 4);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_scan_get_ap_records") == 0) {
        uint32_t pn = espplay_get_arg(cpu, 0);
        uint32_t recs = espplay_get_arg(cpu, 1);
        uint32_t n = 0;
        wifi_mem_read(cpu, pn, &n, 4);
        if (n > (uint32_t)W.ap_count) {
            n = W.ap_count;
        }
        for (uint32_t i = 0; i < n; i++) {
            uint8_t rec[64] = { 0 };
            memcpy(rec, W.aps[i].bssid, 6);
            memcpy(rec + 6, W.aps[i].ssid, 32);
            rec[39] = W.aps[i].ch;
            int sec = 0;
            memcpy(rec + 40, &sec, 4);
            rec[44] = (uint8_t)W.aps[i].rssi;
            int auth = W.aps[i].auth;
            memcpy(rec + 48, &auth, 4);
            wifi_mem_write(cpu, recs + i * 112, rec, sizeof(rec));
        }
        wifi_mem_write(cpu, pn, &n, 4);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_sta_get_ap_info") == 0) {
        uint32_t rec = espplay_get_arg(cpu, 0);
        uint8_t buf[64] = { 0 };
        memcpy(buf, W.bssid, 6);
        memcpy(buf + 6, W.sta_ssid, 32);
        buf[39] = W.channel;
        int sec = 0;
        memcpy(buf + 40, &sec, 4);
        buf[44] = (uint8_t)W.rssi;
        int auth = W.authmode;
        memcpy(buf + 48, &auth, 4);
        wifi_mem_write(cpu, rec, buf, sizeof(buf));
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_get_ip_info") == 0) {
        uint32_t info[3] = {
            W.connected ? 0x0F02000A : 0,
            W.connected ? 0x00FFFFFF : 0,
            W.connected ? 0x0202000A : 0,
        };
        wifi_mem_write(cpu, espplay_get_arg(cpu, 1), info, sizeof(info));
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_set_ps") == 0 ||
        strcmp(sym, "esp_wifi_set_max_tx_power") == 0 ||
        strcmp(sym, "esp_wifi_set_channel") == 0 ||
        strcmp(sym, "esp_wifi_set_log_level") == 0 ||
        strcmp(sym, "esp_wifi_internal_set_log_level") == 0 ||
        strcmp(sym, "esp_wifi_set_mac") == 0 ||
        strcmp(sym, "esp_wifi_internal_update_ap_info") == 0 ||
        strcmp(sym, "esp_wifi_internal_set_sta_config") == 0) {
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_get_mac") == 0) {
        wifi_mem_write(cpu, espplay_get_arg(cpu, 1), W.mac, 6);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_internal_tx") == 0) {
        uint32_t buf = espplay_get_arg(cpu, 1);
        int len = espplay_get_arg(cpu, 2);
        if (len > 0 && len <= 1600) {
            uint8_t frame[1600];
            wifi_mem_read(cpu, buf, frame, len);
            extern void espplay_wifi_frame_tx(const uint8_t *buf, size_t size);
            espplay_wifi_frame_tx(frame, len);
        }
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_internal_reg_rxcb") == 0) {
        W.rxcb = espplay_get_arg(cpu, 1);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_internal_free_rx_buffer") == 0) {
        uint32_t eb = espplay_get_arg(cpu, 0);
        if (eb) {
            uint32_t free_addr = 0;
            if (espplay_sym_lookup("free", &free_addr)) {
                uint32_t args[1] = { eb };
                espplay_qcall_request(free_addr, args, 1, NULL, NULL);
            }
        }
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    if (strcmp(sym, "esp_wifi_ap_get_sta_list") == 0) {
        uint32_t p = espplay_get_arg(cpu, 0);
        uint32_t zero = 0;
        wifi_mem_write(cpu, p, &zero, 4);
        espplay_set_ret(cpu, ESP_OK, 0);
        return true;
    }
    return false;
}
