/*
 * sd_spi_target.c - SD card emulation (SPI mode) protocol engine.
 * See sd_spi_target.h for the model and limitations.
 */
#include "sd_spi_target.h"
#include <string.h>

/* ---- CRC -------------------------------------------------------------- */

uint8_t sd_crc7(const uint8_t *data, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t d = data[i];
        for (int b = 0; b < 8; b++) {
            crc <<= 1;
            if ((d ^ crc) & 0x80)
                crc ^= 0x09;
            d <<= 1;
        }
    }
    return crc & 0x7F;
}

uint16_t sd_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* ---- response framing -------------------------------------------------- */

/* SD command indices (SPI mode subset). */
enum {
    CMD0_GO_IDLE          = 0,
    CMD8_SEND_IF_COND     = 8,
    CMD9_SEND_CSD         = 9,
    CMD10_SEND_CID        = 10,
    CMD12_STOP            = 12,
    CMD16_SET_BLOCKLEN    = 16,
    CMD17_READ_SINGLE     = 17,
    CMD24_WRITE_SINGLE    = 24,
    CMD55_APP_CMD         = 55,
    CMD58_READ_OCR        = 58,
    CMD59_CRC_ON_OFF      = 59,
    ACMD41_SD_SEND_OP_COND = 41,
};

/* R1 status byte: bit0 = in-idle-state, MSB always 0. */
static uint8_t r1(const sd_spi_target_t *t)
{
    return t->ready ? 0x00 : 0x01;
}

/* R1 with the illegal-command bit (bit2) set. */
static uint8_t r1_illegal(const sd_spi_target_t *t)
{
    return (uint8_t)(r1(t) | 0x04);
}

static void tx_reset(sd_spi_target_t *t)
{
    t->tx_len = 0;
    t->tx_pos = 0;
}

static void tx_push(sd_spi_target_t *t, uint8_t b)
{
    if (t->tx_len < sizeof(t->tx))
        t->tx[t->tx_len++] = b;
}

/* Queue a response and arrange to return to `next` once it has been sent.
 * A leading 0xFF gives the host its mandatory Ncr gap before the response. */
static void respond(sd_spi_target_t *t, sd_state_t next)
{
    t->after_tx = next;
    t->state = SD_ST_RESP;
}

static void queue_r1(sd_spi_target_t *t, uint8_t status)
{
    tx_reset(t);
    tx_push(t, 0xFF);       /* Ncr gap */
    tx_push(t, status);
    respond(t, SD_ST_CMD);
}

/* ---- command handling -------------------------------------------------- */

static void handle_read(sd_spi_target_t *t, uint32_t block)
{
    uint8_t data[SD_BLOCK_SIZE];
    int rc = -1;

    if (t->store.read)
        rc = t->store.read(t->store.user, block, data);
    if (rc != 0)
        memset(data, 0x00, sizeof(data));

    tx_reset(t);
    tx_push(t, 0xFF);                 /* Ncr gap */
    tx_push(t, r1(t));                /* R1 = success */
    tx_push(t, 0xFF);                 /* Nac gap before data token */
    tx_push(t, 0xFE);                 /* data start token */
    for (uint16_t i = 0; i < SD_BLOCK_SIZE; i++)
        tx_push(t, data[i]);
    uint16_t crc = sd_crc16(data, SD_BLOCK_SIZE);
    tx_push(t, (uint8_t)(crc >> 8));
    tx_push(t, (uint8_t)(crc & 0xFF));
    respond(t, SD_ST_CMD);
}

static void handle_command(sd_spi_target_t *t)
{
    uint8_t idx = t->cmd[0] & 0x3F;
    uint32_t arg = ((uint32_t)t->cmd[1] << 24) | ((uint32_t)t->cmd[2] << 16) |
                   ((uint32_t)t->cmd[3] << 8)  |  (uint32_t)t->cmd[4];
    uint8_t was_app = t->app_cmd;
    t->app_cmd = 0;

    if (was_app && idx == ACMD41_SD_SEND_OP_COND) {
        /* Host polls ACMD41 until the idle bit clears. Report idle once so the
         * host exercises its wait loop, then come up ready. */
        if (t->acmd41_seen)
            t->ready = 1;
        t->acmd41_seen = 1;
        queue_r1(t, r1(t));
        return;
    }

    switch (idx) {
    case CMD0_GO_IDLE:
        t->ready = 0;
        t->acmd41_seen = 0;
        queue_r1(t, 0x01);            /* always idle after reset */
        break;

    case CMD8_SEND_IF_COND: {         /* R7: R1 + 4 bytes (echo voltage+pattern) */
        tx_reset(t);
        tx_push(t, 0xFF);
        tx_push(t, r1(t));
        tx_push(t, 0x00);
        tx_push(t, 0x00);
        tx_push(t, (uint8_t)((arg >> 8) & 0x0F)); /* voltage accepted (0x1) */
        tx_push(t, (uint8_t)(arg & 0xFF));        /* check pattern echo */
        respond(t, SD_ST_CMD);
        break;
    }

    case CMD58_READ_OCR: {            /* R3: R1 + 4-byte OCR */
        tx_reset(t);
        tx_push(t, 0xFF);
        tx_push(t, r1(t));
        /* bit31 power-up done, bit30 CCS=1 (SDHC), 3.2-3.4V window */
        tx_push(t, t->ready ? 0xC0 : 0x00);
        tx_push(t, 0xFF);
        tx_push(t, 0x80);
        tx_push(t, 0x00);
        respond(t, SD_ST_CMD);
        break;
    }

    case CMD55_APP_CMD:
        t->app_cmd = 1;
        queue_r1(t, r1(t));
        break;

    case CMD16_SET_BLOCKLEN:          /* fixed 512 on SDHC; just accept */
    case CMD59_CRC_ON_OFF:            /* accept; CRC is ignored on writes */
        queue_r1(t, r1(t));
        break;

    case CMD17_READ_SINGLE:
        handle_read(t, arg);
        break;

    case CMD24_WRITE_SINGLE:
        t->write_block = arg;
        t->rx_pos = 0;
        tx_reset(t);
        tx_push(t, 0xFF);
        tx_push(t, r1(t));
        respond(t, SD_ST_RECV_TOKEN); /* after R1, wait for the data token */
        break;

    default:
        queue_r1(t, r1_illegal(t));
        break;
    }
}

/* ---- byte pump --------------------------------------------------------- */

void sd_spi_target_init(sd_spi_target_t *t, const sd_blockstore_t *store)
{
    memset(t, 0, sizeof(*t));
    t->store = *store;
    t->state = SD_ST_CMD;
}

uint8_t sd_spi_target_step(sd_spi_target_t *t, uint8_t mosi)
{
    switch (t->state) {
    case SD_ST_CMD:
        if (t->cmd_len == 0) {
            /* A command frame starts with bits [7:6] == 0b01. */
            if ((mosi & 0xC0) == 0x40)
                t->cmd[t->cmd_len++] = mosi;
            return 0xFF;
        }
        t->cmd[t->cmd_len++] = mosi;
        if (t->cmd_len == 6) {
            t->cmd_len = 0;
            handle_command(t);
        }
        return 0xFF;

    case SD_ST_RESP: {
        uint8_t out = (t->tx_pos < t->tx_len) ? t->tx[t->tx_pos] : 0xFF;
        t->tx_pos++;
        if (t->tx_pos >= t->tx_len) {
            t->state = t->after_tx;
            if (t->state == SD_ST_CMD)
                t->cmd_len = 0;
        }
        return out;
    }

    case SD_ST_RECV_TOKEN:
        /* Host streams 0xFF then the 0xFE start token. 0xFD/0xFC = multi-write
         * tokens we don't support; treat anything <0xFF that isn't 0xFE as a
         * (rejected) abort by simply returning to idle. */
        if (mosi == 0xFE) {
            t->rx_pos = 0;
            t->state = SD_ST_RECV_DATA;
        }
        return 0xFF;

    case SD_ST_RECV_DATA:
        if (t->rx_pos < (uint16_t)(SD_BLOCK_SIZE + 2))
            t->rx[t->rx_pos] = mosi;
        t->rx_pos++;
        if (t->rx_pos >= (uint16_t)(SD_BLOCK_SIZE + 2)) {
            int rc = -1;
            if (t->store.write)
                rc = t->store.write(t->store.user, t->write_block, t->rx);
            /* Data-response token: 0b00000101 = accepted, 0b00001101 = write err. */
            tx_reset(t);
            tx_push(t, (rc == 0) ? 0x05 : 0x0D);
            tx_push(t, 0x00);         /* busy (card programming) */
            tx_push(t, 0x00);
            tx_push(t, 0xFF);         /* busy released */
            respond(t, SD_ST_CMD);
        }
        return 0xFF;
    }

    return 0xFF;
}
