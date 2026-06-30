// SPDX-License-Identifier: GPL-2.0
/*
 * emmc_crc.c - CRC7 and CRC16-CCITT for the eMMC/SD line protocol.
 *
 * Shared verbatim between the kernel module and the userspace daemon.
 */
#include "emmc_crc.h"

/*
 * CRC7, G(x) = x^7 + x^3 + 1 (0x09), MSB-first.
 *
 * The standard SD/eMMC formulation processes the message MSB-first and keeps
 * the running remainder left-aligned in bit 7 of an 8-bit accumulator; the
 * feedback tap is 0x12 = (0x09 << 1) because we work in the byte's high bits
 * and shift the result down by one at the end.
 */
u8 emmc_crc7_bits(const u8 *data, unsigned int bits)
{
	u8 crc = 0;
	unsigned int i;

	for (i = 0; i < bits; i++) {
		unsigned int byte = i >> 3;
		unsigned int bit = (data[byte] >> (7 - (i & 7))) & 1;
		unsigned int inv = bit ^ ((crc >> 6) & 1);

		crc <<= 1;
		if (inv)
			crc ^= 0x09;
		crc &= 0x7f;
	}

	return crc & 0x7f;
}

/* CRC16-CCITT over a byte buffer, MSB-first (used by the daemon/self-tests). */
u16 emmc_crc16_buf(const u8 *data, unsigned int len)
{
	u16 crc = emmc_crc16_init();
	unsigned int i, b;

	for (i = 0; i < len; i++)
		for (b = 0; b < 8; b++)
			crc = emmc_crc16_bit(crc, (data[i] >> (7 - b)) & 1);

	return crc;
}
