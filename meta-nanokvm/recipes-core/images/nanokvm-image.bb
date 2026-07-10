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
# No "package-management": there is no feed to install from, and it costs the
# opkg binary plus its package database on every boot partition.
#
# "debug-tweaks" is retained deliberately. It is what leaves root with an empty
# password and permits root ssh; dropping it on a headless board with no serial
# console attached and no other account provisioned would lock you out. Replace
# it with a real credential (EXTRA_USERS_PARAMS) before shipping hardware.
IMAGE_FEATURES += " \
    ssh-server-openssh \
    debug-tweaks \
    "

# Pulled in by RRECOMMENDS, not by anything here, and both are large:
#   kernel-image-image  ~29 MiB -- a second copy of the kernel in the rootfs's
#                       /boot, which is then shadowed by the vfat mount over it.
#                       The kernel U-Boot actually loads lives on the FAT
#                       partition (IMAGE_BOOT_FILES), not here.
#   eudev-hwdb          ~9.3 MiB of hwdb.bin plus 5.6 MiB of hwdb.d sources --
#                       a PCI/USB vendor-model database for a board with no PCI
#                       bus and no USB host controller.
BAD_RECOMMENDATIONS += "kernel-image-image eudev-hwdb"

# --- System foundation ---
# Dropped: kmod (CONFIG_MODULES=n -- nothing to load), util-linux-rfkill (no
# radio), file (8.2 MiB magic.mgc), watchdog (no wdt node in the DTS, so
# /dev/watchdog never appears and the daemon failed on every boot).
IMAGE_INSTALL:append = " \
    zram-swap \
    busybox \
    bash \
    util-linux \
    e2fsprogs \
    e2fsprogs-resize2fs \
    exfatprogs \
    parted \
    eudev \
    udev-extraconf \
    "

# --- Network core ---
# Dropped: bind (a full DNS server), dnsmasq, lldpd, macchanger, android-tools,
# usbutils (no USB host), iperf3, mtr, tcpdump, traceroute, sysstat, socat,
# netcat-openbsd, rsync. None are referenced by nanokvm-server, which shells out
# only to mount, passwd, reboot, sh, sync and umount.
IMAGE_INSTALL:append = " \
    nanokvm-network \
    busybox-udhcpc \
    iputils \
    iputils-arping \
    ntp \
    ethtool \
    iproute2 \
    avahi-daemon \
    ssdp-responder \
    "

# --- WiFi / Bluetooth ---
# Intentionally none. The SDIO1 pads are repurposed as I2C1 for the I2C slave
# EEPROM, and CONFIG_WLAN / CONFIG_WIRELESS / CONFIG_BT are off in nanokvm.cfg.

# --- VPN / firewall ---
# nftables only; the legacy iptables front-end and ppp are dropped. The kernel
# carries NF_TABLES/NF_NAT/NFT_NAT and WIREGUARD.
IMAGE_INSTALL:append = " \
    nftables \
    wireguard-tools \
    "

# --- SSH / security / crypto ---
# haveged stays: the SG2002 has no hardware RNG, so the entropy pool fills very
# slowly and sshd's first key exchange after boot otherwise stalls. krb5 is
# dropped -- nothing authenticates against a KDC.
IMAGE_INSTALL:append = " \
    openssl \
    ca-certificates \
    haveged \
    "

# --- Compression ---
# The app serves firmware images and ISOs; it does not shell out to any archiver.
# busybox already provides gzip/tar. Everything else (bzip2, lzip, p7zip, pigz,
# unzip, zip, zstd, lzop, brotli, libzip, lz4) is dropped.

# --- Libraries ---
# Dropped: libwebsockets, nanomsg, qrencode (the Go server implements its own
# websocket and QR handling), plus tslib, libinput, libxkbcommon, fontconfig,
# freetype and hicolor-icon-theme -- an input/display/font stack on a board with
# no display, kept alive only by their own inter-dependencies.
IMAGE_INSTALL:append = " \
    libcurl \
    curl \
    "

# --- Input ---
# input-event-daemon drives the front-panel power/reset buttons declared in the
# board DTS (&porta gpio-line-names). evtest and fbset are dropped.
IMAGE_INSTALL:append = " \
    input-event-daemon \
    "

# --- Python 3 ---
# Intentionally none. 68 python3-* packages, ~15 MiB with libpython3.12 and the
# cryptography extension, and nothing in the image invokes an interpreter: the
# server is a single static Go binary and every init script is POSIX sh.

# --- Developer / debug tools ---
# Dropped: vim, tmux, htop, strace, memtester, picocom, setserial, ser2net,
# u-boot-tools, spidev-test (no SPI node is enabled in the board DTS, so
# /dev/spidev* never appears). busybox supplies vi, top and ps.

# --- Sophgo SDK ---
# The closed-source multimedia/AI stack (sophgo-middleware, cvi-rtsp, osdrv,
# maix-cdk, sg2002-codec-firmware) is intentionally removed: it ships proprietary
# ISP tuning blobs and CLOSED-licensed cvitek kernel modules (VI/VPSS/VENC/ISP/
# TPU), and the pure-Go app does not link or exec it. This drops hardware HDMI
# capture / RTSP streaming; only the open-source BMC path remains. What stays are
# the non-video vendor bits: axp2101 (PMU power management) and cvi-pinmux.
# uvc-gadget went with it -- nothing creates a uvc function (see nanokvm.cfg).
IMAGE_INSTALL:append = " \
    cvi-pinmux \
    axp2101 \
    "

# --- NanoKVM application ---
# i2c-eeprom is gone: the board DTS declares both the i2c1 pinmux and the
# eeprom@50 slave node, so the kernel binds i2c-slave-eeprom at boot and the
# init script's only remaining effect was to re-apply a pinmux pinctrl had
# already set.
IMAGE_INSTALL:append = " \
    nanokvm-server \
    nanokvm-gadget \
    "

# --- Rootfs size: 1600 MB matches BR2_TARGET_ROOTFS_EXT2_SIZE="1600M" ---
IMAGE_ROOTFS_SIZE = "1638400"
IMAGE_ROOTFS_EXTRA_SPACE = "65536"

# --- SD card image via WKS ---
WKS_FILE = "nanokvm-sd.wks"
do_image_wic[depends] += "fsbl:do_deploy linux-sophgo:do_deploy"

# --- Boot-partition config files ---
# nanokvm-gadget deploys these to DEPLOY_DIR_IMAGE; wic's bootimg-partition puts
# them on the FAT boot partition next to fip.bin, the kernel Image and the DTB.
# Read at runtime from /boot by the USB gadget udev rule and the app.
# extlinux.conf is the mainline U-Boot boot menu (bootmeth_extlinux scans
# /extlinux/extlinux.conf).
IMAGE_BOOT_FILES:append = " board hostname.prefix ver usb.keyboard usb.mouse usb.rndis0 extlinux.conf;extlinux/extlinux.conf"
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
