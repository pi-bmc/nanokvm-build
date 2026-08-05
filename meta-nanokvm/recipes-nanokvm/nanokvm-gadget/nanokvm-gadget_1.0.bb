SUMMARY = "NanoKVM boot-partition config files (deploy-only)"
DESCRIPTION = "Deploys the boot-partition config files the NanoKVM server and \
initramfs read at runtime (board, hostname.prefix, ver, usb.ecm0, slot, \
extlinux.conf). Installs nothing into the rootfs: the disk provisioning that \
once lived here (S01fs) is in the initramfs, the first-boot firmware seeding \
(S06nanokvm-data) is in the server (server/service/firmware seeds from \
rpi-firmware-seed's baked-in .xz), and the server also owns the USB gadget \
configfs (server/service/usbgadget). usb.ecm0 is read once by the server's \
first-boot migration to seed the default ECM network function."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = " \
    file://board \
    file://extlinux.conf \
    file://hostname.prefix \
    file://usb.ecm0 \
    file://slot \
"
S = "${WORKDIR}"

inherit deploy nopackages

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
