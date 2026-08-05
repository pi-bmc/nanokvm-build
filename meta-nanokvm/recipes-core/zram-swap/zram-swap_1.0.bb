SUMMARY = "Compressed RAM swap (zram) for the memory-constrained SG2002"
DESCRIPTION = "Enables a zram-backed swap device from a one-shot init script: \
sizes zram0 (lzo-rle, capped near physical RAM), mkswaps it and swaps it on \
at priority 100. Gives memory relief on the ~256 MiB board without SD-card \
wear. Requires CONFIG_ZRAM/ZSMALLOC (enabled in the board defconfig). \
Formerly a udev rule; the image no longer runs a device manager."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://zram-swap"
S = "${WORKDIR}"

inherit update-rc.d

RDEPENDS:${PN} = "util-linux-swapon util-linux-mkswap"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/zram-swap ${D}${sysconfdir}/init.d/zram-swap
}

INITSCRIPT_NAME = "zram-swap"
# Early in rcS: swap should exist before the memory-hungry Go server, whose
# inittab ::respawn entry starts once ::sysinit (including rcS) completes.
INITSCRIPT_PARAMS = "start 05 S ."

FILES:${PN} = "${sysconfdir}/init.d/zram-swap"
