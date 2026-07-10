SUMMARY = "NanoKVM USB gadget + first-boot filesystem init, and boot-partition config files"
DESCRIPTION = "Brings up the NanoKVM USB device gadget (HID keyboard/mouse/ \
touchpad, RNDIS network, mass-storage virtual media) via configfs, driven by a \
udev rule on the udc 'add' event, plus the first-boot filesystem setup \
(create/format/mount the /data partition). Also deploys the boot-partition \
config files the gadget/app read (board, hostname.prefix, ver, usb.keyboard, \
usb.mouse, usb.rndis0). Vendored from pi-bmc/nanokvm-app packaging/etc/init.d \
(S01fs, S03usbdev)."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = " \
    file://S01fs \
    file://S03usbdev \
    file://60-nanokvm-usbgadget.rules \
    file://board \
    file://extlinux.conf \
    file://hostname.prefix \
    file://usb.keyboard \
    file://usb.mouse \
    file://usb.rndis0 \
"
S = "${WORKDIR}"

inherit deploy

# S01fs repartitions/formats/mounts the data partition on first boot; udev
# delivers the udc uevent that builds the gadget. /boot + configfs come from
# /etc/fstab (base-files bbappend), which mountall mounts before udev starts.
RDEPENDS:${PN} = "parted e2fsprogs-resize2fs exfatprogs udev"

do_install() {
    install -d ${D}${sysconfdir}/init.d ${D}${sysconfdir}/rc5.d
    install -m 0755 ${WORKDIR}/S01fs ${D}${sysconfdir}/init.d/nanokvm-fs

    # Installed under its literal Buildroot-era name, deliberately NOT renamed:
    # the NanoKVM server shells out to "/etc/init.d/S03usbdev stop" and
    # "/etc/init.d/S03usbdev start" from server/service/vm/virtual-device.go
    # (8 call sites) to re-arm virtual media. Installing it as
    # /etc/init.d/nanokvm-usbdev, as this recipe used to, meant every one of
    # those calls hit a missing path and silently did nothing.
    install -m 0755 ${WORKDIR}/S03usbdev ${D}${sysconfdir}/init.d/S03usbdev

    # The gadget is built by udev on the udc "add" event now, not by a runlevel
    # symlink: it comes up during rcS (S04udev) instead of rc5 (S07) -- before
    # sshd and the app -- and re-arms by itself if the UDC reappears. Only
    # nanokvm-fs keeps an rc5.d entry; parted/resize2fs/mkfs.exfat are far too
    # slow to run on a udev event worker.
    install -d ${D}${sysconfdir}/udev/rules.d
    install -m 0644 ${WORKDIR}/60-nanokvm-usbgadget.rules \
        ${D}${sysconfdir}/udev/rules.d/60-nanokvm-usbgadget.rules

    ln -sf ../init.d/nanokvm-fs ${D}${sysconfdir}/rc5.d/S06nanokvm-fs
}

# The config files live on the FAT boot partition (read at runtime from /boot),
# so deploy them to DEPLOY_DIR_IMAGE for wic's IMAGE_BOOT_FILES.
do_deploy() {
    install -d ${DEPLOYDIR}
    for f in board hostname.prefix usb.keyboard usb.mouse usb.rndis0 extlinux.conf; do
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
    ${sysconfdir}/init.d/S03usbdev \
    ${sysconfdir}/udev/rules.d/60-nanokvm-usbgadget.rules \
    ${sysconfdir}/rc5.d/S06nanokvm-fs \
"
