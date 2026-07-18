SUMMARY = "U-Boot for Raspberry Pi 4/5 (pi-bmc boot image)"
DESCRIPTION = "Upstream U-Boot built with the pi-bmc rpi_arm64 defconfig patch, \
deployed as the kernel8.img payload of the boot partition."
HOMEPAGE = "https://docs.u-boot.org"
LICENSE = "GPL-2.0-or-later"
LIC_FILES_CHKSUM = "file://Licenses/README;md5=2ca5f2c35c8cc335f0a19756634782f1"

PROVIDES += "u-boot virtual/bootloader"

# Tag (or full sha / branch) of upstream U-Boot to build.
# Overridden from the version file via the kas env passthrough.
UBOOT_REF ?= "v2026.07"
PV = "${@d.getVar('UBOOT_REF').lstrip('v')}"

SRC_URI = " \
    https://github.com/u-boot/u-boot/archive/${UBOOT_REF}.tar.gz;downloadfilename=u-boot-${UBOOT_REF}.tar.gz;subdir=u-boot-src;striplevel=1 \
    file://rpi_arm64_defconfig.patch \
"
# The source ref is parameterised, so no fixed checksum can be recorded.
BB_STRICT_CHECKSUM = "ignore"

S = "${WORKDIR}/u-boot-src"
B = "${S}"

DEPENDS = "bc-native bison-native flex-native dtc-native openssl-native gnutls-native"

inherit deploy nopackages

EXTRA_OEMAKE = 'CROSS_COMPILE=${TARGET_PREFIX} CC="${TARGET_PREFIX}gcc ${TOOLCHAIN_OPTIONS} ${DEBUG_PREFIX_MAP}"'
EXTRA_OEMAKE += 'HOSTCC="${BUILD_CC} ${BUILD_CFLAGS} ${BUILD_LDFLAGS}"'
EXTRA_OEMAKE += 'STAGING_INCDIR=${STAGING_INCDIR_NATIVE} STAGING_LIBDIR=${STAGING_LIBDIR_NATIVE}'
EXTRA_OEMAKE += 'HOSTLDLIBS_mkimage="-lssl -lcrypto"'
EXTRA_OEMAKE += 'LOCALVERSION_AUTO=n LOCALVERSION='

do_configure() {
    oe_runmake -C ${S} ${UBOOT_MACHINE}
}

do_compile() {
    unset LDFLAGS CFLAGS CPPFLAGS
    oe_runmake -C ${S}
}

do_install[noexec] = "1"

do_deploy() {
    install -D -m 0644 ${B}/u-boot.bin ${DEPLOYDIR}/u-boot.bin
}
addtask deploy after do_compile before do_build

