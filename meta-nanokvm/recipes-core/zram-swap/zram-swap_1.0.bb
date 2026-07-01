SUMMARY = "Compressed RAM swap (zram) for the memory-constrained SG2002"
DESCRIPTION = "sysvinit service that brings up a zram-backed swap device sized \
to a fraction of physical RAM. Gives real memory relief on the ~256 MiB board \
without SD-card wear. Requires CONFIG_ZRAM/ZSMALLOC/CRYPTO_LZO (enabled in the \
board defconfig)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://zram-swap"
S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "zram-swap"
# Start early (after virtual filesystems are mounted) so swap is available
# before the heavier services come up; stop late on the way down.
INITSCRIPT_PARAMS = "start 04 2 3 4 5 . stop 20 0 1 6 ."

# mkswap/swapon/swapoff/modprobe are all provided by busybox in this image.

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/zram-swap ${D}${sysconfdir}/init.d/zram-swap
}

FILES:${PN} = "${sysconfdir}/init.d/zram-swap"
