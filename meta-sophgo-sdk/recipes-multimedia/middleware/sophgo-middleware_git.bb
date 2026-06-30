SUMMARY = "Sophgo multimedia middleware (ISP, sensor, encoding) for SG200X"
LICENSE = "CLOSED"

inherit pkgconfig

# osdrv provides the cvitek kernel modules these libs talk to; the T-Head GCC is
# required because the C906 module sources + prebuilt ISP/audio algo libs use
# vendor ISA extensions upstream GCC cannot assemble/link.
# rsync-native: the kernel's headers_install target shells out to rsync.
# cmake-native: the bin module builds bundled json-c/miniz with cmake.
# python3(+jinja2)-native: the isp module generates pqtool_definition.json with a
#   `python hFile2json.py` script that imports jinja2.
DEPENDS = "virtual/kernel osdrv cvitek-thead-toolchain-native rsync-native cmake-native \
           python3-native python3-jinja2-native"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

# The module build needs three cvitek submodules checked out (the other ~19 are
# the 3rdparty/ + sample/ trees we don't build): bin's json-c/miniz, and the
# isp sensor support list (component/isp -> sensor_list.h etc.).
SRC_URI = "git://github.com/scpcom/sophgo-middleware;branch=maix_mmf-cvisdk;protocol=https;name=middleware \
           git://github.com/scpcom/json-c;branch=cvi;protocol=https;name=jsonc;destsuffix=git/modules/bin/json-c \
           git://github.com/scpcom/miniz;branch=cvi;protocol=https;name=miniz;destsuffix=git/modules/bin/miniz \
           git://github.com/scpcom/sophgo-SensorSupportList;branch=licheervnano-cvisdk;protocol=https;name=sensorlist;destsuffix=git/component/isp \
           file://cvi_build.config \
           file://xxd-shim"
SRCREV_middleware = "${AUTOREV}"
SRCREV_jsonc = "c106f8f83046516e62e3ec53e41d5d38724d0754"
SRCREV_miniz = "ac466c9e4fc2e5b5b706a704ea4143659fb6fae0"
SRCREV_sensorlist = "81ff6344f80cd31fbdc0aea1fb954dd030e03e5f"
SRCREV_FORMAT = "middleware"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

# SG2002 is the cv181x ASIC. The original build targets the musl/riscv64 SDK
# variant and builds with the T-Head vendor toolchain.
CHIP_ARCH = "CV181X"
CVIARCH = "CV181X"
SDK_VER = "musl_riscv64"
THEAD_TC_DIR = "${STAGING_DATADIR_NATIVE}/cvitek-thead-toolchain"

# Where the cvitek-style kernel UAPI headers (cvi_*.h etc.) are exported for the
# module compile, and where Makefile.param's `include $(BUILD_PATH)/.config`
# looks. Set up in do_configure.
CVI_KERNEL_HDR = "${WORKDIR}/kernel-hdr"
CVI_BUILD_PATH = "${WORKDIR}/cvi-build"

do_configure() {
    # 1) cvitek BUILD_PATH/.config that Makefile.param hard-includes.
    install -d "${CVI_BUILD_PATH}"
    install -m 0644 "${WORKDIR}/cvi_build.config" "${CVI_BUILD_PATH}/.config"

    # 2) Export the (cvitek) kernel UAPI headers the modules -I against.
    install -d "${CVI_KERNEL_HDR}"
    oe_runmake -C "${STAGING_KERNEL_DIR}" ARCH=riscv \
        INSTALL_HDR_PATH="${CVI_KERNEL_HDR}" headers_install

    # 3) The cvitek media UAPI headers (<linux/cvi_defines.h>, <linux/cif_uapi.h>,
    #    ...) are provided by osdrv, not the kernel, split across a common/ and a
    #    per-chip uapi/ tree. Merge both uapi trees' contents (each has a linux/
    #    subdir) into the kernel include dir so the modules' single
    #    -I$(KERNEL_INC) resolves them all.
    cp -a ${STAGING_INCDIR}/cvitek-osdrv/common/uapi/. "${CVI_KERNEL_HDR}/include/"
    cp -a ${STAGING_INCDIR}/cvitek-osdrv/chip/cv181x/uapi/. "${CVI_KERNEL_HDR}/include/"

    # The isp module's pqtool generator reads osdrv UAPI headers through a fixed
    # relative path (<middleware>/../osdrv/interdrv/include/...), assuming the
    # cvitek SDK layout where osdrv is a sibling of middleware. Mirror the staged
    # osdrv include tree there.
    install -d "${WORKDIR}/osdrv/interdrv/include"
    cp -a ${STAGING_INCDIR}/cvitek-osdrv/. "${WORKDIR}/osdrv/interdrv/include/"

    # 4) The bin module builds bundled json-c/miniz with cmake, but the cvitek
    #    LDFLAGS (`-shared --gc-sections ...`, meant for ld) is re-exported into
    #    those cmake sub-shells; cmake links its compiler probe via the gcc
    #    driver, which rejects bare `--gc-sections`. Clear LDFLAGS for just those
    #    cmake/make-install invocations (idempotent).
    if ! grep -q "LDFLAGS= cmake" "${S}/modules/bin/Makefile"; then
        sed -i -e 's/ cmake / LDFLAGS= cmake /g' \
               -e 's/ make install/ LDFLAGS= make install/g' \
               "${S}/modules/bin/Makefile"
    fi
}

# Common make args: select the musl/riscv64 SDK build, point it at the T-Head
# toolchain + its sysroot, the exported kernel headers, and the build config.
# These override the cvitek-SDK-tree paths hard-coded in Makefile.param.
CVI_MW_OEMAKE = "\
    CHIP_ARCH=${CHIP_ARCH} \
    CVIARCH=${CVIARCH} \
    SDK_VER=${SDK_VER} \
    CROSS_COMPILE_MUSL_RISCV64=riscv64-unknown-linux-musl- \
    SYSROOT=${THEAD_TC_DIR}/sysroot \
    KERNEL_DIR=${STAGING_KERNEL_DIR} \
    KERNEL_INC=${CVI_KERNEL_HDR}/include \
    BUILD_PATH=${CVI_BUILD_PATH} \
    CVI_TARGET_PACKAGES_INCLUDE= \
    CVI_TARGET_PACKAGES_LIBDIR= \
"

do_compile() {
    # The isp module's generate_toolJson.sh invokes `python` (with jinja2); the
    # task PATH only exposes python3. Back a `python` alias with the OE native
    # python3 (which has jinja2 staged via python3-jinja2-native).
    install -d "${WORKDIR}/pybin"
    ln -sf "${STAGING_BINDIR_NATIVE}/python3-native/python3" "${WORKDIR}/pybin/python"
    # generate_toolJson.sh also calls `xxd -i` (absent from the task PATH) to
    # embed pqtool_definition.json; provide a self-contained shim.
    install -m 0755 "${WORKDIR}/xxd-shim" "${WORKDIR}/pybin/xxd"
    export PATH="${WORKDIR}/pybin:${THEAD_TC_DIR}/bin:${PATH}"

    # Build only the middleware libraries (modules/). The top-level `module`
    # target would also pull in 3rdparty/ (opencv, ffmpeg, ... already provided
    # by OE) and sample/ demo apps, which the image does not need.
    oe_runmake -C ${S} ${CVI_MW_OEMAKE} prepare
    oe_runmake -C ${S}/modules ${CVI_MW_OEMAKE}
}

do_install() {
    # Middleware shared libraries + their 3rd-party deps.
    install -d ${D}${libdir}/3rd
    find ${S}/lib -maxdepth 1 -name "*.so*" -exec install -m 0755 {} ${D}${libdir}/ \; || true
    find ${S}/lib/3rd -maxdepth 1 -name "*.so*" -exec install -m 0755 {} ${D}${libdir}/3rd/ \; || true

    # Public headers (consumed by cvi-rtsp, maix-cdk, nanokvm-server).
    install -d ${D}${includedir}
    cp -a ${S}/include/. ${D}${includedir}/ || true
}

# Prebuilt vendor .so (ISP/audio algo) ship without GNU_HASH / are pre-stripped;
# they are not cross-linked against the OE sysroot.
INSANE_SKIP:${PN} = "ldflags already-stripped textrel"
FILES:${PN} = "${libdir}"
FILES:${PN}-dev = "${includedir}"
