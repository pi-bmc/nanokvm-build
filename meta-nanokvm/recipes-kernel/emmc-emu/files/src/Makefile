# SPDX-License-Identifier: GPL-2.0
# Out-of-tree kbuild for the NanoKVM eMMC emulator.
#
# Built by the Yocto module.bbclass (which provides KERNEL_SRC and the cross
# toolchain), or standalone against a prepared kernel tree:
#
#   make KERNEL_SRC=/path/to/linux ARCH=riscv CROSS_COMPILE=riscv64-...-

obj-m := emmc_emu.o
emmc_emu-y := emmc_main.o emmc_gpio.o emmc_phy.o emmc_proto.o emmc_crc.o

# Treat the cvitek 5.10 tree's older-style warnings as non-fatal, matching the
# kernel recipe's KCFLAGS.
ccflags-y += -Wno-error=implicit-function-declaration

SRC := $(shell pwd)
KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) clean
