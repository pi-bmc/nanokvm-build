/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stub for the vendor eFuse driver, which does not exist in mainline.
 *
 * The vendor 5.10 tree carried include/linux/cv180x_efuse.h plus a matching
 * soc driver. base.c uses it in exactly five places, all of them sysfs
 * debug attributes -- /sys/class/cvi-base/{efuse_shadow,efuse_prog,uid} --
 * and none of them on the video data path. Nothing in the capture pipeline
 * (vb, cif, vi, vpss, vcodec) reads an eFuse.
 *
 * Porting the driver would also drag in cvi_efuse_write(), an irreversible
 * one-time-programmable write path, for no benefit here: the only eFuse value
 * this board actually needs is the MAC, and U-Boot already reads it (see
 * meta-sophgo 0005-licheerv-nano-efuse-mac.patch) and hands it over via DT.
 *
 * So the attributes stay registered but report -ENODEV rather than silently
 * returning stale data. If eFuse access is ever needed from Linux, replace
 * this header by porting the vendor soc driver.
 */

#ifndef __CV180X_EFUSE_STUB_H__
#define __CV180X_EFUSE_STUB_H__

#include <linux/errno.h>
#include <linux/types.h>

static inline int64_t cvi_efuse_read_from_shadow(uint32_t addr)
{
	return -ENODEV;
}

static inline int cvi_efuse_write(uint32_t addr, uint32_t value)
{
	return -ENODEV;
}

static inline int cvi_efuse_read_buf(u32 addr, void *buf, size_t buf_size)
{
	return -ENODEV;
}

#endif /* __CV180X_EFUSE_STUB_H__ */
