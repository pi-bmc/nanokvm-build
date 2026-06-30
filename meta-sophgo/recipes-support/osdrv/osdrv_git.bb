SUMMARY = "Sophgo OS-level kernel modules for SG200X"
LICENSE = "CLOSED"

inherit module

DEPENDS = "virtual/kernel"

SRC_URI = "git://github.com/scpcom/sophgo-osdrv;branch=licheervnano-cvisdk;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

EXTRA_OEMAKE += "KERNEL_DIR=${STAGING_KERNEL_DIR} \
                 CROSS_COMPILE=${TARGET_PREFIX}"

# The top Makefile builds KO_LIST (base vcodec jpeg ...) as plain prerequisites
# with no inter-target ordering, yet jpeg/cvi_vc_drv consume ../vcodec/Module.symvers
# (which exports tWaitQueue) via KBUILD_EXTRA_SYMBOLS. A parallel top-level make
# races jpeg ahead of vcodec -> "modpost: tWaitQueue undefined". Serialise the
# top-level build (KO_LIST is already in dependency order); each module still
# builds in parallel internally (MAKE_KO uses -j$(nproc)).
PARALLEL_MAKE = ""

MODULES_MODULE_SYMVERS_LOCATION = "."

# SG2002 is in the cv181x family. The osdrv sub-makefiles select per-SoC source
# subdirs via CVIARCH_L (e.g. interdrv/saradc/cv181x) and gate code on CVIARCH
# (-D__CV181X__), and they invoke kbuild as `make ARCH=$(ARCH) -C $(KERNEL_DIR)`
# -- with ARCH unset the kernel resolves arch//Makefile and fails. Pass ARCH and
# the cvitek chip identifiers through.
do_compile() {
    # INSTALL_DIR is where each module's *.ko are collected. It defaults to the
    # relative "ko", but MAKE_KO copies with `cd interdrv/<mod> && cp *.ko ko`,
    # so a module emitting >1 .ko (e.g. vo -> soph_vo.ko + soph_mipi_tx.ko) makes
    # `cp` treat "ko" as a directory that does not exist. Give it an absolute dir
    # (the prepare target mkdir -p's it).
    oe_runmake -C ${S} \
        ARCH=${ARCH} \
        CROSS_COMPILE=${TARGET_PREFIX} \
        KERNEL_DIR=${STAGING_KERNEL_DIR} \
        CVIARCH=CV181X \
        CHIP_ARCH=CV181X \
        INSTALL_DIR=${WORKDIR}/osdrv-ko \
        all
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/sophgo/
    find ${S} -name "*.ko" -exec \
        install -m 0644 {} ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/sophgo/ \;

    # Stage the cvitek media UAPI/kernel-API headers (linux/cvi_*.h, chip + common)
    # for userspace consumers built against these drivers (sophgo-middleware).
    install -d ${D}${includedir}/cvitek-osdrv
    cp -a ${S}/interdrv/include/. ${D}${includedir}/cvitek-osdrv/
}

FILES:${PN} = "${nonarch_base_libdir}/modules"
FILES:${PN}-dev = "${includedir}/cvitek-osdrv"
