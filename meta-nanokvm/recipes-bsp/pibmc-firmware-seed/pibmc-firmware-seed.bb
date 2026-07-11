SUMMARY = "Seed copy of the Raspberry Pi boot image served by the USB gadget"
DESCRIPTION = "Ships the aarch64 U-Boot boot image (built by the pi-bmc \
multiconfig from the vendored meta-pibmc layer) inside the NanoKVM rootfs, gzip- \
compressed. On first boot S01fs decompresses it to /data/firmware/uboot-rpi.img \
-- the path the NanoKVM server presents to the USB mass-storage gadget's lun.0 \
(config default Firmware.ImagePath). Because the server only downloads its \
firmware image when that file is absent, seeding it makes the managed Pi boot \
from our locally-built U-Boot with no runtime network fetch."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Pure data carrier: no compilation of our own, and machine-independent (the
# payload is an opaque disk image). allarch keeps it out of the riscv tune.
# xz-native supplies `xz` to decompress the Pi image, which the pi-bmc machine
# conf emits as .wic.xz; we recompress it to gzip so busybox `zcat` on the
# target (no `unxz`) can expand the seed on first boot.
inherit allarch
DEPENDS = "xz-native"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

# The Pi image is produced under the "pi-bmc" multiconfig. Its deploy directory
# is that config's TMPDIR (set in meta-nanokvm/conf/multiconfig/pi-bmc.conf) with
# the standard per-MACHINE subdir. TOPDIR is shared across multiconfigs, so this
# path is stable; keep it in sync with TMPDIR in that conf.
PIBMC_DEPLOY = "${TOPDIR}/tmp-pi-bmc/deploy/images/pi-bmc-rpi64"

# Build the Pi image (whole config) before installing. mcdepends is
# signature-aware, so a change to the Pi image rebuilds this package too.
do_install[mcdepends] = "mc::pi-bmc:pibmc-uboot-image:do_image_complete"

do_install() {
    # Prefer the stable IMAGE_LINK_NAME symlink; fall back to the newest
    # timestamped file if the symlink naming ever changes.
    src="${PIBMC_DEPLOY}/pibmc-uboot-image-pi-bmc-rpi64.rootfs.wic.xz"
    if [ ! -e "$src" ]; then
        src=$(ls -t ${PIBMC_DEPLOY}/pibmc-uboot-image-pi-bmc-rpi64*.wic.xz 2>/dev/null | head -1)
    fi
    if [ -z "$src" ] || [ ! -e "$src" ]; then
        bbfatal "pibmc-firmware-seed: no Pi boot image (*.wic.xz) found in ${PIBMC_DEPLOY}."
    fi

    # Transcode xz -> gzip so the target's busybox zcat can expand it (busybox
    # here has no unxz). The image is a mostly-empty 512M FAT, so the .gz stays
    # small despite gzip being less dense than xz.
    install -d ${D}${datadir}/pibmc
    xz -dc "$src" | gzip -9 -c > ${D}${datadir}/pibmc/uboot-rpi.img.gz
    chmod 0644 ${D}${datadir}/pibmc/uboot-rpi.img.gz
}

FILES:${PN} = "${datadir}/pibmc/uboot-rpi.img.gz"

# It is just a compressed blob; skip the ELF-oriented QA.
INSANE_SKIP:${PN} += "arch"
