SUMMARY = "NanoKVM USB gadget + filesystem init, and boot-partition config files"
DESCRIPTION = "Brings up the NanoKVM USB device gadget (HID keyboard/mouse/ \
touchpad, RNDIS network, mass-storage virtual media) via configfs, and the \
filesystem setup (mount /boot, configfs, debugfs; create/mount the /data \
partition). Also deploys the boot-partition config files the gadget/app read \
(board, hostname.prefix, ver, usb.disk0, usb.keyboard, usb.mouse, usb.rndis0). \
Vendored from pi-bmc/nanokvm-app packaging/etc/init.d (S01fs, S03usbdev)."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = " \
    file://S01fs \
    file://S03usbdev \
    file://board \
    file://hostname.prefix \
    file://usb.disk0 \
    file://usb.keyboard \
    file://usb.mouse \
    file://usb.rndis0 \
"
S = "${WORKDIR}"

inherit deploy

# S01fs repartitions/formats/mounts the data partition on first boot.
RDEPENDS:${PN} = "parted e2fsprogs-resize2fs exfatprogs"

do_install() {
    install -d ${D}${sysconfdir}/init.d ${D}${sysconfdir}/rc5.d
    install -m 0755 ${WORKDIR}/S01fs     ${D}${sysconfdir}/init.d/nanokvm-fs
    install -m 0755 ${WORKDIR}/S03usbdev ${D}${sysconfdir}/init.d/nanokvm-usbdev

    # Run in runlevel 5 after rcS has mounted /proc, /sys, /boot and the root
    # fs: nanokvm-fs (mount configfs/debugfs + /data) then nanokvm-usbdev
    # (build the gadget), both before the app (S95nanokvm).
    ln -sf ../init.d/nanokvm-fs     ${D}${sysconfdir}/rc5.d/S06nanokvm-fs
    ln -sf ../init.d/nanokvm-usbdev ${D}${sysconfdir}/rc5.d/S07nanokvm-usbdev
}

# The config files live on the FAT boot partition (read at runtime from /boot),
# so deploy them to DEPLOY_DIR_IMAGE for wic's IMAGE_BOOT_FILES.
do_deploy() {
    install -d ${DEPLOYDIR}
    for f in board hostname.prefix usb.disk0 usb.keyboard usb.mouse usb.rndis0; do
        install -m 0644 ${WORKDIR}/$f ${DEPLOYDIR}/$f
    done
    # Firmware version string (no build timestamp -> reproducible).
    echo "NanoKVM ${DISTRO_VERSION} (${DISTRO_CODENAME})" > ${DEPLOYDIR}/ver
}
# deploy.bbclass sets up the sstate/deploydir machinery but does not register
# the task itself; the recipe must add it to the build graph.
addtask deploy after do_install before do_build

FILES:${PN} = " \
    ${sysconfdir}/init.d/nanokvm-fs \
    ${sysconfdir}/init.d/nanokvm-usbdev \
    ${sysconfdir}/rc5.d/S06nanokvm-fs \
    ${sysconfdir}/rc5.d/S07nanokvm-usbdev \
"
