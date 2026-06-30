/* SPDX-License-Identifier: GPL-2.0 */
/*
 * emmc_uapi.h - the char-device ABI shared between the kernel module and the
 * userspace backing-store daemon (/dev/emmc-emu0).
 *
 * Modeled on the ESP-IDF sdio_slave host-interface split: a small "register
 * window" (this info/ioctl surface) plus the bulk data buffer (the device's
 * read/write/mmap of the backing store).
 */
#ifndef EMMC_UAPI_H
#define EMMC_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct emmc_info {
	__u64 capacity_bytes;
	__u64 capacity_blocks;
	__u32 last_cmd;		/* most recent command index seen on the bus */
	__u32 last_arg;
	__u32 cmd_count;	/* commands serviced since load		    */
	__u32 crc_errors;	/* command frames that failed CRC7	    */
	__u32 rca;		/* relative card address assigned by host   */
	__u32 state;		/* enum emmc_state			    */
	__u32 high_capacity;	/* sector vs byte addressing		    */
	__u32 spec_vers;	/* CSD SPEC_VERS (<4 => no EXT_CSD)	    */
};

#define EMMC_IOC_MAGIC		'M'
#define EMMC_IOC_GET_INFO	_IOR(EMMC_IOC_MAGIC, 1, struct emmc_info)
#define EMMC_IOC_SYNC		_IO(EMMC_IOC_MAGIC, 2)

#endif /* EMMC_UAPI_H */
