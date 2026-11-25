#!/usr/bin/env python3
"""Exercise the patched QEMU SPI byte loop without requiring a QEMU build.

Usage: python3 tests/test_spi_transfer.py [path/to/patched/esp32_spi.c]
The default path is produced by scripts/build-qemu.sh.
"""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'work/espressif-qemu/hw/ssi/esp32_spi.c'
text = source.read_text()
start = text.index('static void esp32_spi_txrx_buffer(')
end = text.index('\nstatic void esp32_spi_cs_set(', start)
function = text[start:end]
harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define MAX(a,b) ((a) > (b) ? (a) : (b))
typedef struct { int bus_nr; void *spi; } Esp32SpiState;
static uint8_t sent[16];
static int count;
static void espplay_spi_slave_xfer(int bus, uint8_t byte) {
    assert(bus == 2); sent[count++] = byte;
}
static bool espplay_touch_xfer(int bus, uint8_t byte, uint8_t *out) {
    (void)bus; (void)byte; (void)out; return false;
}
static uint32_t ssi_transfer(void *bus, uint8_t byte) {
    (void)bus; return byte ^ 0xa5;
}
''' + function + r'''
int main(void) {
    Esp32SpiState s = { .bus_nr = 2 };
    uint8_t buf[] = { 0xfe, 0x01, 0x80, 0xcc, 0xdd };
    esp32_spi_txrx_buffer(&s, buf, 3, 2);
    assert(count == 3 && sent[0] == 0xfe && sent[1] == 0x01 && sent[2] == 0x80);
    assert(buf[0] == (0xfe ^ 0xa5) && buf[1] == (0x01 ^ 0xa5));
    assert(buf[2] == 0x80 && buf[3] == 0xcc && buf[4] == 0xdd);
    count = 0;
    uint8_t rx[] = {0x34, 0x56, 0xcc};
    esp32_spi_txrx_buffer(&s, rx, 0, 2);
    assert(count == 2 && sent[0] == 0 && sent[1] == 0);
    assert(rx[0] == 0xa5 && rx[1] == 0xa5 && rx[2] == 0xcc);
    count = 0;
    uint8_t full[] = {0xff, 0x80, 0x00, 0xdd};
    esp32_spi_txrx_buffer(&s, full, 3, 3);
    assert(count == 3 && sent[0] == 0xff && sent[1] == 0x80 && sent[2] == 0);
    assert(full[0] == 0x5a && full[1] == 0x25 && full[2] == 0xa5 && full[3] == 0xdd);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('SPI transfer TX/RX bounds and full-duplex tests passed')
