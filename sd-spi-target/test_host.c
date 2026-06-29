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

static void read_data(uint8_t *out, unsigned len)
{
    assert(wait_token() == 0);
    for (unsigned i = 0; i < len; i++) out[i] = xfer(0xFF);
    uint8_t ch = xfer(0xFF), cl = xfer(0xFF);
    uint16_t got = (uint16_t)((ch << 8) | cl);
    assert(got == sd_crc16(out, len));
}

static void write_data_token(uint8_t token, const uint8_t *data, unsigned len, uint16_t crc)
{
    (void)xfer(token);
    for (unsigned i = 0; i < len; i++) (void)xfer(data[i]);
    (void)xfer((uint8_t)(crc >> 8));
    (void)xfer((uint8_t)(crc & 0xFF));
}

static void wait_not_busy(void)
{
    for (int i = 0; i < 32; i++)
        if (xfer(0xFF) == 0xFF) return;
    assert(!"card stayed busy");
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

    /* Linux 5.10 SD-over-SPI enumeration: CID, CSD, SCR, SSR, switch status. */
    uint8_t cid[16], csd[16], scr[8], ssr[64], sw[64];
    send_cmd(10, 0);
    r = read_r1();
    read_data(cid, sizeof(cid));
    printf("CMD10 -> R1=0x%02X CID name=%c%c%c%c%c\n",
           r, cid[3], cid[4], cid[5], cid[6], cid[7]);
    assert(r == 0x00 && memcmp(&cid[3], "NKVM1", 5) == 0);

    send_cmd(9, 0);
    r = read_r1();
    read_data(csd, sizeof(csd));
    printf("CMD9  -> R1=0x%02X CSD[0]=0x%02X CSD[5]=0x%02X\n", r, csd[0], csd[5]);
    assert(r == 0x00 && ((csd[0] >> 6) == 1) && ((csd[5] & 0x0F) == 9));

    send_cmd(55, 0);
    assert(read_r1() == 0x00);
    send_cmd(51, 0);
    r = read_r1();
    read_data(scr, sizeof(scr));
    printf("ACMD51-> R1=0x%02X SCR=%02X%02X...\n", r, scr[0], scr[1]);
    assert(r == 0x00 && scr[0] == 0x02 && (scr[1] & 0x0F) == 0x05);

    send_cmd(55, 0);
    assert(read_r1() == 0x00);
    send_cmd(13, 0);
    r = read_r1();
    assert(xfer(0xFF) == 0x00);       /* R2 second status byte */
    read_data(ssr, sizeof(ssr));
    printf("ACMD13-> R1=0x%02X SSR[0]=0x%02X\n", r, ssr[0]);
    assert(r == 0x00);

    send_cmd(6, 0x00FFFFFF);
    r = read_r1();
    read_data(sw, sizeof(sw));
    printf("CMD6  -> R1=0x%02X default-speed support=0x%02X\n", r, sw[13]);
    assert(r == 0x00 && (sw[13] & 0x01) && !(sw[13] & 0x02));

    send_cmd(13, 0);
    r = read_r1();
    assert(xfer(0xFF) == 0x00);
    printf("CMD13 -> R1=0x%02X\n", r);
    assert(r == 0x00);

    /* Linux enables SPI CRC by default after reading the card registers. */
    send_cmd(59, 1);
    r = read_r1();
    printf("CMD59 -> R1=0x%02X (CRC on)\n", r);
    assert(r == 0x00);

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
    wait_not_busy();
    assert(memcmp(g_disk[7], pattern, SD_BLOCK_SIZE) == 0);

    /* CRC-on write with a bad CRC must be rejected. */
    send_cmd(24, 6);
    r = read_r1();
    assert(r == 0x00);
    write_data_token(0xFE, pattern, SD_BLOCK_SIZE, 0x1234);
    dr = xfer(0xFF);
    printf("CMD24 bad CRC -> data-resp=0x%02X\n", dr);
    assert((dr & 0x1F) == 0x0B);
    wait_not_busy();

    /* CMD17: read block 7 back and verify data + CRC16. */
    send_cmd(17, 7);
    r = read_r1();
    assert(r == 0x00);
    uint8_t rd[SD_BLOCK_SIZE];
    read_data(rd, sizeof(rd));
    printf("CMD17 -> R1=0x%02X data verified\n", r);
    assert(memcmp(rd, pattern, SD_BLOCK_SIZE) == 0);

    /* CMD25/CMD18: Linux block I/O uses multi-block transfers. */
    uint8_t pattern2[SD_BLOCK_SIZE];
    for (unsigned i = 0; i < SD_BLOCK_SIZE; i++) pattern2[i] = (uint8_t)(255 - i);

    send_cmd(25, 8);
    r = read_r1();
    assert(r == 0x00);
    write_data_token(0xFC, pattern, SD_BLOCK_SIZE, sd_crc16(pattern, SD_BLOCK_SIZE));
    dr = xfer(0xFF);
    assert((dr & 0x1F) == 0x05);
    wait_not_busy();
    write_data_token(0xFC, pattern2, SD_BLOCK_SIZE, sd_crc16(pattern2, SD_BLOCK_SIZE));
    dr = xfer(0xFF);
    assert((dr & 0x1F) == 0x05);
    wait_not_busy();
    (void)xfer(0xFD);                 /* multi-write stop token */
    clocks(2);
    assert(memcmp(g_disk[8], pattern, SD_BLOCK_SIZE) == 0);
    assert(memcmp(g_disk[9], pattern2, SD_BLOCK_SIZE) == 0);

    send_cmd(18, 8);
    r = read_r1();
    assert(r == 0x00);
    read_data(rd, sizeof(rd));
    assert(memcmp(rd, pattern, SD_BLOCK_SIZE) == 0);
    read_data(rd, sizeof(rd));
    assert(memcmp(rd, pattern2, SD_BLOCK_SIZE) == 0);
    send_cmd(12, 0);
    r = read_r1();
    printf("CMD18 -> CMD12 R1=0x%02X after two blocks\n", r);
    assert(r == 0x00);

    /* Unknown command -> illegal-command bit set. */
    send_cmd(1, 0);
    r = read_r1();
    printf("CMD1  -> R1=0x%02X (illegal bit=%d)\n", r, (r & 0x04) ? 1 : 0);
    assert(r & 0x04);

    printf("\nALL TESTS PASSED\n");
    return 0;
}
