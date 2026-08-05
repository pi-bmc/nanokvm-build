# Mount /boot, configfs and debugfs from fstab instead of from S01fs.
#
# The NanoKVM server (server/service/usbgadget) now owns the USB gadget: it
# assembles it under configfs at startup and reads its config from /boot, so
# both mounts must be present before the server runs (inittab ::respawn
# entries start only after the ::sysinit `mount -a` completes):
#
#   /boot               the server reads /boot/ver and, on first boot only,
#                       the legacy migration flags /boot/usb.ecm0, usb.vid, ...
#   /sys/kernel/config  the gadget is assembled under configfs/usb_gadget
#                       (the server mounts configfs itself as a fallback, but
#                       having it in fstab brings it up once, early)
#
# Doing them from fstab (the inittab's `mount -a` sysinit line, which runs
# before rcS) instead of S01fs's old rc5.d/S06 entry keeps them independent of
# the first-boot partition dance and available to everything from early boot
# on. (Previously the gadget was built by a udev rule at coldplug, which is
# why the ordering used to be described against S04udev; that rule is gone now
# that the server builds the gadget.)
#
# base-files owns /etc/fstab, so append here rather than shipping a second
# /etc/fstab from nanokvm-gadget (which would be an ipk file conflict).
#
# /var/lib/nanokvm (the ext4 data partition, p4) is intentionally absent: the
# initramfs creates it on first boot and mounts it before switch_root, so by
# the time mountall runs it is already up. Only the bind of the app's config
# dir lives here — with the volatile tmpfs overlay over the squashfs root,
# anything written to a plain /etc/kvm would vanish on reboot; the bind makes
# it land on the data partition instead. /etc/kvm itself must exist in the
# image (bind targets are not auto-created by mount).
do_install:append:sg2002-licheervnano() {
    install -d ${D}${sysconfdir}/kvm
    cat >> ${D}${sysconfdir}/fstab <<'EOF'

# --- NanoKVM ---
/dev/mmcblk0p1          /boot                vfat   defaults,noatime  0  2
configfs                /sys/kernel/config   configfs  defaults       0  0
debugfs                 /sys/kernel/debug    debugfs   defaults       0  0
/var/lib/nanokvm/config /etc/kvm             none   bind              0  0
EOF
}
