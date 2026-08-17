include $(PWD)/../Makefile.interdrv.param

ifneq ($(KERNELRELEASE),)
$(MODPREFIX)_v4l2-objs += soph_v4l2_drv.o
$(MODPREFIX)_v4l2-objs += soph_lt6911.o
$(MODPREFIX)_v4l2-objs += soph_subdevs.o
$(MODPREFIX)_v4l2-objs += soph_venc_node.o
$(MODPREFIX)_v4l2-objs += soph_pipeline.o

obj-m += $(MODPREFIX)_v4l2.o

ccflags-y += -I$(PWD)/
ccflags-y += -I$(PWD)/../include/common/uapi/ -I$(PWD)/../include/chip/$(CVIARCH_L)/uapi/
ccflags-y += -I$(PWD)/../include/common/kapi/
ccflags-y += -I$(PWD)/../include/
ccflags-y += -I$(PWD)/../base/
ccflags-y += -I$(PWD)/../base/chip/$(CVIARCH_L)
ccflags-y += -I$(PWD)/../sys/common/ -I$(PWD)/../sys/common/uapi
ccflags-y += -I$(PWD)/../vi/chip/$(CVIARCH_L)/ -I$(PWD)/../vi/common/
ccflags-y += -I$(PWD)/../vpss/common/ -I$(PWD)/../vpss/chip/$(CVIARCH_L)/
ccflags-y += -I$(PWD)/../cif/chip/$(CVIARCH_L)/ -I$(PWD)/../cif/chip/$(CVIARCH_L)/drv
ccflags-y += -I$(PWD)/../cvi_vc_drv/include/
ccflags-y += -I$(srctree)/drivers/staging/android

# USE_KERNEL_MODE keeps the venc uapi headers off <pthread.h>, exactly as
# cvi_vc_drv builds them.
ccflags-y += -DUSE_KERNEL_MODE

ccflags-y += -Wall -Wextra -Werror -Wno-unused-parameter -Wno-sign-compare

# The recipe overrides this with the accumulated list of every earlier
# module's Module.symvers; the defaults below only matter for a by-hand build
# of this directory alone.
KBUILD_EXTRA_SYMBOLS = $(PWD)/../base/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(PWD)/../sys/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(PWD)/../vi/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(PWD)/../vpss/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(PWD)/../cif/Module.symvers
KBUILD_EXTRA_SYMBOLS += $(PWD)/../cvi_vc_drv/Module.symvers
else
all:
	$(MAKE) ARCH=$(ARCH) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	@rm -f *.o .*.cmd modules.order Module.symvers *.ko *.mod.c
	@rm -rf .tmp_versions
	@find . -name \*.o* -type f -delete
endif
