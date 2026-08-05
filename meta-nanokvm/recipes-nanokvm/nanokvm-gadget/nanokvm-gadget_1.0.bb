SUMMARY = "NanoKVM data-partition seeding and boot-partition config files"
DESCRIPTION = "Seeds the persistent data partition (/var/lib/nanokvm, mounted \
by the initramfs) with the Raspberry Pi boot image on first boot, and deploys \
the boot-partition config files the NanoKVM server and initramfs read at \
runtime (board, hostname.prefix, ver, usb.ecm0, slot, extlinux.conf). The \
disk provisioning that used to live here (S01fs partition dance) moved into \
the initramfs (nanokvm-initramfs), which owns the disk before the root is \
mounted. The USB device gadget is not built here either: the server \
(server/service/usbgadget) owns the gadget configfs and assembles it at \
startup. usb.ecm0 is read once by the server's first-boot migration to seed \
the default ECM network function."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = " \
    file://nanokvm-data \
    file://board \
    file://extlinux.conf \
    file://hostname.prefix \
    file://usb.ecm0 \
    file://slot \
"
S = "${WORKDIR}"

inherit deploy update-rc.d

# S06 in rc5: after zram (S05, rcS), before the server (S95) that reads the
# seeded firmware image. Same registration mechanism as the sibling recipes
# (zram-swap, nanokvm-server) instead of a hand-rolled rc5.d symlink.
INITSCRIPT_NAME = "nanokvm-data"
INITSCRIPT_PARAMS = "start 06 5 ."

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/nanokvm-data ${D}${sysconfdir}/init.d/nanokvm-data
}

# The config files live on the FAT boot partition (read at runtime from /boot
# by the server, and "slot" by the initramfs for rootfs A/B selection), so
# deploy them to DEPLOY_DIR_IMAGE for wic's IMAGE_BOOT_FILES.
do_deploy() {
    install -d ${DEPLOYDIR}
    for f in board hostname.prefix usb.ecm0 slot extlinux.conf; do
        install -m 0644 ${WORKDIR}/$f ${DEPLOYDIR}/$f
    done
    # Firmware version string (no build timestamp -> reproducible).
    echo "NanoKVM ${DISTRO_VERSION} (${DISTRO_CODENAME})" > ${DEPLOYDIR}/ver
}
# deploy.bbclass sets up the sstate/deploydir machinery but does not register
# the task itself; the recipe must add it to the build graph.
addtask deploy after do_install before do_build

FILES:${PN} = " \
    ${sysconfdir}/init.d/nanokvm-data \
    ${sysconfdir}/rc5.d/S06nanokvm-data \
"
