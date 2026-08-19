SUMMARY = "NanoKVM A/B boot health confirmation and data-partition growth"
DESCRIPTION = "The userspace half of the A/B boot flow. nanokvm-bootok \
restores the running slot's RAUC try counter once the slot has proven it can \
boot; nanokvm-growdata grows the data partition to fill the card; \
nanokvm-update installs a system image into the inactive slot and activates \
it. The first two are here rather than in the initramfs so that nothing on \
the boot path writes to the card."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://nanokvm-bootok \
           file://nanokvm-growdata \
           file://nanokvm-update \
           file://fw_env.config \
           "

S = "${WORKDIR}"
COMPATIBLE_MACHINE = "sg2002-licheervnano"

# fw_printenv/fw_setenv speak the same redundant-environment protocol U-Boot
# does; e2fsprogs/util-linux supply resize2fs and sfdisk/partx for the grow.
# Everything nanokvm-update needs beyond that -- tar, gzip, dd, od, stat, df,
# awk, sha256sum, mount -- is a busybox applet already in the image.
RDEPENDS:${PN} = "libubootenv-bin e2fsprogs-resize2fs util-linux-sfdisk util-linux-partx"

do_install() {
    install -d ${D}${sbindir}
    install -m 0755 ${WORKDIR}/nanokvm-bootok   ${D}${sbindir}/nanokvm-bootok
    install -m 0755 ${WORKDIR}/nanokvm-growdata ${D}${sbindir}/nanokvm-growdata
    install -m 0755 ${WORKDIR}/nanokvm-update   ${D}${sbindir}/nanokvm-update

    install -d ${D}${sysconfdir}
    install -m 0644 ${WORKDIR}/fw_env.config ${D}${sysconfdir}/fw_env.config

    # rcS.d, not inittab: rcS runs after `mount -a`, so the data partition is
    # up, and it runs before the server's respawn entry, so the try counter is
    # restored before anything can crash-loop the board back through it.
    # nanokvm-update is not in rcS.d -- it is invoked by hand or by the server.
    install -d ${D}${sysconfdir}/rcS.d
    ln -sf ${sbindir}/nanokvm-bootok   ${D}${sysconfdir}/rcS.d/S20nanokvm-bootok
    ln -sf ${sbindir}/nanokvm-growdata ${D}${sysconfdir}/rcS.d/S21nanokvm-growdata
}

FILES:${PN} += "${sysconfdir}/fw_env.config ${sysconfdir}/rcS.d"
