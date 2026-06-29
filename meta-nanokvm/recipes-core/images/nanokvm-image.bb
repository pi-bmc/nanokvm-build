SUMMARY = "NanoKVM SD card image — IP KVM system for LicheeRV Nano / SG2002"
LICENSE = "MIT"

inherit core-image

# --- Features ---
IMAGE_FEATURES += " \
    ssh-server-openssh \
    package-management \
    debug-tweaks \
    "

# --- System foundation ---
IMAGE_INSTALL:append = " \
    kmod \
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
    ntp \
    avahi \
    avahi-daemon \
    lldpd \
    macchanger \
    ethtool \
    iproute2 \
    iputils \
    iw \
    wireless-regdb \
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
IMAGE_INSTALL:append = " \
    wpa-supplicant \
    hostapd \
    "

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
IMAGE_INSTALL:append = " \
    bluez5 \
    "

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
    ipmi-sim \
    openipmi \
    "

# --- Rootfs size: 1600 MB matches BR2_TARGET_ROOTFS_EXT2_SIZE="1600M" ---
IMAGE_ROOTFS_SIZE = "1638400"
IMAGE_ROOTFS_EXTRA_SPACE = "65536"

# --- SD card image via WKS ---
WKS_FILE = "nanokvm-sd.wks"
do_image_wic[depends] += "fsbl:do_deploy u-boot-sophgo:do_deploy linux-sophgo:do_deploy"
