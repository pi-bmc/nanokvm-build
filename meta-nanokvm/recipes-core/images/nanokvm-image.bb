SUMMARY = "NanoKVM SD card image — minimal IP KVM system for LicheeRV Nano"
LICENSE = "MIT"

inherit core-image

IMAGE_FEATURES += " \
    ssh-server-openssh \
    package-management \
    "

# Base system
IMAGE_INSTALL:append = " \
    kernel-modules \
    bash \
    busybox \
    util-linux \
    e2fsprogs-resize2fs \
    "

# Networking
IMAGE_INSTALL:append = " \
    networkmanager \
    ntp \
    avahi-daemon \
    openssl \
    wireguard-tools \
    iptables \
    nftables \
    "

# USB gadget support (keyboard, mouse, storage, RNDIS)
IMAGE_INSTALL:append = " \
    usbutils \
    "

# Sophgo SDK
IMAGE_INSTALL:append = " \
    sophgo-middleware \
    cvi-rtsp \
    osdrv \
    "

# NanoKVM application
IMAGE_INSTALL:append = " \
    nanokvm \
    ipmi-sim \
    openipmi \
    "

# Filesystem size — rootfs.ext4 target 1600MB
IMAGE_ROOTFS_SIZE = "1638400"
IMAGE_ROOTFS_EXTRA_SPACE = "65536"

# SD card image via WKS
WKS_FILE = "nanokvm-sd.wks"
do_image_wic[depends] += "fsbl:do_deploy u-boot-sophgo:do_deploy linux-sophgo:do_deploy"
