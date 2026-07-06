SUMMARY = "NanoKVM SD card image — IP KVM system for LicheeRV Nano / SG2002"
LICENSE = "MIT"

inherit core-image

# Use the plain packagegroup-base (not -extended): -extended unconditionally
# RDEPENDS packagegroup-base-wifi -> wireless-regdb-static and would drag in
# wireless kernel modules. WiFi is intentionally absent from this image (the
# SDIO1 pads are repurposed as I2C1 for the I2C slave EEPROM), so packagegroup-base
# pulls the wifi subgroup only when MACHINE_FEATURES advertises it -- it does not.
CORE_IMAGE_BASE_INSTALL = "packagegroup-core-boot packagegroup-base"

# --- Features ---
IMAGE_FEATURES += " \
    ssh-server-openssh \
    package-management \
    debug-tweaks \
    "

# --- System foundation ---
IMAGE_INSTALL:append = " \
    kmod \
    zram-swap \
    busybox \
    bash \
    file \
    util-linux \
    util-linux-rfkill \
    e2fsprogs \
    e2fsprogs-resize2fs \
    exfatprogs \
    parted \
    watchdog \
    eudev \
    udev-extraconf \
    "

# --- USB tools ---
IMAGE_INSTALL:append = " \
    usbutils \
    uvc-gadget \
    android-tools \
    "

# --- Network core ---
IMAGE_INSTALL:append = " \
    nanokvm-network \
    busybox-udhcpc \
    iputils-arping \
    ntp \
    avahi-daemon \
    lldpd \
    macchanger \
    ethtool \
    iproute2 \
    iputils \
    ipmitool \
    iperf3 \
    mtr \
    socat \
    netcat-openbsd \
    rsync \
    dnsmasq \
    tcpdump \
    traceroute \
    sysstat \
    bind \
    iputils \
    ssdp-responder \
    "

# --- WiFi ---
# Intentionally none. The SDIO1 pads are repurposed as I2C1 for the I2C slave
# EEPROM (recipes-core/i2c-eeprom), so no wifi driver, stack, or userspace is
# installed (no wpa-supplicant/hostapd/iw/wireless-regdb; CFG80211/AIC_WLAN off
# in the kernel defconfig; the wifisd DT node is disabled by the linux bbappend).

# --- VPN / firewall ---
IMAGE_INSTALL:append = " \
    iptables \
    nftables \
    wireguard-tools \
    ppp \
    "

# --- SSH / security / crypto ---
IMAGE_INSTALL:append = " \
    openssl \
    ca-certificates \
    haveged \
    krb5 \
    "

# --- Bluetooth ---
# Intentionally none. Bluetooth is removed (CONFIG_BT off in the kernel
# defconfig, no bluez5, no `bluetooth` DISTRO_FEATURE).

# --- Compression ---
IMAGE_INSTALL:append = " \
    bzip2 \
    lzip \
    p7zip \
    pigz \
    unzip \
    zip \
    zstd \
    lzop \
    brotli \
    libzip \
    lz4 \
    "

# --- Libraries ---
IMAGE_INSTALL:append = " \
    libcurl \
    curl \
    libwebsockets \
    nanomsg \
    libpcre2 \
    qrencode \
    tslib \
    libinput \
    libxkbcommon \
    fontconfig \
    freetype \
    "

# --- Input / display ---
IMAGE_INSTALL:append = " \
    evtest \
    input-event-daemon \
    fbset \
    hicolor-icon-theme \
    "

# --- Python 3 ---
IMAGE_INSTALL:append = " \
    python3 \
    python3-core \
    python3-modules \
    python3-requests \
    "

# --- Developer / debug tools ---
IMAGE_INSTALL:append = " \
    vim \
    tmux \
    htop \
    strace \
    memtester \
    picocom \
    setserial \
    ser2net \
    u-boot-tools \
    spidev-test \
    "

# --- Sophgo SDK ---
IMAGE_INSTALL:append = " \
    sophgo-middleware \
    cvi-rtsp \
    osdrv \
    cvi-pinmux \
    maix-cdk \
    sg2002-codec-firmware \
    axp2101 \
    "

# --- NanoKVM application ---
IMAGE_INSTALL:append = " \
    nanokvm-server \
    nanokvm-gadget \
    i2c-eeprom \
    ipmi-sim \
    openipmi \
    "

# --- Rootfs size: 1600 MB matches BR2_TARGET_ROOTFS_EXT2_SIZE="1600M" ---
IMAGE_ROOTFS_SIZE = "1638400"
IMAGE_ROOTFS_EXTRA_SPACE = "65536"

# --- SD card image via WKS ---
WKS_FILE = "nanokvm-sd.wks"
do_image_wic[depends] += "fsbl:do_deploy u-boot-sophgo:do_deploy linux-sophgo:do_deploy"

# --- Boot-partition config files ---
# nanokvm-gadget deploys these to DEPLOY_DIR_IMAGE; wic's bootimg-partition puts
# them on the FAT boot partition next to fip.bin/boot.sd. Read at runtime from
# /boot by the USB gadget init (S03usbdev) and the app.
IMAGE_BOOT_FILES:append = " board hostname.prefix ver usb.disk0 usb.keyboard usb.mouse usb.rndis0"
do_image_wic[depends] += "nanokvm-gadget:do_deploy"

# --- Publish under the original LicheeRV-Nano-Build image name ---
# The upstream build emitted ${BOARD_SHORT}-${VARIANT}_${STORAGE_TYPE}.img.xz =
# "licheervnano-kvm_sd.img.xz". The wic output is the byte-identical raw SD image
# (only the Yocto .wic extension differs), so also expose it under that name:
# a real standalone copy of the compressed image, plus a symlink for the raw one.
NANOKVM_IMG_ALIAS = "licheervnano-kvm_sd"
# do_image_complete is a python task, so register a shell postfunc rather than
# appending shell to it. Runs before the task's sstate capture, so the aliases
# are deployed to DEPLOY_DIR_IMAGE alongside the .wic artifacts.
create_nanokvm_img_alias() {
    if [ -e "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic.xz" ]; then
        cp -fL "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic.xz" \
               "${IMGDEPLOYDIR}/${NANOKVM_IMG_ALIAS}.img.xz"
    fi
    if [ -e "${IMGDEPLOYDIR}/${IMAGE_LINK_NAME}.wic" ]; then
        ln -sf "${IMAGE_LINK_NAME}.wic" "${IMGDEPLOYDIR}/${NANOKVM_IMG_ALIAS}.img"
    fi
}
do_image_complete[postfuncs] += "create_nanokvm_img_alias"
