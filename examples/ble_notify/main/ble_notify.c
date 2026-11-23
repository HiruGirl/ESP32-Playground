/* ble_notify — BLE peripheral with a notify characteristic that sends an
 * incrementing counter every second. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);

static const ble_uuid128_t SVC_UUID = BLE_UUID128_INIT(
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01);
static const ble_uuid128_t CHR_UUID = BLE_UUID128_INIT(
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x02);

static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_val_handle;
static uint32_t s_counter;

static int gatt_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t buf[4] = { s_counter & 0xFF, (s_counter >> 8) & 0xFF,
                           (s_counter >> 16) & 0xFF, (s_counter >> 24) & 0xFF };
        int rc = os_mbuf_append(ctxt->om, buf, sizeof(buf));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (const ble_uuid_t *)&SVC_UUID,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = (const ble_uuid_t *)&CHR_UUID,
                .access_cb = gatt_access,
                .val_handle = &s_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },
        },
    },
    { 0 },
};

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN;
    fields.name = (uint8_t *)"ESP32-NOTIFY";
    fields.name_len = strlen("ESP32-NOTIFY");
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);
    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
    printf("advertising as ESP32-NOTIFY\n");
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            printf("connected handle=%d\n", s_conn);
        } else {
            advertise();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        advertise();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        return 0;
    }
    return 0;
}

static void notify_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        s_counter++;
        if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
            uint8_t buf[4] = { s_counter & 0xFF, (s_counter >> 8) & 0xFF,
                               (s_counter >> 16) & 0xFF, (s_counter >> 24) & 0xFF };
            struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
            ble_gattc_notify_custom(s_conn, s_val_handle, om);
            printf("notified counter=%u\n", (unsigned)s_counter);
        }
    }
}

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, NULL);
    advertise();
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());
    nimble_port_init();
    ble_store_config_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    xTaskCreate(notify_task, "notify", 4096, NULL, 5, NULL);
    printf("ble_notify ready\n");
}
