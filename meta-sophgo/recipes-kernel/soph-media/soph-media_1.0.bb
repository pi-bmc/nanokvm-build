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

# VPU firmware.
#
# The WAVE420L (H.265) and coda980 (H.264) cores have no ROM: cvi_vc_drv uploads
# a firmware image to each on the first VENC_CREATE_CHN, and without it the core
# never comes out of reset -- which looks like a channel that creates and then
# produces nothing. The driver reads them with filp_open() from the absolute
# paths in cvi_vc_drv/vcodec/config.h, so they have to exist in the rootfs at
# ${datadir}/fw_vcodec by the time capture starts. (Building them into the .ko
# instead, by defining FIRMWARE_H, is the vendor's other option; it costs ~390
# KiB of permanently resident kernel memory on a board that has already given
# 105 MiB of its 256 to the ION carveout, so the files win.)
#
# Upstream ships the blobs only as C arrays inside those headers, so extract
# them back out at build time rather than committing a second copy that could
# drift from the header the driver would compile against. The md5s are the ones
# the vendor documents beside each path in config.h; checking them here turns a
# mangled or truncated vendored header into a build failure instead of a VPU
# that silently never starts.
#
# NB these blobs are Chips&Media firmware -- proprietary, no SPDX, no source.
# They fall under the same licensing review the cvi_vc_drv note above calls for.
# A separate task rather than do_install:append: do_install is a shell function
# here, and bitbake cannot append Python to one.
python do_install_vpu_firmware() {
    import hashlib
    import os
    import re

    helper = os.path.join(d.getVar('S'), 'cvi_vc_drv', 'vcodec', 'sample', 'helper')
    dest = os.path.join(d.getVar('D') + d.getVar('datadir'), 'fw_vcodec')
    bb.utils.mkdirhier(dest)

    firmware = (
        ('fw_h265.h', 'monet.bin', '202043a809fdfa607a2d01d179aa7c3d'),
        ('fw_h264.h', 'coda980.bin', '02d773cb7b1ef926c7f1e30e5b7918f8'),
    )

    for header, blob, expected in firmware:
        path = os.path.join(helper, header)
        with open(path, 'r') as f:
            text = f.read()

        # One array per header, "CVI_U8 fw_hXXX[] = { 0x.., ... };". Slice to
        # the braces first so a stray hex literal in a comment or an #ifdef
        # outside the initialiser cannot contribute a byte.
        start = text.index('= {') + len('= {')
        body = text[start:text.index('};', start)]
        data = bytes(int(b, 16) for b in re.findall(r'0x([0-9A-Fa-f]{2})', body))

        got = hashlib.md5(data).hexdigest()
        if got != expected:
            bb.fatal('%s: extracted %d bytes with md5 %s, expected %s -- the '
                     'vendored header does not match the firmware config.h '
                     'documents' % (header, len(data), got, expected))

        with open(os.path.join(dest, blob), 'wb') as f:
            f.write(data)
        os.chmod(os.path.join(dest, blob), 0o644)

        bb.note('soph-media: extracted %s (%d bytes) from %s' % (blob, len(data), header))
}
addtask install_vpu_firmware after do_install before do_populate_sysroot do_package

# Ship the firmware as its own package rather than folding it into ${PN}.
#
# Adding ${datadir}/fw_vcodec to FILES:${PN} does not work here, which is worth
# recording because it looks like it should: `bitbake -e` shows the append
# landing (FILES:soph-media=" /usr/share/fw_vcodec"), and do_package still fails
# the installed-vs-shipped check with the blobs unclaimed. Something between
# module.bbclass's FILES:${PN} = "" and kernel-module-split's do_package-time
# rewrite of the meta package's FILES is dropping it; the exact mechanism was
# not chased down, because a dedicated package is the better shape regardless --
# these are vendor blobs of unestablished redistribution status, and a product
# image should be able to account for them apart from the GPL modules.
#
# PACKAGES =+ puts it ahead of ${PN} so it claims these files first.
PACKAGES =+ "${PN}-firmware"
FILES:${PN}-firmware = "${datadir}/fw_vcodec"

# The modules are useless without it -- the VPU never leaves reset -- so make
# it a hard dependency rather than trusting the image to list both.
RDEPENDS:${PN} += "${PN}-firmware"

# ION is NOT here: it has to be built into the kernel (see meta-nanokvm patch
# 0009). It calls cma_alloc()/plist_add()/arch_sync_dma_for_device(), none of
# which mainline exports to modules, so it cannot be a .ko at all. Patch 0010
# exports the two DMA cache hooks these modules do need.
#
# Load order matters at runtime and mirrors the build order above; nothing here
# autoloads, since the pipeline is brought up by the service that owns capture.
RPROVIDES:${PN} += "kernel-module-soph-media"

COMPATIBLE_MACHINE = "sg2002-licheervnano"
