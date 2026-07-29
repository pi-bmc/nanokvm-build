SUMMARY = "NanoKVM initramfs /init: A/B squashfs slots + tmpfs overlay root"
DESCRIPTION = "The PID-1 script for the NanoKVM initramfs. Waits for the SD \
card, creates the ext4 data partition on first boot, picks a rootfs slot \
(/slot on the FAT boot partition, with automatic fallback to the other slot), \
assembles squashfs + tmpfs-overlay as the root, mounts the data partition at \
/var/lib/nanokvm, and switch_roots into sysvinit. See files/init for the \
persistence contract."
LICENSE = "GPL-3.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-3.0-only;md5=c79ff39f19dfec6d293b95dea7b07891"

SRC_URI = "file://init"
S = "${WORKDIR}"

# Tools /init actually invokes; busybox provides the rest (sh, mount,
# switch_root, sfdisk is NOT busybox's -- the util-linux one understands
# --append and script input).
RDEPENDS:${PN} = "busybox util-linux-sfdisk e2fsprogs-e2fsck e2fsprogs-mke2fs"

do_install() {
    install -m 0755 ${WORKDIR}/init ${D}/init
}

FILES:${PN} = "/init"
