SUMMARY = "Compressed RAM swap (zram) for the memory-constrained SG2002"
DESCRIPTION = "Enables a zram-backed swap device via a single udev rule: when \
the built-in zram driver creates zram0, the rule sizes it (lzo-rle, capped near \
physical RAM), mkswaps it and swaps it on at priority 100. Gives memory relief \
on the ~256 MiB board without SD-card wear. Requires CONFIG_ZRAM/ZSMALLOC \
(enabled in the board defconfig)."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://60-zram-swap.rules"
S = "${WORKDIR}"

# mkswap/swapon come from util-linux (aliased at /sbin); udev delivers the
# zram0 uevent and runs the rule.
RDEPENDS:${PN} = "udev util-linux-swapon util-linux-mkswap"

do_install() {
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/60-zram-swap.rules \
        ${D}${sysconfdir}/udev/rules.d/60-zram-swap.rules
}

FILES:${PN} = "${sysconfdir}/udev/rules.d/60-zram-swap.rules"
