/*
 * sd_spi_target.h - SD card emulation (SPI mode) protocol engine.
 *
 * This implements the *card/target* side of the SD "SPI mode" protocol: it
 * answers the command/response handshake a simple SPI host (3D printer,
 * Arduino/MCU SD library, RepRap/Marlin firmware, ...) issues when it thinks it
 * is talking to a microSD card.
 *
 * It is platform independent. The engine is driven one SPI byte at a time:
 * for every byte the SPI-slave hardware clocks in from the host (MOSI), call
 * sd_spi_target_step() and transmit the byte it returns (MISO). Wire that to
 * the SG2002 DesignWare-SSI slave FIFO/DMA in the binding layer
 * (see platform/ and README.md).
 *
 * Scope / limitations:
 *   - SPI mode only. This does NOT and CANNOT emulate native 1-bit/4-bit SD
 *     mode (see README.md - the SG2002 has no SD-target hardware for that).
 *   - Single-block read (CMD17) and write (CMD24). Multi-block (CMD18/CMD25)
 *     is not implemented (most simple hosts use single-block).
 *   - Presents as an SDHC (block-addressed) card.
 */
#ifndef SD_SPI_TARGET_H
#define SD_SPI_TARGET_H

#include <stdint.h>
#include <stddef.h>

#define SD_BLOCK_SIZE 512u

/*
 * Backing store for the emulated card. block is a 0-based 512-byte block index
 * (SDHC block addressing). Return 0 on success, non-zero on error. Either
 * callback may be NULL (read-only / write-only / dummy card).
 */
typedef struct {
    int (*read)(void *user, uint32_t block, uint8_t buf[SD_BLOCK_SIZE]);
    int (*write)(void *user, uint32_t block, const uint8_t buf[SD_BLOCK_SIZE]);
    uint32_t block_count;   /* total blocks; reported in the CSD if you extend it */
    void *user;
} sd_blockstore_t;

typedef enum {
    SD_ST_CMD = 0,    /* scanning for / collecting a 6-byte command frame */
    SD_ST_RESP,       /* streaming a queued response / read-data buffer    */
    SD_ST_RECV_TOKEN, /* CMD24: waiting for the 0xFE data start token      */
    SD_ST_RECV_DATA,  /* CMD24: absorbing 512 data bytes + 2 CRC bytes     */
} sd_state_t;

typedef struct {
    sd_blockstore_t store;

    sd_state_t state;
    sd_state_t after_tx;        /* state to enter once the tx buffer drains */

    uint8_t  cmd[6];
    uint8_t  cmd_len;

    /* transmit buffer (response bytes / read data block) */
    uint8_t  tx[8 + SD_BLOCK_SIZE];
    uint16_t tx_len;
    uint16_t tx_pos;

    /* receive buffer for CMD24 (512 data + 2 CRC) */
    uint8_t  rx[SD_BLOCK_SIZE + 2];
    uint16_t rx_pos;
    uint32_t write_block;       /* target block for the in-flight CMD24 */

    /* card state */
    uint8_t  app_cmd;           /* set by CMD55: next ACMDxx is an app command */
    uint8_t  ready;             /* cleared while idle, set after ACMD41 completes */
    uint8_t  acmd41_seen;       /* used to return idle once, then ready */
} sd_spi_target_t;

/* Initialise the engine against a backing store. */
void sd_spi_target_init(sd_spi_target_t *t, const sd_blockstore_t *store);

/*
 * Exchange one SPI byte. mosi is the byte clocked in from the host; the return
 * value is the byte to clock back out on MISO for the same SPI transfer.
 */
uint8_t sd_spi_target_step(sd_spi_target_t *t, uint8_t mosi);

/* CRC helpers (also used by hosts / tests). */
uint8_t  sd_crc7(const uint8_t *data, size_t len);   /* returns 7-bit CRC, unshifted */
uint16_t sd_crc16(const uint8_t *data, size_t len);  /* CCITT, init 0x0000, poly 0x1021 */

#endif /* SD_SPI_TARGET_H */
