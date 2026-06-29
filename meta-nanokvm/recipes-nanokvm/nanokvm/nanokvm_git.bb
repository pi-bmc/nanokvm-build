SUMMARY = "NanoKVM server — IP KVM for LicheeRV Nano"
HOMEPAGE = "https://github.com/sipeed/NanoKVM"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=c75605ca0cdc68090b9bb68e6e587bdd"

inherit go-module systemd

GO_IMPORT = "github.com/sipeed/NanoKVM"

SRC_URI = "git://github.com/sipeed/NanoKVM;branch=main;protocol=https"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# Disable vendor mode; go.sum is present in upstream source.
GO_WORKDIR = "${S}"

do_compile() {
    cd ${S}
    ${GO} build ${GOFLAGS} -o ${B}/nanokvm ./... || \
        ${GO} build ${GOFLAGS} -o ${B}/nanokvm .
}

do_install() {
    install -d ${D}${bindir}
    install -m 0755 ${B}/nanokvm ${D}${bindir}/nanokvm

    # Web UI assets
    if [ -d ${S}/web ]; then
        install -d ${D}${datadir}/nanokvm/web
        cp -r ${S}/web/* ${D}${datadir}/nanokvm/web/
    fi

    # Default config
    if [ -d ${S}/kvmd ]; then
        install -d ${D}${sysconfdir}/nanokvm
        cp -r ${S}/kvmd/* ${D}${sysconfdir}/nanokvm/ || true
    fi

    # Init script (SysV)
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/nanokvm.init ${D}${sysconfdir}/init.d/S99nanokvm
}

SYSTEMD_SERVICE:${PN} = "nanokvm.service"

FILES:${PN} = " \
    ${bindir}/nanokvm \
    ${datadir}/nanokvm \
    ${sysconfdir}/nanokvm \
    ${sysconfdir}/init.d/S99nanokvm \
    "
