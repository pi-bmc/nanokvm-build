/* SPDX-License-Identifier: GPL-2.0 */
/*
 * emmc_host_shim.h - userspace stand-ins so the *real* emmc_proto.c register
 * and framing logic compiles in the host self-test (test_proto.c). Only the
 * types referenced by struct emmc_dev and the protocol code are provided; the
 * kernel build never sees this file (it's behind #ifndef __KERNEL__).
 */
#ifndef EMMC_HOST_SHIM_H
#define EMMC_HOST_SHIM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned int uint;

#define __iomem

typedef int spinlock_t;
typedef struct { int counter; } atomic_t;
typedef unsigned int dev_t;
struct cdev { int _pad; };
struct device;
struct class;
struct task_struct;

#endif /* EMMC_HOST_SHIM_H */
