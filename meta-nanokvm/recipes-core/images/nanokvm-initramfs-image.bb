SUMMARY = "NanoKVM initramfs image (cpio.gz loaded by extlinux INITRD)"
DESCRIPTION = "Minimal initramfs that assembles the NanoKVM root: A/B \
squashfs slot selection with fallback, tmpfs overlay upper, first-boot \
creation of the ext4 data partition, and the /var/lib/nanokvm mount. Kept as \
a separate INITRD file on the FAT boot partition (not bundled into the \
kernel) so it can be updated without a kernel rebuild."

# Modeled on core-image-minimal-initramfs: PACKAGE_INSTALL (not
# IMAGE_INSTALL) so nothing beyond the listed packages sneaks in.
PACKAGE_INSTALL = " \
    nanokvm-initramfs \
    busybox \
    base-passwd \
    ${ROOTFS_BOOTSTRAP_INSTALL} \
    "

IMAGE_FEATURES = ""
IMAGE_LINGUAS = ""

LICENSE = "MIT"

IMAGE_FSTYPES = "${INITRAMFS_FSTYPES}"
# Initramfs images drop the ".rootfs" infix (image-artifact-names.bbclass
# convention) so the artifact is nanokvm-initramfs-image-${MACHINE}.cpio.gz --
# the name nanokvm-image.bb's IMAGE_BOOT_FILES references.
IMAGE_NAME_SUFFIX = ""
inherit core-image

IMAGE_ROOTFS_SIZE = "8192"
IMAGE_ROOTFS_EXTRA_SPACE = "0"

# No interactive login shell, no syslog: PID 1 is /init, nothing else runs.
BAD_RECOMMENDATIONS += "busybox-syslog"
