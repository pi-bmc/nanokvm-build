SUMMARY = "Sophgo OS-level kernel modules for SG200X"
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://LICENSE;md5=b234ee4d69f5fce4486a80fdaf4a4263"

inherit module

DEPENDS = "virtual/kernel"

SRC_URI = "git://github.com/scpcom/sophgo-osdrv;branch=licheervnano-cvisdk;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

EXTRA_OEMAKE += "KERNEL_DIR=${STAGING_KERNEL_DIR} \
                 CROSS_COMPILE=${TARGET_PREFIX}"

MODULES_MODULE_SYMVERS_LOCATION = "."

do_compile() {
    oe_runmake -C ${S} \
        KERNEL_SRC=${STAGING_KERNEL_DIR} \
        CROSS_COMPILE=${TARGET_PREFIX} \
        all
}

do_install() {
    install -d ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/sophgo/
    find ${S} -name "*.ko" -exec \
        install -m 0644 {} ${D}${nonarch_base_libdir}/modules/${KERNEL_VERSION}/extra/sophgo/ \;
}

FILES:${PN} = "${nonarch_base_libdir}/modules"
