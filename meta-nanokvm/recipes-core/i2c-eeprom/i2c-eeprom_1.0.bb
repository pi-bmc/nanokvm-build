SUMMARY = "NanoKVM I2C slave EEPROM (SD1 pads repurposed as I2C1)"
DESCRIPTION = "sysvinit service that muxes two SDIO1 pads to I2C1 \
(SD1_D3->IIC1_SCL, SD1_D0->IIC1_SDA) and registers the kernel i2c-slave-eeprom \
backend on that bus, so the NanoKVM presents a standard I2C EEPROM to an \
external host (Raspberry Pi). Replaces the removed eMMC device emulator. The \
kernel support (I2C_SLAVE_EEPROM, I2C_DESIGNWARE_SLAVE) is already enabled in \
the board defconfig."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://i2c-eeprom"
S = "${WORKDIR}"

inherit update-rc.d

INITSCRIPT_NAME = "i2c-eeprom"
# Bring the EEPROM up early (after device nodes exist) so the app and the host
# can use it; tear down late.
INITSCRIPT_PARAMS = "start 15 2 3 4 5 . stop 85 0 1 6 ."

# devmem (pad pinmux) is a busybox applet; the i2c-slave-eeprom backend is
# built into the kernel.
RDEPENDS:${PN} = "busybox"

do_install() {
    install -d ${D}${sysconfdir}/init.d
    install -m 0755 ${WORKDIR}/i2c-eeprom ${D}${sysconfdir}/init.d/i2c-eeprom
}

FILES:${PN} = "${sysconfdir}/init.d/i2c-eeprom"
