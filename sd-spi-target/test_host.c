/*
 * test_host.c - native simulator of an SD-SPI *host* (3D-printer / MCU class),
 * driving sd_spi_target to verify the protocol end to end.
 *
 * Build & run:  make test   (or: cc -Wall -Wextra test_host.c sd_spi_target.c && ./a.out)
 */
#include "sd_spi_target.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ---- in-RAM backing store ---------------------------------------------- */
#define NBLOCKS 64
static uint8_t g_disk[NBLOCKS][SD_BLOCK_SIZE];

static int disk_read(void *u, uint32_t blk, uint8_t buf[SD_BLOCK_SIZE])
{
    (void)u;
    if (blk >= NBLOCKS) return -1;
    memcpy(buf, g_disk[blk], SD_BLOCK_SIZE);
    return 0;
}
static int disk_write(void *u, uint32_t blk, const uint8_t buf[SD_BLOCK_SIZE])
{
    (void)u;
    if (blk >= NBLOCKS) return -1;
    memcpy(g_disk[blk], buf, SD_BLOCK_SIZE);
    return 0;
}

/* ---- host-side SPI primitives ------------------------------------------ */
static sd_spi_target_t T;

static uint8_t xfer(uint8_t b) { return sd_spi_target_step(&T, b); }

static void clocks(int n) { while (n--) (void)xfer(0xFF); }

/* Send a 6-byte command frame with a correct CRC7. */
static void send_cmd(uint8_t idx, uint32_t arg)
{
    uint8_t f[5];
    f[0] = (uint8_t)(0x40 | idx);
    f[1] = (uint8_t)(arg >> 24);
    f[2] = (uint8_t)(arg >> 16);
    f[3] = (uint8_t)(arg >> 8);
    f[4] = (uint8_t)(arg);
    uint8_t crc = (uint8_t)((sd_crc7(f, 5) << 1) | 1);
    for (int i = 0; i < 5; i++) (void)xfer(f[i]);
    (void)xfer(crc);
}

/* Read R1: clock until the first byte with MSB clear (max 8 per spec). */
static uint8_t read_r1(void)
{
    for (int i = 0; i < 9; i++) {
        uint8_t b = xfer(0xFF);
        if ((b & 0x80) == 0) return b;
    }
    return 0xFF; /* timeout */
}

/* Skip 0xFF then read `n` trailing bytes (for R3/R7 payloads). */
static void read_rn(uint8_t *out, int n) { for (int i = 0; i < n; i++) out[i] = xfer(0xFF); }

/* Wait for the data start token 0xFE. */
static int wait_token(void)
{
    for (int i = 0; i < 2048; i++)
        if (xfer(0xFF) == 0xFE) return 0;
    return -1;
}

/* ---- the test ---------------------------------------------------------- */
int main(void)
{
    sd_blockstore_t store = {
        .read = disk_read, .write = disk_write,
        .block_count = NBLOCKS, .user = NULL,
    };
    sd_spi_target_init(&T, &store);

    /* Power-up: >=74 idle clocks with the card not selected. */
    clocks(10);

    /* CMD0: go idle -> R1 = 0x01 */
    send_cmd(0, 0);
    uint8_t r = read_r1();
    printf("CMD0  -> R1=0x%02X\n", r);
    assert(r == 0x01);

    /* CMD8: send if cond, voltage 0x1, pattern 0xAA -> R7 echoes 0x01AA */
    send_cmd(8, 0x000001AA);
    r = read_r1();
    uint8_t r7[4];
    read_rn(r7, 4);
    printf("CMD8  -> R1=0x%02X R7=%02X%02X%02X%02X\n", r, r7[0], r7[1], r7[2], r7[3]);
    assert(r == 0x01 && r7[2] == 0x01 && r7[3] == 0xAA);

    /* CMD58: read OCR (before init: not-ready) */
    send_cmd(58, 0);
    r = read_r1();
    uint8_t ocr[4];
    read_rn(ocr, 4);
    printf("CMD58 -> R1=0x%02X OCR=%02X%02X%02X%02X\n", r, ocr[0], ocr[1], ocr[2], ocr[3]);
    assert(r == 0x01);

    /* ACMD41 loop (CMD55 + CMD41 HCS) until idle clears. */
    int tries = 0;
    do {
        send_cmd(55, 0);
        (void)read_r1();
        send_cmd(41, 0x40000000);
        r = read_r1();
        tries++;
    } while ((r & 0x01) && tries < 100);
    printf("ACMD41-> R1=0x%02X after %d tries\n", r, tries);
    assert((r & 0x01) == 0 && tries >= 1);

    /* CMD58 again: CCS bit (bit30) must now be set -> SDHC. */
    send_cmd(58, 0);
    r = read_r1();
    read_rn(ocr, 4);
    printf("CMD58 -> R1=0x%02X OCR=%02X%02X%02X%02X (CCS=%d)\n",
           r, ocr[0], ocr[1], ocr[2], ocr[3], (ocr[0] & 0x40) ? 1 : 0);
    assert(r == 0x00 && (ocr[0] & 0x40));

    /* CMD24: write block 7 with a known pattern. */
    uint8_t pattern[SD_BLOCK_SIZE];
    for (unsigned i = 0; i < SD_BLOCK_SIZE; i++) pattern[i] = (uint8_t)(i * 7 + 3);
    send_cmd(24, 7);
    r = read_r1();
    assert(r == 0x00);
    (void)xfer(0xFF);                 /* gap before data token */
    (void)xfer(0xFE);                 /* data start token */
    for (unsigned i = 0; i < SD_BLOCK_SIZE; i++) (void)xfer(pattern[i]);
    uint16_t wcrc = sd_crc16(pattern, SD_BLOCK_SIZE);
    (void)xfer((uint8_t)(wcrc >> 8));
    (void)xfer((uint8_t)(wcrc & 0xFF));
    uint8_t dr = xfer(0xFF);          /* data-response token */
    printf("CMD24 -> R1=0x%02X data-resp=0x%02X\n", r, dr);
    assert((dr & 0x1F) == 0x05);
    /* wait out busy (card holds MISO 0x00 while programming) */
    for (int i = 0; i < 16; i++) if (xfer(0xFF) == 0xFF) break;
    assert(memcmp(g_disk[7], pattern, SD_BLOCK_SIZE) == 0);

    /* CMD17: read block 7 back and verify data + CRC16. */
    send_cmd(17, 7);
    r = read_r1();
    assert(r == 0x00);
    assert(wait_token() == 0);
    uint8_t rd[SD_BLOCK_SIZE];
    for (unsigned i = 0; i < SD_BLOCK_SIZE; i++) rd[i] = xfer(0xFF);
    uint8_t ch = xfer(0xFF), cl = xfer(0xFF);
    uint16_t rcrc = (uint16_t)((ch << 8) | cl);
    printf("CMD17 -> R1=0x%02X data CRC16=0x%04X (calc 0x%04X)\n",
           r, rcrc, sd_crc16(rd, SD_BLOCK_SIZE));
    assert(memcmp(rd, pattern, SD_BLOCK_SIZE) == 0);
    assert(rcrc == sd_crc16(rd, SD_BLOCK_SIZE));

    /* Unknown command -> illegal-command bit set. */
    send_cmd(1, 0);
    r = read_r1();
    printf("CMD1  -> R1=0x%02X (illegal bit=%d)\n", r, (r & 0x04) ? 1 : 0);
    assert(r & 0x04);

    printf("\nALL TESTS PASSED\n");
    return 0;
}
