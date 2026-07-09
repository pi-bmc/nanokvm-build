SUMMARY = "First-Stage Bootloader (FSBL) + FIP packaging for Sophgo SG2002"
LICENSE = "CLOSED"

# The FIP bundles the FSBL (BL2), OpenSBI (MONITOR/fw_dynamic) and U-Boot
# (LOADER_2ND / BL33), so both must be built and staged first.
DEPENDS = "opensbi u-boot python3-native"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI = "git://github.com/scpcom/sophgo-fsbl;branch=licheervnano;protocol=https \
           file://cvi_board_memmap.h"
SRCREV = "${AUTOREV}"
PV = "0.1+git${SRCPV}"

S = "${WORKDIR}/git"

COMPATIBLE_MACHINE = "sg2002-licheervnano"

inherit deploy

# cvitek FSBL build parameters for the SG2002 (cv181x ASIC family).
FSBL_CHIP_ARCH ?= "cv181x"
FSBL_BOARD ?= "licheervnano"
FSBL_BOOT_CPU ?= "riscv"
# DDR3 1866 MHz x16 (256 MB) — matches CONFIG_DDR_CFG_ddr3_1866_x16 for this
# board. Without it the DDR sources (incl. ddr_pkg_info.c -> get_pkg) are not
# compiled and BL2 fails to link.
FSBL_DDR_CFG ?= "ddr3_1866_x16"

# Built with the standard OE cross GCC (mainline >= 13 has the T-Head scalar
# vendor extensions), not the vendored T-Head 10.2 fork. What the FSBL actually
# needs from the toolchain, and how mainline provides it:
#   * `th.icache.iall` / `th.sync.i` mnemonics -> -march ..._xtheadcmo_xtheadsync
#     (the sources use the old un-prefixed vendor spelling; sed'ed below);
#   * the dcache range ops / sync.s are already raw `.long` encodings in
#     cache.c — no toolchain support needed;
#   * C906 M-mode CSRs by name (mxstatus/mhcr/mcor/mhint) — mainline gas has no
#     vendor CSR names, so define them as assembler symbols (--defsym reaches
#     the inline asm in .c files too). Values are from the C906 user manual and
#     verified byte-identical against the vendor assembler's encodings.
# The vendor -march spelling rv64imafdcvxthead ('v' = pre-ratified RVV 0.7) is
# replaced with rv64gc_xtheadcmo_xtheadsync: the FSBL uses no vector
# instructions, and mainline 'v' would mean RVV 1.0, which the C906 lacks.
THEAD_MARCH = "rv64gc_xtheadcmo_xtheadsync"
THEAD_CSR_DEFSYMS = "-Wa,--defsym,mxstatus=0x7c0 -Wa,--defsym,mhcr=0x7c1 -Wa,--defsym,mcor=0x7c2 -Wa,--defsym,mhint=0x7c5"

EXTRA_OEMAKE = "CROSS_COMPILE=${TARGET_PREFIX} \
                CHIP_ARCH=${FSBL_CHIP_ARCH} \
                BOARD=${FSBL_BOARD} \
                BOOT_CPU=${FSBL_BOOT_CPU} \
                DDR_CFG=${FSBL_DDR_CFG}"

# The FSBL (ATF-style) invokes `ld` directly and inherits LDFLAGS from the
# environment; OE's gcc-driver LDFLAGS (-Wl,-O1, ...) are rejected by ld. The
# FSBL provides its own link flags, so clear the OE ones. Likewise the OE
# CFLAGS/CPPFLAGS/ASFLAGS carry the userspace tune, not the bare-metal one.
LDFLAGS = ""
CFLAGS = ""
CPPFLAGS = ""
ASFLAGS = ""
TARGET_CC_ARCH = ""

do_configure() {
    # Mainline spellings for the vendor constructs (see comment above):
    # -march (cpu.mk hardcodes the T-Head 10.2 spelling in ASFLAGS+TF_CFLAGS)
    sed -i 's/rv64imafdcvxthead/${THEAD_MARCH}/g' ${S}/lib/cpu/riscv/cpu.mk
    # cache-maintenance mnemonics grew a 'th.' prefix in the ratified spec /
    # mainline binutils.
    sed -i -e 's/"icache\.iall\\n"/"th.icache.iall\\n"/' \
           -e 's/"sync\.i\\n"/"th.sync.i\\n"/' \
        ${S}/lib/cpu/riscv/cpu_helper.c
}

# Need the deployed OpenSBI + U-Boot binaries before packing the FIP.
do_compile[depends] += "opensbi:do_deploy u-boot:do_deploy"

do_compile() {
    # cpu.mk appends (+=) to ASFLAGS / TF_CFLAGS, so seed them from the
    # environment with the CSR defsyms. The C flags additionally pin down
    # bare-metal codegen the vendor GCC defaulted to but OE's userspace GCC
    # does not (default-PIE, stack protector).
    export ASFLAGS="${THEAD_CSR_DEFSYMS}"
    export TF_CFLAGS="${THEAD_CSR_DEFSYMS} -fno-pie -fno-stack-protector"
    # A bare-metal BL2 legitimately has an RWX LOAD segment; binutils >= 2.39
    # warns about it, and the FSBL links with --fatal-warnings.
    export TF_LDFLAGS="--no-warn-rwx-segments"

    # The cvitek FSBL plat headers (plat/cv181x/include/mmap.h) include the
    # generated board memory map; place it where they can find it.
    install -m 0644 "${WORKDIR}/cvi_board_memmap.h" \
        "${S}/plat/${FSBL_CHIP_ARCH}/include/cvi_board_memmap.h"

    # fip.mk looks for OpenSBI's fw_dynamic.bin at ../opensbi/... relative to
    # the FSBL source dir (S = ${WORKDIR}/git, so ../opensbi = ${WORKDIR}/opensbi).
    install -d "${WORKDIR}/opensbi/build/platform/generic/firmware"
    install -m 0644 "${DEPLOY_DIR_IMAGE}/fw_dynamic.bin" \
        "${WORKDIR}/opensbi/build/platform/generic/firmware/fw_dynamic.bin"

    # Mainline u-boot.bin carries no cvitek loader_2nd ("BL33") head, which
    # fiptool requires (magic check) and BL2 relies on for placement: BL2 loads
    # header+image at RUNADDR and enters at RUNADDR + 0x20 (the header size),
    # so RUNADDR must be TEXT_BASE - 0x20 for U-Boot to land on its link
    # address. fiptool recomputes the CKSUM/SIZE fields, so zeros suffice.
    python3 - "${DEPLOY_DIR_IMAGE}/u-boot.bin" "${WORKDIR}/u-boot-bl33.bin" <<'EOF'
import struct, sys
TEXT_BASE = 0x80200000  # CONFIG_TEXT_BASE, sipeed_licheerv_nano_defconfig
hdr = struct.pack(
    "<I4sIIQII",
    0x0001a005,          # JUMP0: c.j/c.nop; never executed (entry skips header)
    b"BL33",             # MAGIC
    0, 0,                # CKSUM, SIZE: filled in by fiptool
    TEXT_BASE - 0x20,    # RUNADDR
    0, 0)                # reserved
with open(sys.argv[1], "rb") as f:
    body = f.read()
with open(sys.argv[2], "wb") as f:
    f.write(hdr + body)
EOF

    # Build the FSBL (BL2) and pack the FIP with U-Boot as the 2nd-stage loader.
    oe_runmake -C ${S} all LOADER_2ND_PATH="${WORKDIR}/u-boot-bl33.bin"
}

do_install() {
    install -d ${D}${datadir}/fsbl
    fip="$(find ${S}/build -name fip.bin 2>/dev/null | head -1)"
    if [ -z "${fip}" ]; then
        bbfatal "fsbl: fip.bin was not produced by the FSBL build"
    fi
    install -m 0644 "${fip}" ${D}${datadir}/fsbl/fip.bin
}

do_deploy() {
    install -d ${DEPLOYDIR}
    fip="$(find ${S}/build -name fip.bin 2>/dev/null | head -1)"
    install -m 0644 "${fip}" ${DEPLOYDIR}/fip.bin
}
addtask deploy after do_install before do_build

FILES:${PN} = "${datadir}/fsbl"
