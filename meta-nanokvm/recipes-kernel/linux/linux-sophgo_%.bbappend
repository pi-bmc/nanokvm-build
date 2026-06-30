# Supply the board defconfig. It originates from the sophgo-build submodule
# (boards/sg200x/sg2002_licheervnano_sd/linux/) and is vendored into this layer
# under files/ so the build does not depend on the submodule checkout.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:sg2002-licheervnano = " \
    file://sg2002_licheervnano_sd_defconfig \
    file://boot.its \
    "

# mkimage (FIT support) and dtc are needed to build the boot.sd FIT image.
DEPENDS:append:sg2002-licheervnano = " u-boot-tools-native dtc-native"

do_configure:prepend:sg2002-licheervnano() {
    if [ ! -f "${S}/arch/riscv/configs/sg2002_licheervnano_sd_defconfig" ]; then
        install -d "${S}/arch/riscv/configs"
        install -m 0644 "${WORKDIR}/sg2002_licheervnano_sd_defconfig" \
            "${S}/arch/riscv/configs/"
    fi
}

KBUILD_DEFCONFIG:sg2002-licheervnano = "sg2002_licheervnano_sd_defconfig"

# Build the cvitek-style FIT boot image (boot.sd) and deploy it so wic can place
# it in the FAT boot partition (IMAGE_BOOT_FILES = "fip.bin boot.sd"). This
# replaces the sophgo-build "make boot" step which produced boot.itb -> boot.sd.
do_deploy:append:sg2002-licheervnano() {
    fit_work="${WORKDIR}/boot-fit"
    rm -rf "${fit_work}"
    install -d "${fit_work}"

    # Locate the freshly built kernel Image and board DTB.
    kimage="${B}/arch/riscv/boot/Image"
    if [ ! -f "${kimage}" ]; then
        kimage="$(find ${B} -path '*/arch/riscv/boot/Image' -type f 2>/dev/null | head -1)"
    fi
    kdtb="$(find ${B} -name 'sg2002_licheervnano_sd.dtb' -type f 2>/dev/null | head -1)"
    if [ -z "${kdtb}" ]; then
        kdtb="$(find ${DEPLOYDIR} -name 'sg2002_licheervnano_sd.dtb' -type f 2>/dev/null | head -1)"
    fi

    if [ ! -f "${kimage}" ] || [ -z "${kdtb}" ] || [ ! -f "${kdtb}" ]; then
        bbfatal "boot.sd: could not locate kernel Image (${kimage}) or DTB (${kdtb})"
    fi

    install -m 0644 "${kimage}" "${fit_work}/Image"
    install -m 0644 "${kdtb}" "${fit_work}/sg2002_licheervnano_sd.dtb"
    install -m 0644 "${WORKDIR}/boot.its" "${fit_work}/boot.its"

    ( cd "${fit_work}" && mkimage -f boot.its boot.sd )

    install -m 0644 "${fit_work}/boot.sd" "${DEPLOYDIR}/boot.sd"
}
