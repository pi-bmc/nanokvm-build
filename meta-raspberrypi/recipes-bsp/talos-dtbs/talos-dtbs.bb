SUMMARY = "Broadcom DTBs from the Talos kernel OCI image"
DESCRIPTION = "Extracts bcm27xx device trees from ghcr.io/siderolabs/kernel \
so the DTBs shipped on the boot partition match the kernel Talos actually runs."
LICENSE = "GPL-2.0-only"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/GPL-2.0-only;md5=801f80980d171dd6425610833a22dbe6"

# Talos kernel image tag; overridden via the kas env passthrough.
TALOS_KERNEL_IMAGE ?= "ghcr.io/siderolabs/kernel"
TALOS_KERNEL_TAG ?= "v1.13.0-36-g6b315f7"

# Overlays merged into the Pi 5 base DTBs at build time (see do_compile). dtc and
# fdtoverlay come from dtc-native.
SRC_URI = " \
    file://fixup-blconfig-overlay.dts \
    file://uefi-eeprom-overlay.dts \
    file://bcm2712-thermal-overlay.dts \
"
DEPENDS = "dtc-native"

inherit deploy nopackages

COMPATIBLE_MACHINE = "rpi64"

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

    # Bake the RPi 5 device-tree overlays into the base DTBs at build time. They
    # add nodes the stripped mainline Talos DTB omits:
    #   fixup-blconfig  - bootloader-config / public-key NVRAM (blconfig/blpubkey).
    #                     Must be present before the VPU firmware's fixup runs, so
    #                     a runtime .dtbo is too late (reg stays 0).
    #   uefi-eeprom     - RP1 i2c1 + UEFI-var-store EEPROM consumed by U-Boot.
    #   bcm2712-thermal - AVS monitor + cpu-thermal zone for the thermal driver.
    # Merging here (vs shipping runtime .dtbo's) makes them present before the
    # firmware processes the DTB and avoids fragile firmware-time application.
    # See each .dts header for details.
    OVERLAYS="fixup-blconfig-overlay uefi-eeprom-overlay bcm2712-thermal-overlay"
    dtbos=""
    for ovl in ${OVERLAYS}; do
        dtc -O dtb -@ -H epapr -o ${B}/${ovl}.dtbo ${WORKDIR}/${ovl}.dts
        dtbos="${dtbos} ${B}/${ovl}.dtbo"
    done

    if [ ! -f ${B}/dtbs/bcm2712-rpi-5-b.dtb ]; then
        bbfatal "bcm2712-rpi-5-b.dtb not extracted from ${TALOS_KERNEL_IMAGE}:${TALOS_KERNEL_TAG}; cannot bake overlays"
    fi

    # bcm2712-rpi-5-b-ovl-rp1.dtb is deliberately excluded: it is the mainline
    # split-RP1 variant whose /axi/pcie@1000120000 is empty (RP1 supplied by a
    # separate overlay), so the uefi-eeprom target path is absent. The firmware
    # never loads that variant - it loads bcm2712-rpi-5-b.dtb, plus bcm2712d0 /
    # bcm2712-d-rpi-5-b.dtb on D0 silicon - both of which carry the full RP1 tree.
    for base in bcm2712-rpi-5-b bcm2712-d-rpi-5-b; do
        dtb=${B}/dtbs/${base}.dtb
        [ -f "${dtb}" ] || continue
        fdtoverlay -i "${dtb}" -o "${dtb}.new" ${dtbos}
        mv "${dtb}.new" "${dtb}"
        bbnote "talos-dtbs: baked overlays (${OVERLAYS}) into ${base}.dtb"
    done
}

do_deploy() {
    install -d ${DEPLOYDIR}/talos-dtbs
    install -m 0644 ${B}/dtbs/*.dtb ${DEPLOYDIR}/talos-dtbs/
}
addtask deploy after do_compile before do_build

