# Supply the board defconfig from the sophgo-build submodule.
FILESEXTRAPATHS:prepend:sg2002-licheervnano := "${TOPDIR}/../build/boards/sg200x/sg2002_licheervnano_sd/linux:"

SRC_URI:append:sg2002-licheervnano = " \
    file://sg2002_licheervnano_sd_defconfig \
    "

do_configure:prepend:sg2002-licheervnano() {
    if [ ! -f "${S}/arch/riscv/configs/sg2002_licheervnano_sd_defconfig" ]; then
        install -d "${S}/arch/riscv/configs"
        install -m 0644 "${WORKDIR}/sg2002_licheervnano_sd_defconfig" \
            "${S}/arch/riscv/configs/"
    fi
}

KBUILD_DEFCONFIG:sg2002-licheervnano = "sg2002_licheervnano_sd_defconfig"
