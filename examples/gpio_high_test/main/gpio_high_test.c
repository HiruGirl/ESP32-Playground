#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

void app_main(void)
{
    gpio_set_direction(GPIO_NUM_33, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_36, GPIO_MODE_INPUT);
    for (;;) {
        gpio_set_level(GPIO_NUM_33, 1);
        printf("GPIO33 high GPIO36=%d\n", gpio_get_level(GPIO_NUM_36));
        vTaskDelay(pdMS_TO_TICKS(500));
        gpio_set_level(GPIO_NUM_33, 0);
        printf("GPIO33 low GPIO36=%d\n", gpio_get_level(GPIO_NUM_36));
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
