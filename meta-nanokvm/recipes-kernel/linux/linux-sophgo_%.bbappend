# NanoKVM board customisation for the mainline linux-sophgo kernel.
#
# The base recipe (meta-sophgo/recipes-kernel/linux/linux-sophgo_6.18.bb) builds
# mainline v6.18 with the unified riscv `defconfig`. Here we:
#   * backport the upstream cv18xx USB DTS node + enable it on nano-b,
#   * merge the NanoKVM config-fragment delta (USB gadget, i2c-slave, ...),
#   * wrap Image + board DTB into the cvitek-style FIT (boot.sd) that mainline
#     U-Boot loads from the FAT boot partition.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# DTS backports for USB (v6.18 has the drivers but not the DT nodes). ORDER
# matters -- applied by the base do_patch in SRC_URI order:
#   0001 upstream "Add USB support for cv18xx": adds usb@4340000 (references
#        &usbphy) to cv180x.dtsi, enables &usb on nano-b. Merged post-6.18-rc1.
#   0002 companion backport: adds the sophgo,cv1800b-usb2-phy node (&usbphy,
#        child of the top syscon) that 0001 references -- absent in v6.18, so
#        without it dtc fails "Reference to non-existent node or label usbphy".
#   0003 local board tweaks on top of 0001: &usb -> dr_mode "peripheral" (gadget
#        role) and i2c1 on the repurposed SDIO1 pads for the slave EEPROM.
# nanokvm.cfg is merged in do_configure:append below; boot.its is consumed by
# do_deploy:append to build the boot.sd FIT.
SRC_URI:append:sg2002-licheervnano = " \
    file://0001-apply-dts-usb-dev.patch \
    file://0002-riscv-dts-sophgo-add-cv1800b-usb2-phy.patch \
    file://0003-nanokvm-board-dts.patch \
    file://0004-nanokvm-cvitek-ephy-driver.patch \
    file://0005-nanokvm-cv1800-reboot.patch \
    file://0006-nanokvm-cv1800-efuse-nvmem.patch \
    file://nanokvm.cfg \
    file://boot.its \
    "

# mkimage (FIT support) and dtc are needed to build the boot.sd FIT image.
DEPENDS:append:sg2002-licheervnano = " u-boot-tools-native dtc-native"

# Merge the NanoKVM fragment on top of the mainline defconfig. Plain `inherit
# kernel` (not kernel-yocto) does not auto-apply .cfg fragments, so do it here;
# do_compile's syncconfig then resolves dependencies.
do_configure:append:sg2002-licheervnano() {
    "${S}/scripts/kconfig/merge_config.sh" -m -O "${B}" \
        "${B}/.config" "${WORKDIR}/nanokvm.cfg"
    oe_runmake -C ${S} O=${B} olddefconfig
}

# Build the cvitek-style FIT boot image (boot.sd) and deploy it so wic can place
# it on the FAT boot partition (IMAGE_BOOT_FILES = "fip.bin boot.sd"). Mainline
# builds the DTB as sophgo/sg2002-licheerv-nano-b.dtb.
do_deploy:append:sg2002-licheervnano() {
    fit_work="${WORKDIR}/boot-fit"
    rm -rf "${fit_work}"
    install -d "${fit_work}"

    # Locate the freshly built kernel Image and board DTB.
    kimage="${B}/arch/riscv/boot/Image"
    if [ ! -f "${kimage}" ]; then
        kimage="$(find ${B} -path '*/arch/riscv/boot/Image' -type f 2>/dev/null | head -1)"
    fi
    kdtb="$(find ${B} -name 'sg2002-licheerv-nano-b.dtb' -type f 2>/dev/null | head -1)"
    if [ -z "${kdtb}" ]; then
        kdtb="$(find ${DEPLOYDIR} -name 'sg2002-licheerv-nano-b.dtb' -type f 2>/dev/null | head -1)"
    fi

    if [ ! -f "${kimage}" ] || [ -z "${kdtb}" ] || [ ! -f "${kdtb}" ]; then
        bbfatal "boot.sd: could not locate kernel Image (${kimage}) or DTB (${kdtb})"
    fi

    install -m 0644 "${kimage}" "${fit_work}/Image"
    install -m 0644 "${kdtb}" "${fit_work}/sg2002-licheerv-nano-b.dtb"
    install -m 0644 "${WORKDIR}/boot.its" "${fit_work}/boot.its"

    ( cd "${fit_work}" && mkimage -f boot.its boot.sd )

    install -m 0644 "${fit_work}/boot.sd" "${DEPLOYDIR}/boot.sd"
}
