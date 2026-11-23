/* pot_servo — the potentiometer on GPIO34 (ADC1 channel 6) controls a
 * hobby servo on GPIO18 (LEDC, 50 Hz, 500-2500 us pulse). */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/ledc.h"

#define POT_ADC_CHANNEL ADC1_CHANNEL_6 /* GPIO34 */
#define SERVO_GPIO 18
#define SERVO_FREQ_HZ 50
#define SERVO_MIN_US 500
#define SERVO_MAX_US 2500

void app_main(void)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(POT_ADC_CHANNEL, ADC_ATTEN_DB_11);

    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_16_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);
    ledc_channel_config_t ch = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ledc_channel_config(&ch);
    printf("pot_servo ready\n");
    int last_us = -1;
    while (1) {
        int raw = adc1_get_raw(POT_ADC_CHANNEL);
        int us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * raw / 4095;
        if (us != last_us) {
            /* duty in 16-bit ticks of a 20 ms period */
            uint32_t duty = (uint32_t)us * 65535 / 20000;
            ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
            ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
            printf("pot=%d servo=%dus\n", raw, us);
            last_us = us;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
