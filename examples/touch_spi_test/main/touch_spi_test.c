#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
static spi_device_handle_t dev;
static uint8_t transfer(uint8_t byte) {
    spi_transaction_t t = { .length = 8, .rxlength = 8, .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA };
    t.tx_data[0] = byte;
    ESP_ERROR_CHECK(spi_device_polling_transmit(dev, &t));
    return t.rx_data[0];
}
static unsigned sample(uint8_t command) {
    gpio_set_level(GPIO_NUM_33, 0);
    transfer(command);
    unsigned hi = transfer(0);
    unsigned lo = transfer(0);
    gpio_set_level(GPIO_NUM_33, 1);
    return ((hi << 8) | lo) >> 3;
}
void app_main(void) {
    gpio_set_direction(GPIO_NUM_33, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_33, 1);
    gpio_set_direction(GPIO_NUM_36, GPIO_MODE_INPUT);
    spi_bus_config_t bus = { .mosi_io_num = 32, .miso_io_num = 39, .sclk_io_num = 25,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 64 };
    spi_device_interface_config_t cfg = { .clock_speed_hz = 1000000, .spics_io_num = -1, .queue_size = 1 };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, 0));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &cfg, &dev));
    for (;;) {
        unsigned x = sample(0x90), y = sample(0xd0);
        gpio_set_level(GPIO_NUM_33, 0);
        transfer(0xb1);
        unsigned z1 = (unsigned)transfer(0) << 8; z1 |= transfer(0xc1); z1 >>= 3;
        unsigned z2 = (unsigned)transfer(0) << 8; z2 |= transfer(0x91); z2 >>= 3;
        unsigned px = (unsigned)transfer(0) << 8; px |= transfer(0); px >>= 3;
        gpio_set_level(GPIO_NUM_33, 1);
        printf("TOUCH irq=%d x=%u y=%u z1=%u z2=%u px=%u\n", gpio_get_level(GPIO_NUM_36), x, y, z1, z2, px);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
