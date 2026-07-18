SUMMARY = "Raspberry Pi VideoCore boot firmware blobs"
DESCRIPTION = "start*.elf / fixup*.dat / bootcode.bin and the stock firmware \
overlays from raspberrypi/firmware, staged for the boot partition."
HOMEPAGE = "https://github.com/raspberrypi/firmware"
LICENSE = "Proprietary"
# md5 tracks raspberrypi/firmware boot/LICENCE.broadcom. It drifts whenever the
# fetched ref's license text changes (RPI_FIRMWARE_REF = "master" moves), even
# though the terms stay the same Broadcom/RPi proprietary "binary-only, Raspberry
# Pi devices only" license -- so LICENSE = "Proprietary" remains correct. Re-pin
# this md5 after re-verifying the text; pinning RPI_FIRMWARE_REF to a tag/sha
# stops the drift entirely.
LIC_FILES_CHKSUM = "file://boot/LICENCE.broadcom;md5=c403841ff2837657b2ed8e5bb474ac8d"

# Branch, tag or full sha of raspberrypi/firmware; overridden via the kas
# env passthrough. NOTE: when this is a branch name ("master"), the tarball
# is cached in DL_DIR by name - pin a sha/tag for reproducible builds.
RPI_FIRMWARE_REF ?= "master"

SRC_URI = "https://github.com/raspberrypi/firmware/archive/${RPI_FIRMWARE_REF}.tar.gz;downloadfilename=rpi-firmware-${RPI_FIRMWARE_REF}.tar.gz;subdir=firmware-src;striplevel=1"
BB_STRICT_CHECKSUM = "ignore"

S = "${WORKDIR}/firmware-src"

inherit deploy allarch nopackages

do_configure[noexec] = "1"
do_compile[noexec] = "1"
do_install[noexec] = "1"

do_deploy() {
    install -d ${DEPLOYDIR}/rpi-firmware/overlays
    install -m 0644 ${S}/boot/*.elf ${DEPLOYDIR}/rpi-firmware/
    install -m 0644 ${S}/boot/*.dat ${DEPLOYDIR}/rpi-firmware/
    install -m 0644 ${S}/boot/*.bin ${DEPLOYDIR}/rpi-firmware/
    cp -r --no-preserve=ownership ${S}/boot/overlays/. ${DEPLOYDIR}/rpi-firmware/overlays/
}
addtask deploy after do_patch before do_build

