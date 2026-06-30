# Supply the board defconfig. It originates from the sophgo-build submodule
# (boards/sg200x/sg2002_licheervnano_sd/linux/) and is vendored into this layer
# under files/ so the build does not depend on the submodule checkout.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

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
