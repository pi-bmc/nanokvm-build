SUMMARY = "Seed copy of the Raspberry Pi boot image served by the USB gadget"
DESCRIPTION = "Ships the aarch64 U-Boot boot image (built by the rpi \
multiconfig from the vendored meta-raspberrypi layer) inside the NanoKVM \
rootfs as the .wic.xz the rpi build emitted. At startup the NanoKVM server \
(server/service/firmware, config default Firmware.SeedPath) decompresses it \
in-process to Firmware.ImagePath -- the file presented to the USB \
mass-storage gadget's lun.0 -- whenever that image is absent, so a \
factory-fresh BMC boots the managed Pi from our locally-built U-Boot with \
no runtime network fetch."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Pure data carrier: no compilation of our own, and machine-independent (the
# payload is an opaque disk image). allarch keeps it out of the riscv tune.
# The .wic.xz is shipped byte-for-byte: the server links an xz reader
# (ulikunitz/xz, the same one its firmware downloads use), so the old
# xz->gzip transcode for busybox zcat is gone with the seeding init script.
inherit allarch

do_configure[noexec] = "1"
do_compile[noexec] = "1"

# The Pi image is produced under the "rpi" multiconfig. Its deploy directory
# is that config's TMPDIR (set in meta-nanokvm/conf/multiconfig/rpi.conf) with
# the standard per-MACHINE subdir. TOPDIR is shared across multiconfigs, so this
# path is stable; keep it in sync with TMPDIR in that conf.
RPI_DEPLOY = "${TOPDIR}/tmp-rpi/deploy/images/rpi64"

# Build the Pi image (whole config) before installing. mcdepends is
# signature-aware, so a change to the Pi image rebuilds this package too.
do_install[mcdepends] = "mc::rpi:rpi-uboot-image:do_image_complete"

do_install() {
    # Prefer the stable IMAGE_LINK_NAME symlink; fall back to the newest
    # timestamped file if the symlink naming ever changes.
    src="${RPI_DEPLOY}/rpi-uboot-image-rpi64.rootfs.wic.xz"
    if [ ! -e "$src" ]; then
        src=$(ls -t ${RPI_DEPLOY}/rpi-uboot-image-rpi64*.wic.xz 2>/dev/null | head -1)
    fi
    if [ -z "$src" ] || [ ! -e "$src" ]; then
        bbfatal "rpi-firmware-seed: no Pi boot image (*.wic.xz) found in ${RPI_DEPLOY}."
    fi

    install -d ${D}${datadir}/rpi
    install -m 0644 "$src" ${D}${datadir}/rpi/uboot-rpi.img.xz
}

# Path is the server's config default Firmware.SeedPath; keep them in sync.
FILES:${PN} = "${datadir}/rpi/uboot-rpi.img.xz"

# It is just a compressed blob; skip the ELF-oriented QA.
INSANE_SKIP:${PN} += "arch"
