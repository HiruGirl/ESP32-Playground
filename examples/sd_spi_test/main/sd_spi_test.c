/* End-to-end, single-block SPI SD smoke test on the CYD VSPI wiring. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

static spi_device_handle_t dev;
static uint8_t xfer(uint8_t b) {
    spi_transaction_t t = { .length = 8, .rxlength = 8, .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA };
    t.tx_data[0] = b;
    ESP_ERROR_CHECK(spi_device_polling_transmit(dev, &t));
    return t.rx_data[0];
}
static void select_card(int yes) { gpio_set_level(GPIO_NUM_5, !yes); }
static uint8_t cmd(uint8_t c, uint32_t arg, uint8_t crc) {
    select_card(1);
    xfer(0xff); xfer(0x40 | c); xfer(arg >> 24); xfer(arg >> 16); xfer(arg >> 8); xfer(arg); xfer(crc);
    for (int i = 0; i < 16; i++) { uint8_t r = xfer(0xff); if (!(r & 0x80)) return r; }
    return 0xff;
}
static void end(void) { select_card(0); xfer(0xff); }
void app_main(void) {
    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT); select_card(0);
    spi_bus_config_t bus = { .mosi_io_num = 23, .miso_io_num = 19, .sclk_io_num = 18,
        .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = 64 };
    spi_device_interface_config_t cfg = { .clock_speed_hz = 400000, .spics_io_num = -1, .queue_size = 1 };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, 0));
    ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &cfg, &dev));
    for (int i = 0; i < 12; i++) xfer(0xff);
    uint8_t r = cmd(0, 0, 0x95); printf("CMD0=%02x\n", r); end();
    r = cmd(8, 0x1aa, 0x87); printf("CMD8=%02x", r);
    for (int i = 0; i < 4; i++) { printf(" %02x", xfer(0xff)); }
    puts(""); end();
    for (int i = 0; i < 100; i++) {
        cmd(55, 0, 0xff); end(); r = cmd(41, 0x40000000, 0xff); end();
        if (r == 0) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    printf("ACMD41=%02x\n", r);
    if (r != 0) return;
    r = cmd(17, 0, 0xff); printf("CMD17=%02x\n", r);
    uint8_t token = 0xff;
    for (int i = 0; i < 1000 && token == 0xff; i++) token = xfer(0xff);
    printf("TOKEN=%02x\n", token);
    unsigned mismatch = 0;
    for (int i = 0; i < 512; i++) { uint8_t b = xfer(0xff); if (b != (uint8_t)i) mismatch++; }
    xfer(0xff); xfer(0xff); end();
    printf("SD_READ_MISMATCH=%u\n", mismatch);
    if (r != 0 || token != 0xfe || mismatch) return;
    puts("SD_SPI_READ_PASS");
    r = cmd(24, 0, 0xff); printf("CMD24=%02x\n", r);
    xfer(0xff); xfer(0xfe);
    for (int i = 0; i < 512; i++) xfer((uint8_t)(255 - i));
    xfer(0xff); xfer(0xff);
    uint8_t result = xfer(0xff);
    printf("WRITE_RESPONSE=%02x\n", result);
    end();
    if (r != 0 || (result & 0x1f) != 5) return;
    r = cmd(17, 0, 0xff);
    token = 0xff;
    for (int i = 0; i < 1000 && token == 0xff; i++) token = xfer(0xff);
    mismatch = 0;
    for (int i = 0; i < 512; i++) if (xfer(0xff) != (uint8_t)(255 - i)) mismatch++;
    xfer(0xff); xfer(0xff); end();
    printf("SD_WRITE_MISMATCH=%u\n", mismatch);
    if (r == 0 && token == 0xfe && !mismatch) puts("SD_SPI_WRITE_PASS");
}
