SUMMARY = "Pre-seeded U-Boot environment image for the raw MMC env sectors"
DESCRIPTION = "Builds the environment blob the wic writes at offset 512 KiB, \
so a freshly flashed card comes up with a complete, known environment instead \
of falling back to the compiled-in defaults with a bad-CRC warning. Both \
redundant copies are written, so re-flashing a used card cannot leave a stale \
copy behind that outranks the new one."

LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

FILESEXTRAPATHS:prepend := "${THISDIR}/files:"
SRC_URI = "file://uboot-default-env.txt"

DEPENDS = "u-boot-mkenvimage-native"
# The blob is generated FROM U-Boot's own default environment, so it has to be
# rebuilt whenever that changes -- and the file only exists after do_deploy.
do_compile[depends] += "u-boot:do_deploy"

COMPATIBLE_MACHINE = "sg2002-licheervnano"
PACKAGE_ARCH = "${MACHINE_ARCH}"
INHIBIT_DEFAULT_DEPS = "1"
PACKAGES = ""
do_install[noexec] = "1"
do_populate_sysroot[noexec] = "1"

inherit deploy nopackages

# Must match CONFIG_ENV_SIZE in the u-boot recipe.
UBOOT_ENV_SIZE ?= "0x10000"

do_compile() {
    initial="${DEPLOY_DIR_IMAGE}/u-boot-initial-env"
    [ -f "$initial" ] || bbfatal "nanokvm-uboot-env: $initial missing;" \
        "the u-boot recipe should have deployed it"

    # A valid stored environment REPLACES the compiled defaults rather than
    # merging with them, so the seed has to carry all of them. Start from
    # U-Boot's own default environment and layer the overrides on top, dropping
    # any base line the override file redefines so no key appears twice.
    grep -v '^[[:space:]]*\(#\|$\)' ${WORKDIR}/uboot-default-env.txt > ${B}/overrides.txt
    cut -d= -f1 ${B}/overrides.txt | sed -e 's/^/^/' -e 's/$/=/' > ${B}/override-keys
    if [ -s ${B}/override-keys ]; then
        grep -v -f ${B}/override-keys "$initial" > ${B}/base.txt
    else
        cp "$initial" ${B}/base.txt
    fi
    cat ${B}/base.txt ${B}/overrides.txt > ${B}/env.txt

    # The boot flow cannot start without these, and getting the seed wrong is
    # silent: the board simply stops booting with no message that points here.
    for v in kernel_addr_r fdt_addr_r ramdisk_addr_r bootm_size bootcmd bootmeths; do
        grep -q "^$v=" ${B}/env.txt || bbfatal "nanokvm-uboot-env: $v missing" \
            "from the generated environment -- the boot flow needs it"
    done

    # -r selects the redundant-env format (a flag byte after the CRC).
    mkenvimage -r -s ${UBOOT_ENV_SIZE} -o ${B}/uboot-env-copy.bin ${B}/env.txt

    # Write BOTH copies. The alternative -- seeding only the primary -- looks
    # safe because U-Boot accepts one valid copy, but the wic leaves the second
    # slot as a hole, and `bmaptool copy` (which the machine conf recommends and
    # hack/flash-sd.sh prefers) skips holes. Re-flashing a card that has ever
    # run saveenv would then leave the OLD redundant copy in place, and both
    # U-Boot's env_check_redund() and libubootenv's libuboot_load() pick the
    # copy with the higher flag byte -- the stale one. Two identical copies are
    # not a problem: with equal flags both implementations choose the primary.
    cat ${B}/uboot-env-copy.bin ${B}/uboot-env-copy.bin > ${B}/uboot-env.bin
}

do_deploy() {
    install -d ${DEPLOYDIR}
    install -m 0644 ${B}/uboot-env.bin ${DEPLOYDIR}/uboot-env.bin
}
addtask deploy after do_compile before do_build
