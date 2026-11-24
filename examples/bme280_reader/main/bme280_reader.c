/* bme280_reader — read temperature, humidity and pressure from a BME280
 * on I2C address 0x76 (SDA=GPIO21, SCL=GPIO22) using the Bosch
 * fixed-point compensation formulas from the datasheet. */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA 21
#define I2C_SCL 22
#define BME280_ADDR 0x76

static void reg_read(uint8_t reg, uint8_t *buf, int len)
{
    i2c_master_write_read_device(I2C_PORT, BME280_ADDR, &reg, 1, buf, len,
                                 pdMS_TO_TICKS(100));
}

static uint8_t reg_read8(uint8_t reg)
{
    uint8_t v = 0;
    reg_read(reg, &v, 1);
    return v;
}

static void reg_write8(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    i2c_master_write_to_device(I2C_PORT, BME280_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

static int16_t le16s(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }
static uint16_t le16u(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void app_main(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_PORT, &cfg);
    i2c_driver_install(I2C_PORT, cfg.mode, 0, 0, 0);

    uint8_t id = reg_read8(0xD0);
    printf("bme280 chip id: 0x%02X\n", id);
    if (id != 0x60) {
        printf("unexpected chip id, continuing anyway\n");
    }

    uint8_t cal[32];
    reg_read(0x88, cal, 26);
    uint16_t dig_T1 = le16u(&cal[0]);
    int16_t dig_T2 = le16s(&cal[2]);
    int16_t dig_T3 = le16s(&cal[4]);
    uint16_t dig_P1 = le16u(&cal[6]);
    int16_t dig_P2 = le16s(&cal[8]);
    int16_t dig_P3 = le16s(&cal[10]);
    int16_t dig_P4 = le16s(&cal[12]);
    int16_t dig_P5 = le16s(&cal[14]);
    int16_t dig_P6 = le16s(&cal[16]);
    int16_t dig_P7 = le16s(&cal[18]);
    int16_t dig_P8 = le16s(&cal[20]);
    int16_t dig_P9 = le16s(&cal[22]);
    uint8_t dig_H1 = cal[25];
    uint8_t hcal[7];
    reg_read(0xE1, hcal, 7);
    int16_t dig_H2 = le16s(&hcal[0]);
    uint8_t dig_H3 = hcal[2];
    int16_t dig_H4 = (int16_t)((hcal[3] << 4) | (hcal[4] & 0x0F));
    int16_t dig_H5 = (int16_t)((hcal[5] << 4) | (hcal[4] >> 4));
    int8_t dig_H6 = (int8_t)hcal[6];

    /* humidity x1 oversampling, temp x1, press x1, normal mode */
    reg_write8(0xF2, 0x01);
    reg_write8(0xF4, 0x27);
    reg_write8(0xF5, 0xA0);
    printf("bme280_reader ready\n");

    while (1) {
        uint8_t d[8];
        reg_read(0xF7, d, 8);
        int32_t adc_P = (int32_t)((d[0] << 12) | (d[1] << 4) | (d[2] >> 4));
        int32_t adc_T = (int32_t)((d[3] << 12) | (d[4] << 4) | (d[5] >> 4));
        int32_t adc_H = (int32_t)((d[6] << 8) | d[7]);

        /* temperature (datasheet 4.2.3) */
        int32_t var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) *
                        ((int32_t)dig_T2)) >> 11;
        int32_t var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
                          ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
                        ((int32_t)dig_T3)) >> 14;
        int32_t t_fine = var1 + var2;
        int32_t temp_c_x100 = (t_fine * 5 + 128) >> 8;

        /* pressure (datasheet 4.2.3), returns Pa */
        int64_t pvar1 = ((int64_t)t_fine) - 128000;
        int64_t pvar2 = pvar1 * pvar1 * (int64_t)dig_P6;
        pvar2 = pvar2 + ((pvar1 * (int64_t)dig_P5) << 17);
        pvar2 = pvar2 + (((int64_t)dig_P4) << 35);
        pvar1 = ((pvar1 * pvar1 * (int64_t)dig_P3) >> 8) +
                ((pvar1 * (int64_t)dig_P2) << 12);
        pvar1 = (((((int64_t)1) << 47) + pvar1)) * ((int64_t)dig_P1) >> 33;
        int64_t press_pa = 0;
        if (pvar1 != 0) {
            press_pa = 1048576 - adc_P;
            press_pa = (((press_pa << 31) - pvar2) * 3125) / pvar1;
            pvar1 = (((int64_t)dig_P9) * (press_pa >> 13) *
                     (press_pa >> 13)) >> 25;
            pvar2 = (((int64_t)dig_P8) * press_pa) >> 19;
            press_pa = ((press_pa + pvar1 + pvar2) >> 8) +
                       (((int64_t)dig_P7) << 4);
        }

        /* humidity (datasheet 4.2.4), returns %RH x1024 */
        int32_t v_x1 = t_fine - ((int32_t)76800);
        v_x1 = (((((adc_H << 14) - (((int32_t)dig_H4) << 20) -
                   (((int32_t)dig_H5) * v_x1)) + ((int32_t)16384)) >> 15) *
                (((((((v_x1 * ((int32_t)dig_H6)) >> 10) *
                     (((v_x1 * ((int32_t)dig_H3)) >> 11) + ((int32_t)32768))) >> 10) +
                   ((int32_t)2097152)) * ((int32_t)dig_H2) + 8192) >> 14));
        v_x1 = (v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) *
                         ((int32_t)dig_H1)) >> 4));
        v_x1 = v_x1 < 0 ? 0 : v_x1;
        v_x1 = v_x1 > 419430400 ? 419430400 : v_x1;
        int32_t hum_x1024 = v_x1 >> 12;

        printf("temp=%.2fC hum=%.1f%% press=%.2fhPa\n",
               temp_c_x100 / 100.0, hum_x1024 / 1024.0, press_pa / 100.0);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
