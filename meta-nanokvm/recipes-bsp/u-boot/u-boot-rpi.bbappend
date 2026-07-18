# u-boot-rpi PROVIDES "u-boot" and "virtual/bootloader" with no
# COMPATIBLE_MACHINE, so in a multiconfig build it becomes a second candidate
# provider of "u-boot" in the NanoKVM config -- where meta-sophgo's u-boot
# (PN = u-boot) already provides it -- and bitbake errors on the ambiguity.
#
# Confine it to the Pi machine. In the NanoKVM multiconfig the recipe is then
# skipped entirely (incompatible machine) and cannot provide anything; in the
# rpi multiconfig it builds as before.
COMPATIBLE_MACHINE = "rpi64"
