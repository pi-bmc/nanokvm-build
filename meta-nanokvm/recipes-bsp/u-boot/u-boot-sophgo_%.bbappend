# Placeholder — no NanoKVM-specific U-Boot customizations active.
# The licheervnano-cvisdk-2021.10 branch already includes board support.
#
# To re-enable the SPI2 slave pinmux patch and board init file overrides,
# uncomment the block below and add meta-nanokvm/recipes-bsp/u-boot/u-boot-sophgo/
# containing the patch and board files.
#
# FILESEXTRAPATHS:prepend:sg2002-licheervnano := "${TOPDIR}/../patches/build:${TOPDIR}/../build/boards/sg200x/sg2002_licheervnano_sd/u-boot:"
# SRC_URI:append:sg2002-licheervnano = " \
#     file://0001-licheervnano-uboot-spi2-slave-pinmux.patch \
#     file://sg2002_licheervnano_sd_defconfig \
#     file://cvi_board_init.c \
#     file://cvitek.h \
#     "
