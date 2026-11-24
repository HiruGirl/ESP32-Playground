/* button_led — the button on GPIO0 drives the LED on GPIO2.
 * GPIO0 is read with the internal pull-up; pressing the button pulls it
 * low (active low). The LED follows the button state (on while pressed). */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#define BUTTON_GPIO 0
#define LED_GPIO 2

void app_main(void)
{
    gpio_config_t out = {
        .pin_bit_mask = 1ULL << LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&out);
    gpio_config_t in = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&in);
    printf("button_led ready\n");
    int last = -1;
    while (1) {
        int pressed = gpio_get_level(BUTTON_GPIO) == 0;
        if (pressed != last) {
            gpio_set_level(LED_GPIO, pressed);
            printf("button %s, LED %s\n", pressed ? "DOWN" : "UP",
                   pressed ? "ON" : "OFF");
            last = pressed;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
