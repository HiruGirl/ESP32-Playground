/* mqtt_publisher — connects to WiFi, then publishes temperature readings to
 * an MQTT broker every 2 seconds (topic esp32playground/temperature). */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#define WIFI_SSID "ESP32Playground"
#define WIFI_PASS "playground123"
#define BROKER_URI "mqtt://10.0.2.2:1883"

static EventGroupHandle_t s_events;
#define CONNECTED_BIT BIT0

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, CONNECTED_BIT);
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_mqtt_event_handle_t e = data;
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        printf("MQTT connected\n");
        break;
    case MQTT_EVENT_DISCONNECTED:
        printf("MQTT disconnected\n");
        break;
    case MQTT_EVENT_PUBLISHED:
        printf("MQTT published msg_id=%d\n", e->msg_id);
        break;
    default:
        break;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL));
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    wifi_config_t wc = {0};
    strcpy((char *)wc.sta.ssid, WIFI_SSID);
    strcpy((char *)wc.sta.password, WIFI_PASS);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    printf("mqtt_publisher ready\n");
    xEventGroupWaitBits(s_events, CONNECTED_BIT, false, true, portMAX_DELAY);

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = BROKER_URI;
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    float temp = 25.0f;
    while (1) {
        char payload[64];
        snprintf(payload, sizeof(payload), "{\"temperature\":%.2f}", temp);
        int msg_id = esp_mqtt_client_publish(client, "esp32playground/temperature", payload, 0, 1, 0);
        printf("publish queued msg_id=%d payload=%s\n", msg_id, payload);
        temp += 0.5f;
        if (temp > 35.0f) temp = 25.0f;
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
