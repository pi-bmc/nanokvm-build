# The cvitek U-Boot for SG2002 includes generated headers that the upstream
# sophgo-build Makefile produces before compiling:
#   - cvipart.h          (mkcvipart.py   <- partition_sd.xml)
#   - imgs.h             (mk_imgHeader.py <- partition_sd.xml)
#   - cvi_board_memmap.h (mmap_conv.py   <- board memmap.py)
# include/configs/mars-asic.h #includes these, so the build fails without them.
# The board's other files (cvi_board_init.c, cvitek.h, cvi_panels/) already ship
# in the licheervnano-cvisdk-2021.10 branch. The headers are deterministic for
# this board, so they are generated once and vendored here, then dropped into
# the U-Boot include path before configure/compile.

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI:append:sg2002-licheervnano = " \
    file://cvipart.h \
    file://imgs.h \
    file://cvi_board_memmap.h \
    file://cvi_build.config \
    "

# The cvitek U-Boot Makefile does '-include ${BUILD_PATH}/.config' (the
# sophgo-build top-level config). BUILD_PATH is unset under OE, so it resolves
# to /.config and make tries (and fails) to *build* it. Point BUILD_PATH at a
# directory holding the cvitek build config so the include just succeeds.
CVI_BUILD_DIR = "${WORKDIR}/cvi-build"
# CHIP/CVIBOARD select the board device tree: arch/riscv/dts/Makefile builds
# $(CHIP)_$(CVIBOARD).dtb = sg2002_licheervnano_sd.dtb. The sophgo build exports
# these for u-boot targets; OE must pass them too (else it builds "_.dtb").
EXTRA_OEMAKE:append:sg2002-licheervnano = " BUILD_PATH=${CVI_BUILD_DIR} CHIP=sg2002 CVIBOARD=licheervnano_sd"

do_configure:prepend:sg2002-licheervnano() {
    # Keep the source tree clean for the out-of-tree build (B != S): the cvitek
    # Makefile aborts at prepare3 if S has a stale .config/include/config.
    rm -f "${S}/.config" "${S}/.config.old"
    rm -rf "${S}/include/config" "${S}/include/generated"

    install -d "${S}/include"
    install -m 0644 "${WORKDIR}/cvipart.h"          "${S}/include/cvipart.h"
    install -m 0644 "${WORKDIR}/imgs.h"             "${S}/include/imgs.h"
    install -m 0644 "${WORKDIR}/cvi_board_memmap.h" "${S}/include/cvi_board_memmap.h"

    install -d "${CVI_BUILD_DIR}"
    install -m 0644 "${WORKDIR}/cvi_build.config" "${CVI_BUILD_DIR}/.config"

    # The NanoKVM is headless (no local display panel), so drop the U-Boot boot
    # logo. CONFIG_BOOTLOGO makes env_default.h reference SHOWLOGOCOMMAND, whose
    # cvitek VO/panel command macros assume an active MIPI panel/display stack.
    # Disable it in the defconfig before the config is generated so the whole
    # config/autoconf flow is consistent (no stale auto.conf).
    if [ -f "${S}/configs/sg2002_licheervnano_sd_defconfig" ]; then
        sed -i 's/^CONFIG_BOOTLOGO=y/# CONFIG_BOOTLOGO is not set/' \
            "${S}/configs/sg2002_licheervnano_sd_defconfig"
    fi
}
