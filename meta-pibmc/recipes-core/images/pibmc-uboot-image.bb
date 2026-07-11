SUMMARY = "Boot-partition-only SD card image with U-Boot for Raspberry Pi 4/5"
DESCRIPTION = "GPT image with a single FAT32 EFI System Partition containing \
the Raspberry Pi firmware, U-Boot (as kernel8.img), TF-A armstubs, Talos \
kernel DTBs, and device tree overlays. There is no rootfs - the OS (Talos) \
is booted over the network or from another disk."
LICENSE = "MIT"

inherit image

# Nothing is installed into the (empty) rootfs; the image content is the
# boot partition assembled by wic from IMAGE_BOOT_FILES.
IMAGE_FEATURES = ""
IMAGE_INSTALL = ""
PACKAGE_INSTALL = ""
IMAGE_LINGUAS = ""
NO_RECOMMENDATIONS = "1"

IMAGE_FSTYPES = "wic.xz"

do_image_wic[depends] += " \
    u-boot-pibmc:do_deploy \
    armstub-tfa:do_deploy \
    rpi-firmware:do_deploy \
    pibmc-config:do_deploy \
    pibmc-overlays:do_deploy \
    talos-dtbs:do_deploy \
"
