/* wifi_scan — scan for WiFi networks and print results. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static void wifi_scan_task(void *arg)
{
    wifi_scan_config_t cfg = {0};
    ESP_ERROR_CHECK(esp_wifi_scan_start(&cfg, true));
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    printf("found %d APs\n", n);
    wifi_ap_record_t *aps = malloc(sizeof(wifi_ap_record_t) * (n ? n : 1));
    uint16_t max = n;
    esp_wifi_scan_get_ap_records(&max, aps);
    for (int i = 0; i < max; i++) {
        printf("AP %d: ssid=%s rssi=%d auth=%d ch=%d\n", i, aps[i].ssid, aps[i].rssi,
               aps[i].authmode, aps[i].primary);
    }
    free(aps);
    printf("WIFI_SCAN_DONE\n");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    printf("wifi_scan ready\n");
    xTaskCreate(wifi_scan_task, "scan", 4096, NULL, 5, NULL);
}
