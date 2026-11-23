/* wifi_webserver_led — softAP web server that toggles the LED on GPIO2.
 * AP: SSID "ESP32Playground-AP", password "playground123" (WPA2). */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

#define LED_GPIO 2
static int s_led_on = 0;

static esp_err_t root_get(httpd_req_t *req)
{
    char page[512];
    snprintf(page, sizeof(page),
        "<html><body><h1>ESP32 Playground</h1>"
        "<p>LED is %s</p>"
        "<form action='/toggle'><button>Toggle LED</button></form>"
        "</body></html>", s_led_on ? "ON" : "OFF");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t toggle_get(httpd_req_t *req)
{
    s_led_on = !s_led_on;
    gpio_set_level(LED_GPIO, s_led_on);
    printf("LED toggled -> %d\n", s_led_on);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = data;
        printf("client connected, mac=%02x:%02x:%02x:%02x:%02x:%02x\n",
               e->mac[0], e->mac[1], e->mac[2], e->mac[3], e->mac[4], e->mac[5]);
    }
}

void app_main(void)
{
    gpio_config_t io = { .pin_bit_mask = 1ULL << LED_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&io);
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL));
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    wifi_config_t wc = {0};
    strcpy((char *)wc.ap.ssid, "ESP32Playground-AP");
    strcpy((char *)wc.ap.password, "playground123");
    wc.ap.ssid_len = strlen("ESP32Playground-AP");
    wc.ap.channel = 6;
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wc.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_start(&server, &cfg);
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t tog = { .uri = "/toggle", .method = HTTP_GET, .handler = toggle_get };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &tog);
    printf("wifi_webserver_led ready\n");
}
