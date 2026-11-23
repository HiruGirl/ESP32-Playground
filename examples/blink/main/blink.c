/* blink — toggle the LED on GPIO2 every 500 ms. */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define LED_GPIO 2

void app_main(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
    printf("blink ready\n");
    int level = 0;
    while (1) {
        level ^= 1;
        gpio_set_level(LED_GPIO, level);
        printf("LED %s\n", level ? "ON" : "OFF");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
