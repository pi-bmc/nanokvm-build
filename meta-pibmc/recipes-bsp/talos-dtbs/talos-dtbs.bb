SUMMARY = "Broadcom DTBs from the Talos kernel OCI image"
DESCRIPTION = "Extracts bcm27xx device trees from ghcr.io/siderolabs/kernel \
so the DTBs shipped on the boot partition match the kernel Talos actually runs."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

# Talos kernel image tag; overridden via the kas env passthrough.
TALOS_KERNEL_IMAGE ?= "ghcr.io/siderolabs/kernel"
TALOS_KERNEL_TAG ?= "v1.13.0-36-g6b315f7"

# Overlay merged into the Pi 5 base DTBs at build time (see do_compile). dtc and
# fdtoverlay come from dtc-native.
SRC_URI = "file://fixup-blconfig-overlay.dts"
DEPENDS = "dtc-native"

inherit deploy nopackages

COMPATIBLE_MACHINE = "pi-bmc-rpi64"

do_configure[noexec] = "1"
do_install[noexec] = "1"

# The image is pulled with crane (installed on the host / CI runner) rather
# than the bitbake fetcher, so the task needs network access.
do_compile[network] = "1"
do_compile() {
    if ! command -v crane >/dev/null 2>&1; then
        bbfatal "crane not found on the build host. Install it (e.g. 'go install github.com/google/go-containerregistry/cmd/crane@latest') - it is needed to extract DTBs from ${TALOS_KERNEL_IMAGE}:${TALOS_KERNEL_TAG}."
    fi

    rm -rf ${B}/dtbs
    mkdir -p ${B}/dtbs

    crane export --platform linux/arm64 \
        ${TALOS_KERNEL_IMAGE}:${TALOS_KERNEL_TAG} \
        ${B}/talos-kernel.tar

    tar -xf ${B}/talos-kernel.tar --strip-components=2 -C ${B}/dtbs \
        $(tar -tf ${B}/talos-kernel.tar | grep -E '^dtb/broadcom/bcm27[^/]+\.dtb$')
    rm -f ${B}/talos-kernel.tar

    if [ -z "$(ls ${B}/dtbs/bcm2711*.dtb 2>/dev/null)" ]; then
        bbfatal "No bcm2711* device tree files extracted from ${TALOS_KERNEL_IMAGE}:${TALOS_KERNEL_TAG}"
    fi

    # Bake the bootloader-config / public-key NVRAM nodes (blconfig / blpubkey)
    # into the Pi 5 base DTBs. The VPU firmware's blconfig fixup only populates a
    # node already present in the DTB it loads, so applying this as a runtime
    # .dtbo is too late (reg stays 0). Merging it here makes the node present
    # before that fixup, exactly as on a stock DTB.
    dtc -O dtb -@ -H epapr \
        -o ${B}/fixup-blconfig.dtbo ${WORKDIR}/fixup-blconfig-overlay.dts

    if [ ! -f ${B}/dtbs/bcm2712-rpi-5-b.dtb ]; then
        bbfatal "bcm2712-rpi-5-b.dtb not extracted from ${TALOS_KERNEL_IMAGE}:${TALOS_KERNEL_TAG}; cannot bake blconfig"
    fi

    for base in bcm2712-rpi-5-b bcm2712-d-rpi-5-b bcm2712-rpi-5-b-ovl-rp1; do
        dtb=${B}/dtbs/${base}.dtb
        [ -f "${dtb}" ] || continue
        fdtoverlay -i "${dtb}" -o "${dtb}.new" ${B}/fixup-blconfig.dtbo
        mv "${dtb}.new" "${dtb}"
        bbnote "talos-dtbs: baked blconfig/blpubkey into ${base}.dtb"
    done
}

do_deploy() {
    install -d ${DEPLOYDIR}/talos-dtbs
    install -m 0644 ${B}/dtbs/*.dtb ${DEPLOYDIR}/talos-dtbs/
}
addtask deploy after do_compile before do_build

