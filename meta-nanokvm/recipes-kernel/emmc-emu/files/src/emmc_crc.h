/* SPDX-License-Identifier: GPL-2.0 */
/*
 * emmc_crc.h - CRC engines for the eMMC/SD line protocol.
 *
 * Two polynomials are used on the bus:
 *
 *   CRC7  : G(x) = x^7 + x^3 + 1   (0x09)  - commands and most responses,
 *           computed MSB-first over the "content" bits of the 48/136-bit frame
 *           (everything except the 7-bit CRC field and the end bit).
 *
 *   CRC16 : G(x) = x^16 + x^12 + x^5 + 1 (CCITT, 0x1021) - one CRC per DAT
 *           line, computed MSB-first over the data payload that is clocked out
 *           on that line. In 4-bit mode the byte stream is striped across the
 *           four lines (nibble interleave), so each line carries its own CRC16.
 *
 * The same source compiles in the kernel module and in the userspace daemon;
 * keep it free of kernel-only headers. Callers provide u8/u16 typedefs (the
 * kernel side via <linux/types.h>, the daemon via <stdint.h> shim below).
 */
#ifndef EMMC_CRC_H
#define EMMC_CRC_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif

/*
 * CRC7 over a left-aligned bit stream.
 *
 * @data : buffer holding the content bits, MSB of data[0] transmitted first.
 * @bits : number of valid bits (e.g. 40 for a command/R1, 120 for the
 *         CID/CSD content of an R2).
 *
 * Returns the 7-bit CRC in the low 7 bits. On the wire it is transmitted MSB
 * first and followed by the end bit '1', i.e. the trailing frame byte is
 * (crc << 1) | 1.
 */
u8 emmc_crc7_bits(const u8 *data, unsigned int bits);

/* Convenience: CRC7 over a whole number of bytes (bits = len * 8). */
static inline u8 emmc_crc7(const u8 *data, unsigned int len)
{
	return emmc_crc7_bits(data, len * 8);
}

/*
 * Incremental CRC16-CCITT (one running value per DAT line).
 *
 * Seed with emmc_crc16_init(), fold in each transmitted/received bit with
 * emmc_crc16_bit(), and read the 16-bit remainder out at the end. A bitwise
 * API is used deliberately: in 1-bit mode the device shifts the block out one
 * bit at a time, and in 4-bit mode each line sees an independent bit stream,
 * so a per-bit fold maps cleanly onto the sampling/driving loop.
 */
static inline u16 emmc_crc16_init(void)
{
	return 0;
}

static inline u16 emmc_crc16_bit(u16 crc, unsigned int bit)
{
	unsigned int xorflag = ((crc >> 15) ^ (bit & 1)) & 1;

	crc <<= 1;
	if (xorflag)
		crc ^= 0x1021;
	return crc;
}

/* Whole-buffer CRC16 (MSB-first), used by the daemon and for self-tests. */
u16 emmc_crc16_buf(const u8 *data, unsigned int len);

#endif /* EMMC_CRC_H */
