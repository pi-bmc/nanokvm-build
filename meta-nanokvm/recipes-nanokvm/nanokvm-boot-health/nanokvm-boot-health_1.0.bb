SUMMARY = "NanoKVM A/B boot health confirmation and data-partition growth"
DESCRIPTION = "Two userspace one-shots that the boot path deliberately does \
not do itself: nanokvm-bootok ends an update's probation once the slot has \
proven it can boot, and nanokvm-growdata grows the data partition to fill the \
card. Both are here rather than in the initramfs so that nothing on the boot \
path writes to the card."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://nanokvm-bootok \
           file://nanokvm-growdata \
           file://fw_env.config \
           "

S = "${WORKDIR}"
COMPATIBLE_MACHINE = "sg2002-licheervnano"

# fw_printenv/fw_setenv speak the same redundant-environment protocol U-Boot
# does; e2fsprogs/util-linux supply resize2fs and sfdisk/partx for the grow.
RDEPENDS:${PN} = "libubootenv-bin e2fsprogs-resize2fs util-linux-sfdisk util-linux-partx"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/nanokvm-bootok   ${D}${sbindir}/nanokvm-bootok
    install -m 0755 ${WORKDIR}/nanokvm-growdata ${D}${sbindir}/nanokvm-growdata

    install -d ${D}${sysconfdir}
    install -m 0644 ${WORKDIR}/fw_env.config ${D}${sysconfdir}/fw_env.config

    # rcS.d, not inittab: rcS runs after `mount -a`, so the data partition is
    # up, and it runs before the server's respawn entry, so probation ends
    # before anything can crash-loop the board back into the counter.
    install -d ${D}${sysconfdir}/rcS.d
    ln -sf ${sbindir}/nanokvm-bootok   ${D}${sysconfdir}/rcS.d/S20nanokvm-bootok
    ln -sf ${sbindir}/nanokvm-growdata ${D}${sysconfdir}/rcS.d/S21nanokvm-growdata
}

FILES:${PN} += "${sysconfdir}/fw_env.config ${sysconfdir}/rcS.d"
