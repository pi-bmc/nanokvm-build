SUMMARY = "Sophgo CVI RTSP server library for SG200X"
LICENSE = "CLOSED"

inherit pkgconfig

# Links the middleware libs + live555 (C++), so it must use the same T-Head GCC
# as sophgo-middleware (C++ ABI / vendor ISA). rsync-native for headers_install.
DEPENDS = "sophgo-middleware cvitek-thead-toolchain-native rsync-native"

# live555 (the RTSP/RTP C++ stack cvi_rtsp links statically) is a middleware
# submodule; fetch it alongside and build it inline with the same toolchain.
SRC_URI = "git://github.com/scpcom/cvi_rtsp;branch=licheervnano-cvisdk;protocol=https;name=rtsp \
           git://github.com/scpcom/live555;branch=sg200x-dev;protocol=https;name=live555;destsuffix=git/live555-src"
SRCREV_rtsp = "${AUTOREV}"
SRCREV_live555 = "1c4bef2e9d856c51c0d9bb43f776d623eb5b542b"
SRCREV_FORMAT = "rtsp"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# OE's LDFLAGS carry GCC-13 options (e.g. -fcanon-prefix-map) the T-Head GCC 10.2
# rejects. Empty the exported LDFLAGS so the Makefile's `LDFLAGS += $(LIVE555_LIB)`
# starts clean; the recipe passes the libs it needs explicitly.
LDFLAGS = ""

CHIP_ARCH = "CV181X"
SDK_VER = "musl_riscv64"
THEAD_TC_DIR = "${STAGING_DATADIR_NATIVE}/cvitek-thead-toolchain"
THEAD_PREFIX = "riscv64-unknown-linux-musl-"
MW_DIR = "${STAGING_DATADIR}/cvi_mpi"
CVI_KERNEL_HDR = "${WORKDIR}/kernel-hdr"
LIVE555_OUT = "${WORKDIR}/live555-out/usr/local"
THEAD_MARCH = "-march=rv64imafdcv0p7xthead -mabi=lp64d -mcmodel=medany"

do_configure() {
    # Export the cvitek kernel UAPI headers the rtsp sources -I against (same as
    # sophgo-middleware: kernel headers_install + osdrv cvi_*.h merged in).
    install -d "${CVI_KERNEL_HDR}"
    oe_runmake -C "${STAGING_KERNEL_DIR}" ARCH=riscv \
        INSTALL_HDR_PATH="${CVI_KERNEL_HDR}" headers_install
    cp -a ${STAGING_INCDIR}/cvitek-osdrv/common/uapi/. "${CVI_KERNEL_HDR}/include/"
    cp -a ${STAGING_INCDIR}/cvitek-osdrv/chip/cv181x/uapi/. "${CVI_KERNEL_HDR}/include/"

}

do_compile() {
    export PATH="${THEAD_TC_DIR}/bin:${PATH}"

    # 1) Build live555 (static libUsageEnvironment/groupsock/liveMedia/...).
    #    config.linux compiles via the env $(CXX), which OE sets to its own
    #    riscv64-oe g++ (whose assembler rejects the T-Head -march). Force the
    #    T-Head toolchain on the make command line (overrides the OE env).
    ( cd "${S}/live555-src" && ./genMakefiles linux )
    oe_runmake -C "${S}/live555-src" install \
        CC="${THEAD_PREFIX}gcc" \
        CXX="${THEAD_PREFIX}g++" \
        AR="${THEAD_PREFIX}ar" \
        RANLIB="${THEAD_PREFIX}ranlib" \
        LD="${THEAD_PREFIX}ld" \
        CFLAGS="${THEAD_MARCH} -g0 -fPIC" \
        CXXFLAGS="${THEAD_MARCH} -g0 -fPIC" \
        CPPFLAGS="" LDFLAGS="" \
        DESTDIR="${WORKDIR}/live555-out"

    # 2) Build the cvi_rtsp library against the staged middleware + live555.
    #    Set MW_INC/MW_LIB directly (the Makefile would otherwise derive them via
    #    `pkg-config cvi_common cvi_sample`, whose paths OE's pkg-config sysroot
    #    rewrite would mangle).
    mw_inc="-I${MW_DIR}/include -I${MW_DIR}/include/isp/cv181x -I${CVI_KERNEL_HDR}/include -I${MW_DIR}/include/linux"
    mw_lib="-L${MW_DIR}/lib -lsys -lvi -lvo -lvpss -lrgn -lgdc -lvenc -lcvi_bin -lcvi_bin_isp -lisp -lisp_algo -lae -laf -lawb -lsns_full -latomic -lsample -L${MW_DIR}/lib/3rd -lini"
    oe_runmake -C ${S} \
        CHIP_ARCH=${CHIP_ARCH} \
        SDK_VER=${SDK_VER} \
        CROSS_COMPILE=${THEAD_PREFIX} \
        CC="${THEAD_PREFIX}gcc" \
        CXX="${THEAD_PREFIX}g++" \
        AR="${THEAD_PREFIX}ar" \
        RANLIB="${THEAD_PREFIX}ranlib" \
        LD="${THEAD_PREFIX}ld" \
        LIVE555_DIR="${LIVE555_OUT}" \
        MW_DIR="${MW_DIR}" \
        KERNEL_INC="${CVI_KERNEL_HDR}/include" \
        MW_INC="${mw_inc}" \
        MW_LIB="${mw_lib}" \
        all
}

do_install() {
    install -d ${D}${libdir}
    find ${S}/src -maxdepth 1 \( -name "libcvi_rtsp.so" -o -name "libcvi_rtsp.a" \) \
        -exec install -m 0755 {} ${D}${libdir}/ \;

    install -d ${D}${includedir}
    cp -a ${S}/include/cvi_rtsp ${D}${includedir}/ || true
}

# T-Head-built binaries: same vendor-binary handling as sophgo-middleware.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"
INSANE_SKIP:${PN} = "ldflags already-stripped textrel"
INSANE_SKIP:${PN}-dev = "dev-so dev-elf"
FILES:${PN} = "${libdir}"
FILES:${PN}-dev = "${includedir}"
