SUMMARY = "Factory content of the data partition: the Pi boot image"
DESCRIPTION = "Stages the aarch64 U-Boot boot image (built by the rpi \
multiconfig from the vendored meta-raspberrypi layer) as the factory content \
of the SD image's data partition (p4). wic packs the staged directory into \
p4 via '--source rootfs' (see wic/nanokvm-sd.wks.in), so a freshly flashed \
card already carries /var/lib/nanokvm/firmware/uboot-rpi.img -- the file the \
NanoKVM server presents to the USB mass-storage gadget's lun.0 (config \
default Firmware.ImagePath) -- and first boot only grows the partition, \
copying nothing. The server re-downloads the image only if the data \
partition is ever lost."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# Pure data carrier: nothing is compiled and nothing lands in any rootfs (wic
# consumes the deploy directory directly, so there is no package). xz-native
# decompresses the Pi image, which the rpi64 machine conf emits as .wic.xz --
# p4 must hold the raw bytes the gadget serves.
inherit allarch deploy nopackages
DEPENDS = "xz-native"

do_configure[noexec] = "1"
do_compile[noexec] = "1"
do_install[noexec] = "1"

# The Pi image is produced under the "rpi" multiconfig. Its deploy directory
# is that config's TMPDIR (set in meta-nanokvm/conf/multiconfig/rpi.conf) with
# the standard per-MACHINE subdir. TOPDIR is shared across multiconfigs, so this
# path is stable; keep it in sync with TMPDIR in that conf.
RPI_DEPLOY = "${TOPDIR}/tmp-rpi/deploy/images/rpi64"

# Build the Pi image (whole config) before staging. mcdepends is
# signature-aware, so a change to the Pi image restages this too.
do_deploy[mcdepends] = "mc::rpi:rpi-uboot-image:do_image_complete"

do_deploy() {
    # Prefer the stable IMAGE_LINK_NAME symlink; fall back to the newest
    # timestamped file if the symlink naming ever changes.
    src="${RPI_DEPLOY}/rpi-uboot-image-rpi64.rootfs.wic.xz"
    if [ ! -e "$src" ]; then
        src=$(ls -t ${RPI_DEPLOY}/rpi-uboot-image-rpi64*.wic.xz 2>/dev/null | head -1)
    fi
    if [ -z "$src" ] || [ ! -e "$src" ]; then
        bbfatal "rpi-firmware-seed: no Pi boot image (*.wic.xz) found in ${RPI_DEPLOY}."
    fi

    # The staged tree mirrors the data partition root: wic's rootfs source
    # plugin packs it verbatim, so this file becomes
    # /var/lib/nanokvm/firmware/uboot-rpi.img at runtime.
    install -d ${DEPLOYDIR}/nkvm-data-root/firmware
    xz -dc "$src" > ${DEPLOYDIR}/nkvm-data-root/firmware/uboot-rpi.img
    chmod 0644 ${DEPLOYDIR}/nkvm-data-root/firmware/uboot-rpi.img
}
addtask deploy after do_compile before do_build
