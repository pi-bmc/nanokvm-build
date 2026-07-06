# Enable the devmem applet (poky's busybox defconfig disables it); required by
# recipes-core/i2c-eeprom to program the SD1 pad pinmux for I2C1.
FILESEXTRAPATHS:prepend := "${THISDIR}/files:"

SRC_URI += "file://devmem.cfg"
