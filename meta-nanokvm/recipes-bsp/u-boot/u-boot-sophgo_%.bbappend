# Applies the SPI2 slave pinmux patch (remaps WiFi pads P18/P21/P22/P23 to SPI2).
FILESEXTRAPATHS:prepend:sg2002-licheervnano := "${TOPDIR}/../patches/build:"

SRC_URI:append:sg2002-licheervnano = " \
    file://0001-licheervnano-uboot-spi2-slave-pinmux.patch \
    "

# Board-specific U-Boot init files from the sophgo-build submodule.
FILESEXTRAPATHS:prepend:sg2002-licheervnano += ":${TOPDIR}/../build/boards/sg200x/sg2002_licheervnano_sd/u-boot"

SRC_URI:append:sg2002-licheervnano = " \
    file://sg2002_licheervnano_sd_defconfig \
    file://cvi_board_init.c \
    file://cvitek.h \
    "

do_configure:prepend:sg2002-licheervnano() {
    for f in cvi_board_init.c cvitek.h; do
        if [ -f "${WORKDIR}/${f}" ]; then
            cp "${WORKDIR}/${f}" "${S}/board/cvitek/" || true
        fi
    done
}
