#!/usr/bin/env python3
"""Unit-test the actual ESP32 SPI DMA descriptor walker using fake guest RAM."""
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'work/espressif-qemu/hw/ssi/esp32_spi.c'
text = source.read_text()
start = text.index('#define ESPPLAY_SPI_DMA_MAX')
end = text.index('\nstatic void esp32_spi_transaction(', start)
walker = text[start:end]
harness = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#define MIN(a,b) ((a) < (b) ? (a) : (b))
static uint8_t ram[1024 * 1024];
static void cpu_physical_memory_read(uint32_t addr, void *data, size_t len) {
    assert(addr >= 0x3ff00000 && (uint64_t)addr + len <= 0x40000000);
    memcpy(data, ram + addr - 0x3ff00000, len);
}
static void cpu_physical_memory_write(uint32_t addr, const void *data, size_t len) {
    assert(addr >= 0x3ff00000 && (uint64_t)addr + len <= 0x40000000);
    memcpy(ram + addr - 0x3ff00000, data, len);
}
static uint32_t ldl_le_p(const void *p) {
    const uint8_t *b = p; return b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void stl_le_p(void *p, uint32_t n) {
    uint8_t *b = p; b[0] = n; b[1] = n >> 8; b[2] = n >> 16; b[3] = n >> 24;
}
static void desc(int off, uint32_t flags, uint32_t buf, uint32_t next) {
    stl_le_p(ram + off, flags); stl_le_p(ram + off + 4, buf); stl_le_p(ram + off + 8, next);
}
''' + walker + r'''
int main(void) {
    uint8_t bytes[10] = {0};
    memcpy(ram + 0x200, "abc", 3); memcpy(ram + 0x300, "DEFG", 4);
    desc(0x100, 0x80000000 | (3 << 12) | 3, 0x3ff00200, 0x3ff0010c);
    desc(0x10c, 0xc0000000 | (4 << 12) | 4, 0x3ff00300, 0);
    esp32_spi_dma_copy(0x100, bytes, 7, false);
    assert(!memcmp(bytes, "abcDEFG", 7));
    /* RX spans two descriptors and returns ownership and length. */
    desc(0x100, 0x80000000 | 3, 0x3ff00400, 0x3ff0010c);
    desc(0x10c, 0x80000000 | 6, 0x3ff00500, 0);
    esp32_spi_dma_copy(0x100, bytes, 7, true);
    assert(!memcmp(ram + 0x400, "abc", 3));
    assert(!memcmp(ram + 0x500, "DEFG", 4));
    assert(ldl_le_p(ram + 0x100) == ((3 << 12) | 3));
    assert(ldl_le_p(ram + 0x10c) == (0x40000000 | (4 << 12) | 6));
    /* Unowned or malformed descriptors must not access their buffer. */
    desc(0x100, 3 | (3 << 12), 0x3ff00200, 0);
    memset(bytes, 0, sizeof(bytes));
    esp32_spi_dma_copy(0x100, bytes, 7, false);
    assert(bytes[0] == 0);
    desc(0x100, 0x80000000 | 2 | (3 << 12), 0x3ff00200, 0);
    esp32_spi_dma_copy(0x100, bytes, 7, false);
    assert(bytes[0] == 0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = pathlib.Path(tmp)
    (path / 'test.c').write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(path / 'test.c'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('SPI DMA descriptor chain TX/RX tests passed')
