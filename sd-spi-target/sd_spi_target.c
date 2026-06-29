/*
 * sd_spi_target.c - SD card emulation (SPI mode) protocol engine.
 * See sd_spi_target.h for the model and limitations.
 */
#include "sd_spi_target.h"

/* ---- CRC -------------------------------------------------------------- */

uint8_t sd_crc7(const uint8_t *data, size_t len)
{
    uint8_t crc;
    size_t i;
    int b;

    crc = 0;
    for (i = 0; i < len; i++) {
        uint8_t d;

        d = data[i];
        for (b = 0; b < 8; b++) {
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
    uint16_t crc;
    size_t i;
    int b;

    crc = 0;
    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* ---- SD command/response constants ------------------------------------ */

enum {
    CMD0_GO_IDLE              = 0,
    CMD6_SWITCH_FUNC          = 6,
    CMD8_SEND_IF_COND         = 8,
    CMD9_SEND_CSD             = 9,
    CMD10_SEND_CID            = 10,
    CMD12_STOP                = 12,
    CMD13_SEND_STATUS         = 13,
    CMD16_SET_BLOCKLEN        = 16,
    CMD17_READ_SINGLE         = 17,
    CMD18_READ_MULTIPLE       = 18,
    CMD23_SET_BLOCK_COUNT     = 23,
    CMD24_WRITE_SINGLE        = 24,
    CMD25_WRITE_MULTIPLE      = 25,
    CMD55_APP_CMD             = 55,
    CMD58_READ_OCR            = 58,
    CMD59_CRC_ON_OFF          = 59,
    ACMD6_SET_BUS_WIDTH       = 6,
    ACMD13_SD_STATUS          = 13,
    ACMD23_SET_WR_BLK_ERASE_COUNT = 23,
    ACMD41_SD_SEND_OP_COND    = 41,
    ACMD51_SEND_SCR           = 51,
};

enum {
    R1_IDLE       = 0x01,
    R1_ERASE_RST  = 0x02,
    R1_ILLEGAL    = 0x04,
    R1_COM_CRC    = 0x08,
    R1_ERASE_SEQ  = 0x10,
    R1_ADDRESS    = 0x20,
    R1_PARAMETER  = 0x40,
};

enum {
    DATA_RESP_ACCEPTED = 0x05,
    DATA_RESP_CRC_ERR  = 0x0B,
    DATA_RESP_WRITE_ERR = 0x0D,
};

/* ---- bitfield/register helpers ---------------------------------------- */

static void put_bits(uint8_t *buf, unsigned int total_bits,
                     unsigned int start, unsigned int size, uint32_t value)
{
    unsigned int i;

    for (i = 0; i < size; i++) {
        unsigned int bit;
        unsigned int byte;
        uint8_t mask;

        bit = start + i;
        byte = (total_bits - 1 - bit) / 8;
        mask = (uint8_t)(1u << (bit & 7));
        if (value & (1u << i))
            buf[byte] |= mask;
        else
            buf[byte] &= (uint8_t)~mask;
    }
}

static uint32_t advertised_blocks(const sd_spi_target_t *t)
{
    if (t->store.block_count >= 1024)
        return t->store.block_count;

    /*
     * CSD v2 encodes capacity in 512 KiB units. The real NanoKVM backing
     * store is much larger than this; the floor exists for tiny native tests.
     */
    return 1024;
}

static void finish_cid_csd_crc(uint8_t reg[16])
{
    reg[15] = (uint8_t)((sd_crc7(reg, 15) << 1) | 1);
}

static void build_cid(uint8_t cid[16])
{
    memset(cid, 0, 16);

    put_bits(cid, 128, 120, 8, 0x4E);          /* manufacturer: NanoKVM */
    put_bits(cid, 128, 104, 16, 0x4E4B);       /* OEM/application: NK */
    put_bits(cid, 128, 96, 8, 'N');
    put_bits(cid, 128, 88, 8, 'K');
    put_bits(cid, 128, 80, 8, 'V');
    put_bits(cid, 128, 72, 8, 'M');
    put_bits(cid, 128, 64, 8, '1');
    put_bits(cid, 128, 56, 8, 0x10);           /* product revision 1.0 */
    put_bits(cid, 128, 24, 32, 0x20260624);    /* deterministic serial */
    put_bits(cid, 128, 12, 8, 26);             /* 2026 */
    put_bits(cid, 128, 8, 4, 6);               /* June */

    finish_cid_csd_crc(cid);
}

static void build_csd(const sd_spi_target_t *t, uint8_t csd[16])
{
    uint32_t blocks;
    uint32_t c_size;

    memset(csd, 0, 16);

    blocks = advertised_blocks(t);
    c_size = (blocks / 1024) ? (blocks / 1024) - 1 : 0;
    if (c_size > 0x3FFFFF)
        c_size = 0x3FFFFF;

    put_bits(csd, 128, 126, 2, 1);             /* CSD v2.0, SDHC/SDXC */
    put_bits(csd, 128, 112, 8, 0x0E);          /* TAAC fixed by spec */
    put_bits(csd, 128, 104, 8, 0x00);          /* NSAC fixed by spec */
    put_bits(csd, 128, 96, 8, 0x32);           /* 25 MHz transfer rate */
    put_bits(csd, 128, 84, 12,
             (1u << 0) | (1u << 2) | (1u << 4) | (1u << 8) | (1u << 10));
    put_bits(csd, 128, 80, 4, 9);              /* READ_BL_LEN = 512 */
    put_bits(csd, 128, 48, 22, c_size);        /* device size */

    finish_cid_csd_crc(csd);
}

static void build_scr(uint8_t scr[8])
{
    memset(scr, 0, 8);

    put_bits(scr, 64, 60, 4, 0);               /* SCR structure v1.0 */
    put_bits(scr, 64, 56, 4, 2);               /* SD physical spec v2.0 */
    put_bits(scr, 64, 48, 4, 0x5);             /* 1-bit and 4-bit modes */
}

static void build_sd_status(uint8_t status[64])
{
    memset(status, 0, 64);
}

static void build_switch_status(uint8_t status[64])
{
    memset(status, 0, 64);

    /*
     * Function group 1 supports only default speed. That keeps Linux 5.10 in
     * the slow/default mode that a software-backed SPI target can service.
     */
    status[13] = 0x01;
    status[16] = 0x00;
}

/* ---- response framing -------------------------------------------------- */

static uint8_t r1(const sd_spi_target_t *t)
{
    return t->ready ? 0x00 : R1_IDLE;
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

static void respond(sd_spi_target_t *t, sd_state_t next)
{
    t->after_tx = next;
    t->state = SD_ST_RESP;
}

static void queue_r1_next(sd_spi_target_t *t, uint8_t status, sd_state_t next)
{
    tx_reset(t);
    tx_push(t, 0xFF);
    tx_push(t, status);
    respond(t, next);
}

static void queue_r1(sd_spi_target_t *t, uint8_t status)
{
    queue_r1_next(t, status, SD_ST_CMD);
}

static void queue_r2(sd_spi_target_t *t, uint8_t status)
{
    tx_reset(t);
    tx_push(t, 0xFF);
    tx_push(t, status);
    tx_push(t, 0x00);
    respond(t, SD_ST_CMD);
}

static void queue_r3_r7(sd_spi_target_t *t, uint8_t status,
                        uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3)
{
    tx_reset(t);
    tx_push(t, 0xFF);
    tx_push(t, status);
    tx_push(t, b0);
    tx_push(t, b1);
    tx_push(t, b2);
    tx_push(t, b3);
    respond(t, SD_ST_CMD);
}

static void queue_data(sd_spi_target_t *t, uint8_t status, uint8_t second_status,
                       const uint8_t *data, uint16_t len, sd_state_t next)
{
    uint16_t crc;
    uint16_t i;

    tx_reset(t);
    tx_push(t, 0xFF);
    tx_push(t, status);
    if (second_status)
        tx_push(t, 0x00);
    tx_push(t, 0xFF);
    tx_push(t, 0xFE);
    for (i = 0; i < len; i++)
        tx_push(t, data[i]);
    crc = sd_crc16(data, len);
    tx_push(t, (uint8_t)(crc >> 8));
    tx_push(t, (uint8_t)(crc & 0xFF));
    respond(t, next);
}

static void queue_data_token(sd_spi_target_t *t, const uint8_t *data,
                             uint16_t len, sd_state_t next)
{
    uint16_t crc;
    uint16_t i;

    tx_reset(t);
    tx_push(t, 0xFF);
    tx_push(t, 0xFE);
    for (i = 0; i < len; i++)
        tx_push(t, data[i]);
    crc = sd_crc16(data, len);
    tx_push(t, (uint8_t)(crc >> 8));
    tx_push(t, (uint8_t)(crc & 0xFF));
    respond(t, next);
}

static uint8_t command_crc_status(sd_spi_target_t *t)
{
    uint8_t idx;
    uint8_t expected;

    idx = t->cmd[0] & 0x3F;
    if (!t->crc_enabled && idx != CMD0_GO_IDLE && idx != CMD8_SEND_IF_COND)
        return 0;

    expected = (uint8_t)((sd_crc7(t->cmd, 5) << 1) | 1);
    return (t->cmd[5] == expected) ? 0 : R1_COM_CRC;
}

/* ---- data operations --------------------------------------------------- */

static uint8_t block_status(const sd_spi_target_t *t, uint32_t block)
{
    if (t->store.block_count && block >= t->store.block_count)
        return (uint8_t)(r1(t) | R1_ADDRESS);

    return r1(t);
}

static void queue_read_block(sd_spi_target_t *t, uint32_t block, sd_state_t next)
{
    uint8_t data[SD_BLOCK_SIZE];
    uint8_t status;
    int rc;

    status = block_status(t, block);
    if (status & (R1_ADDRESS | R1_PARAMETER)) {
        queue_r1_next(t, status, SD_ST_CMD);
        return;
    }

    rc = -1;
    if (t->store.read)
        rc = t->store.read(t->store.user, block, data);
    if (rc != 0)
        memset(data, 0x00, sizeof(data));

    queue_data(t, r1(t), 0, data, SD_BLOCK_SIZE, next);
}

static void queue_read_block_cont(sd_spi_target_t *t, uint32_t block)
{
    uint8_t data[SD_BLOCK_SIZE];
    int rc;

    rc = -1;
    if (t->store.read && !(block_status(t, block) & (R1_ADDRESS | R1_PARAMETER)))
        rc = t->store.read(t->store.user, block, data);
    if (rc != 0)
        memset(data, 0x00, sizeof(data));

    queue_data_token(t, data, SD_BLOCK_SIZE, SD_ST_MULTI_READ);
}

static void queue_register(sd_spi_target_t *t, uint8_t status,
                           const uint8_t *data, uint16_t len)
{
    queue_data(t, status, 0, data, len, SD_ST_CMD);
}

/* ---- command handling -------------------------------------------------- */

static void handle_app_command(sd_spi_target_t *t, uint8_t idx, uint32_t arg)
{
    switch (idx) {
    case ACMD6_SET_BUS_WIDTH:
        queue_r1(t, r1(t));
        break;

    case ACMD13_SD_STATUS: {
        uint8_t status[64];

        build_sd_status(status);
        queue_data(t, r1(t), 1, status, sizeof(status), SD_ST_CMD);
        break;
    }

    case ACMD23_SET_WR_BLK_ERASE_COUNT:
        queue_r1(t, r1(t));
        break;

    case ACMD41_SD_SEND_OP_COND:
        /*
         * Linux 5.10 polls ACMD41 until the idle bit clears. Returning idle
         * once exercises that path and then exposes the card as ready.
         */
        if (t->acmd41_seen)
            t->ready = 1;
        t->acmd41_seen = 1;
        queue_r1(t, r1(t));
        break;

    case ACMD51_SEND_SCR: {
        uint8_t scr[8];

        build_scr(scr);
        queue_register(t, r1(t), scr, sizeof(scr));
        break;
    }

    default:
        (void)arg;
        queue_r1(t, (uint8_t)(r1(t) | R1_ILLEGAL));
        break;
    }
}

static void handle_command(sd_spi_target_t *t)
{
    uint8_t idx;
    uint32_t arg;
    uint8_t was_app;
    uint8_t crc_status;

    idx = t->cmd[0] & 0x3F;
    arg = ((uint32_t)t->cmd[1] << 24) | ((uint32_t)t->cmd[2] << 16) |
          ((uint32_t)t->cmd[3] << 8)  |  (uint32_t)t->cmd[4];
    was_app = t->app_cmd;
    t->app_cmd = 0;

    crc_status = command_crc_status(t);
    if (crc_status) {
        queue_r1(t, (uint8_t)(r1(t) | crc_status));
        return;
    }

    if (was_app) {
        handle_app_command(t, idx, arg);
        return;
    }

    switch (idx) {
    case CMD0_GO_IDLE:
        t->ready = 0;
        t->acmd41_seen = 0;
        t->crc_enabled = 0;
        t->write_multi = 0;
        queue_r1(t, R1_IDLE);
        break;

    case CMD6_SWITCH_FUNC: {
        uint8_t status[64];

        build_switch_status(status);
        queue_register(t, r1(t), status, sizeof(status));
        break;
    }

    case CMD8_SEND_IF_COND:
        queue_r3_r7(t, r1(t), 0x00, 0x00,
                    (uint8_t)((arg >> 8) & 0x0F), (uint8_t)(arg & 0xFF));
        break;

    case CMD9_SEND_CSD: {
        uint8_t csd[16];

        build_csd(t, csd);
        queue_register(t, r1(t), csd, sizeof(csd));
        break;
    }

    case CMD10_SEND_CID: {
        uint8_t cid[16];

        build_cid(cid);
        queue_register(t, r1(t), cid, sizeof(cid));
        break;
    }

    case CMD12_STOP:
        t->write_multi = 0;
        queue_r1(t, r1(t));
        break;

    case CMD13_SEND_STATUS:
        queue_r2(t, r1(t));
        break;

    case CMD16_SET_BLOCKLEN:
        queue_r1(t, (arg == SD_BLOCK_SIZE) ? r1(t) : (uint8_t)(r1(t) | R1_PARAMETER));
        break;

    case CMD17_READ_SINGLE:
        queue_read_block(t, arg, SD_ST_CMD);
        break;

    case CMD18_READ_MULTIPLE:
        t->read_block = arg;
        queue_read_block(t, t->read_block, SD_ST_MULTI_READ);
        t->read_block++;
        break;

    case CMD23_SET_BLOCK_COUNT:
        queue_r1(t, r1(t));
        break;

    case CMD24_WRITE_SINGLE:
        if (block_status(t, arg) & (R1_ADDRESS | R1_PARAMETER)) {
            queue_r1(t, block_status(t, arg));
            break;
        }
        t->write_block = arg;
        t->write_multi = 0;
        t->rx_pos = 0;
        queue_r1_next(t, r1(t), SD_ST_RECV_TOKEN);
        break;

    case CMD25_WRITE_MULTIPLE:
        if (block_status(t, arg) & (R1_ADDRESS | R1_PARAMETER)) {
            queue_r1(t, block_status(t, arg));
            break;
        }
        t->write_block = arg;
        t->write_multi = 1;
        t->rx_pos = 0;
        queue_r1_next(t, r1(t), SD_ST_RECV_TOKEN);
        break;

    case CMD55_APP_CMD:
        t->app_cmd = 1;
        queue_r1(t, r1(t));
        break;

    case CMD58_READ_OCR:
        queue_r3_r7(t, r1(t), t->ready ? 0xC0 : 0x00, 0xFF, 0x80, 0x00);
        break;

    case CMD59_CRC_ON_OFF:
        t->crc_enabled = (uint8_t)(arg & 1u);
        queue_r1(t, r1(t));
        break;

    default:
        queue_r1(t, (uint8_t)(r1(t) | R1_ILLEGAL));
        break;
    }
}

static void finish_write_block(sd_spi_target_t *t)
{
    uint16_t got_crc;
    uint16_t calc_crc;
    uint8_t data_response;
    int rc;

    got_crc = (uint16_t)(((uint16_t)t->rx[SD_BLOCK_SIZE] << 8) |
                         t->rx[SD_BLOCK_SIZE + 1]);
    calc_crc = sd_crc16(t->rx, SD_BLOCK_SIZE);
    rc = -1;

    if (t->crc_enabled && got_crc != calc_crc) {
        data_response = DATA_RESP_CRC_ERR;
    } else if (block_status(t, t->write_block) & (R1_ADDRESS | R1_PARAMETER)) {
        data_response = DATA_RESP_WRITE_ERR;
    } else {
        if (t->store.write)
            rc = t->store.write(t->store.user, t->write_block, t->rx);
        data_response = (rc == 0) ? DATA_RESP_ACCEPTED : DATA_RESP_WRITE_ERR;
    }

    tx_reset(t);
    tx_push(t, data_response);
    tx_push(t, 0x00);
    tx_push(t, 0x00);
    tx_push(t, 0xFF);
    if (t->write_multi && data_response == DATA_RESP_ACCEPTED)
        t->write_block++;
    respond(t, t->write_multi ? SD_ST_RECV_TOKEN : SD_ST_CMD);
}

/* ---- byte pump --------------------------------------------------------- */

void sd_spi_target_init(sd_spi_target_t *t, const sd_blockstore_t *store)
{
    memset(t, 0, sizeof(*t));
    if (store)
        t->store = *store;
    t->state = SD_ST_CMD;
}

uint8_t sd_spi_target_step(sd_spi_target_t *t, uint8_t mosi)
{
    switch (t->state) {
    case SD_ST_CMD:
        if (t->cmd_len == 0) {
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
        uint8_t out;

        out = (t->tx_pos < t->tx_len) ? t->tx[t->tx_pos] : 0xFF;
        t->tx_pos++;
        if (t->tx_pos >= t->tx_len) {
            t->state = t->after_tx;
            if (t->state == SD_ST_CMD || t->state == SD_ST_MULTI_READ)
                t->cmd_len = 0;
        }
        return out;
    }

    case SD_ST_RECV_TOKEN:
        if (t->write_multi && mosi == 0xFD) {
            t->write_multi = 0;
            t->state = SD_ST_CMD;
            t->cmd_len = 0;
            return 0xFF;
        }
        if ((!t->write_multi && mosi == 0xFE) || (t->write_multi && mosi == 0xFC)) {
            t->rx_pos = 0;
            t->state = SD_ST_RECV_DATA;
        } else if (mosi != 0xFF && !t->write_multi) {
            t->state = SD_ST_CMD;
        }
        return 0xFF;

    case SD_ST_RECV_DATA:
        if (t->rx_pos < (uint16_t)(SD_BLOCK_SIZE + 2))
            t->rx[t->rx_pos] = mosi;
        t->rx_pos++;
        if (t->rx_pos >= (uint16_t)(SD_BLOCK_SIZE + 2))
            finish_write_block(t);
        return 0xFF;

    case SD_ST_MULTI_READ:
        if (t->cmd_len == 0) {
            if ((mosi & 0xC0) == 0x40) {
                t->cmd[t->cmd_len++] = mosi;
                return 0xFF;
            }
            queue_read_block_cont(t, t->read_block);
            t->read_block++;
            return 0xFF;
        }
        t->cmd[t->cmd_len++] = mosi;
        if (t->cmd_len == 6) {
            t->cmd_len = 0;
            handle_command(t);
        }
        return 0xFF;
    }

    return 0xFF;
}
