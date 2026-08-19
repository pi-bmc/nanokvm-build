SUMMARY = "NanoKVM boot-partition config files (deploy-only)"
DESCRIPTION = "Deploys the boot-partition config files the NanoKVM server \
reads at runtime (board, hostname.prefix, ver, usb.ecm0). Installs nothing \
into the rootfs: the disk provisioning that once lived here (S01fs) is in the \
initramfs, and the server owns the USB gadget configfs \
(server/service/usbgadget). The first-boot firmware seeding (S06nanokvm-data) \
is gone entirely -- the BMC no longer stores a host boot image; host firmware \
is updated with FMP capsules. usb.ecm0 is read once by the server's first-boot \
migration to seed the default ECM network function."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = " \
    file://board \
    file://hostname.prefix \
    file://usb.ecm0 \
"
S = "${WORKDIR}"

inherit deploy nopackages

# The config files live on the FAT boot partition (read at runtime from the
# read-only /boot mount by the server), so deploy them to DEPLOY_DIR_IMAGE for
# wic's IMAGE_BOOT_FILES.
#
# "slot" and "extlinux.conf" are gone. The A/B slot moved into the U-Boot
# environment (raw redundant sectors, atomic) because keeping it here meant
# something had to write to a FAT16 boot partition to change slots; extlinux
# went away with the move to a single FIT payload.
do_deploy() {
    install -d ${DEPLOYDIR}
    for f in board hostname.prefix usb.ecm0; do
        install -m 0644 ${WORKDIR}/$f ${DEPLOYDIR}/$f
    done
    # Firmware version string (no build timestamp -> reproducible).
    echo "NanoKVM ${DISTRO_VERSION} (${DISTRO_CODENAME})" > ${DEPLOYDIR}/ver
}
# deploy.bbclass sets up the sstate/deploydir machinery but does not register
# the task itself; the recipe must add it to the build graph.
addtask deploy after do_install before do_build
