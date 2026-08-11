SUMMARY = "CVITek/Sophgo multimedia kernel modules (CV181x/SG2002), ported to 6.18"
DESCRIPTION = "\
The out-of-tree CVITek pipeline that gives the SG2002 local HDMI capture and \
hardware H.264/H.265 encode: buffer pool (base/vb), system/clock glue (sys), \
sensor I2C wrapper, MIPI CSI-2 receiver (cif -> cvi_mipi_rx), video input (vi), \
scaler/format converter (vpss), and the Chips&Media WAVE4 VPU (vcodec, jpeg, \
cvi_vc_drv). None of this exists in mainline -- Sophgo's own upstreaming \
tracker still lists Media as Not Started -- so the vendor sources are carried \
here, forward-ported from their 5.10 base."

# The vendor sources carry a Cvitek copyright header and MODULE_LICENSE("GPL")
# but, outside the few files we added, no SPDX tags. Tracked as GPL-2.0-only on
# the strength of the MODULE_LICENSE assertion; note that cvi_vc_drv embeds the
# Chips&Media VPU SDK, whose own headers are proprietary and carry no SPDX --
# worth a licensing review before this is redistributed in a product image.
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://jpeg/jpeg_common.c;beginline=1;endline=6;md5=59b1383d9367ddc468424866a26f2aa2"

inherit module

# Package all nine modules as one package (this recipe's own ${PN}, since
# module.bbclass sets KERNEL_MODULES_META_PACKAGE = "${PN}") instead of letting
# kernel-module-split.bbclass emit a kernel-module-<name>-${KERNEL_VERSION}
# package per .ko.
#
# This is a size fix, not tidiness. do_split_packages() is called with
# extra_depends='kernel-${KERNEL_VERSION}', so every split package RDEPENDS on
# the kernel package, which drags kernel-image-image into the rootfs: a second
# 9.6 MiB copy of Image in /boot that the image's own vfat /boot mount then
# shadows, for ~4.9 MiB of squashfs. The image's BAD_RECOMMENDATIONS cannot
# stop it -- that filters RRECOMMENDS, and this is a hard RDEPENDS. Setting
# KERNEL_SPLIT_MODULES=0 takes the early-return path in the bbclass, which
# packages ${nonarch_base_libdir}/modules into ${PN} and never adds that
# dependency.
#
# Dropping the kernel RDEPENDS costs nothing here: the kernel is delivered on
# the FAT boot partition by wic (IMAGE_BOOT_FILES), never as a rootfs package,
# so the dependency could not have enforced an ABI match anyway. The match is
# guaranteed by building against STAGING_KERNEL_DIR.
KERNEL_SPLIT_MODULES = "0"

# Upstream: github.com/sophgo/osdrv, branch sg200x-dev, commit
# aa542c41df94f7bc656cb740f6622a5dca7dc403 ("osdrv: weekly rls 2026.06.30"),
# subdirectory interdrv/. That is Sophgo's own repo, not a board-vendor fork:
# Milk-V's duo-buildroot-sdk-v2 and Sipeed's LicheeRV-Nano-Build both vendor a
# copy of it, and the scpcom fork additionally ships cvi_vc_drv as a prebuilt
# blob rather than source.
#
# The sources are vendored rather than fetched because they are not consumed
# as-is: the forward-port to 6.18 touches files throughout the tree, and the
# upstream repo has no branch or tag that carries those changes. Keeping the
# ported tree in the layer is what makes the delta reviewable in our history
# instead of hiding it in a pile of patch files applied to a moving target.
#
# To re-derive against a newer upstream:
#   git clone -b sg200x-dev https://github.com/sophgo/osdrv
#   diff -ru osdrv/interdrv <this>/files
# Fetched as a named directory rather than "file://." so it unpacks into a
# predictable subdirectory: with "." the tree lands directly in ${WORKDIR}
# alongside temp/, recipe-sysroot/ and friends, and S/LIC_FILES_CHKSUM paths
# stop lining up.
SRC_URI = "file://soph-media"

# NB: UNPACKDIR does not exist in scarthgap (added in a later release), so
# file:// entries land in ${WORKDIR} and S must be spelled that way.
S = "${WORKDIR}/soph-media"

# CV181X is the family the SG2002 belongs to; CVIARCH_L is its lowercase form,
# used for the per-chip subdirectories (chip/cv181x, hal/cv181x).
CVIARCH = "CV181X"
CVIARCH_L = "cv181x"
EXTRA_OEMAKE += "CVIARCH=${CVIARCH} CVIARCH_L=${CVIARCH_L}"

# Build order is a real dependency chain, not a preference: base and sys export
# the symbols everything else links against, and the codec modules resolve
# against base/sys/vcodec. Each module's Module.symvers is fed to the next.
SOPH_MEDIA_MODULES = "sys base snsr_i2c cif vi vpss vcodec jpeg cvi_vc_drv"

# PWD has to be passed explicitly. The vendor Makefiles build their include
# paths out of $(PWD) -- e.g. base/Makefile has -I$(PWD)/chip/$(CVIARCH_L) and
# -I$(PWD)/../include/common/uapi -- which only resolves if make's PWD is the
# module directory. oe_runmake runs from the recipe's working directory, so
# without this every module fails on the first vendor header it includes.
do_compile() {
    symvers=""
    for m in ${SOPH_MEDIA_MODULES}; do
        oe_runmake -C ${STAGING_KERNEL_DIR} M=${S}/$m PWD=${S}/$m \
            ${EXTRA_OEMAKE} \
            KBUILD_EXTRA_SYMBOLS="$symvers" \
            modules
        if [ -f "${S}/$m/Module.symvers" ]; then
            symvers="$symvers ${S}/$m/Module.symvers"
        fi
    done
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra
    for m in ${SOPH_MEDIA_MODULES}; do
        find ${S}/$m -name '*.ko' -exec \
            install -m 0644 {} ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/ \;
    done
}

# ION is NOT here: it has to be built into the kernel (see meta-nanokvm patch
# 0009). It calls cma_alloc()/plist_add()/arch_sync_dma_for_device(), none of
# which mainline exports to modules, so it cannot be a .ko at all. Patch 0010
# exports the two DMA cache hooks these modules do need.
#
# Load order matters at runtime and mirrors the build order above; nothing here
# autoloads, since the pipeline is brought up by the service that owns capture.
RPROVIDES:${PN} += "kernel-module-soph-media"

COMPATIBLE_MACHINE = "sg2002-licheervnano"
