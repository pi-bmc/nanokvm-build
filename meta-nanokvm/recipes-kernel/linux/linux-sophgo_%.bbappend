# Adds the SD-SPI target driver (emulates microSD over SPI slave mode)
# and the Kconfig/Makefile wiring patch.

# Source files from the local sd-spi-target/ directory in this repo.
FILESEXTRAPATHS:prepend := "${TOPDIR}/../sd-spi-target:${TOPDIR}/../sd-spi-target/kernel:${TOPDIR}/../patches/linux_5.10:"

SRC_URI:append:sg2002-licheervnano = " \
    file://sd_spi_target.c \
    file://sd_spi_target.h \
    file://spi-dw-sd-slave.c \
    file://0001-spi-register-dw-ssi-sd-slave.patch \
    file://spi-dw-sd-slave.cfg \
    "

# Board defconfig from the sophgo-build submodule.
FILESEXTRAPATHS:prepend:sg2002-licheervnano := "${TOPDIR}/../build/boards/sg200x/sg2002_licheervnano_sd/linux:"
SRC_URI:append:sg2002-licheervnano = " \
    file://sg2002_licheervnano_sd_defconfig \
    "

do_configure:prepend:sg2002-licheervnano() {
    # Copy SD-SPI driver source files into the kernel tree before configuration.
    install -d "${S}/drivers/spi/"
    for f in sd_spi_target.c sd_spi_target.h spi-dw-sd-slave.c; do
        if [ -f "${WORKDIR}/${f}" ]; then
            cp "${WORKDIR}/${f}" "${S}/drivers/spi/"
        fi
    done

    # Copy board defconfig if not already in the kernel tree.
    if [ ! -f "${S}/arch/riscv/configs/sg2002_licheervnano_sd_defconfig" ]; then
        install -d "${S}/arch/riscv/configs"
        cp "${WORKDIR}/sg2002_licheervnano_sd_defconfig" \
           "${S}/arch/riscv/configs/" || \
            bbwarn "sg2002_licheervnano_sd_defconfig not found in WORKDIR"
    fi
}

KBUILD_DEFCONFIG:sg2002-licheervnano = "sg2002_licheervnano_sd_defconfig"
