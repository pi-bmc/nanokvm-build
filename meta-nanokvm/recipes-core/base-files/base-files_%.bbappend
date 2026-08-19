# Mount /boot, configfs and debugfs from fstab instead of from S01fs.
#
# The NanoKVM server (server/service/usbgadget) now owns the USB gadget: it
# assembles it under configfs at startup and reads its config from /boot, so
# both mounts must be present before the server runs (inittab ::respawn
# entries start only after the ::sysinit `mount -a` completes):
#
#   /boot               READ-ONLY. The server only ever reads from it
#                       (/boot/ver and, on first boot only, the legacy
#                       migration flags /boot/usb.ecm0, usb.vid, ...), and the
#                       boot payload itself must survive a crash: FAT16 has no
#                       journal, so a dirty unmount of a writable /boot is a
#                       direct route to an unbootable card. The A/B slot that
#                       used to be a file here now lives in the U-Boot
#                       environment (raw, redundant, atomic). Anything that
#                       genuinely needs to write the boot partition -- an image
#                       update writing a new FIT -- must remount it rw
#                       explicitly, do its work, and remount ro.
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
# /var/lib/nanokvm (the ext4 userdata partition, p4) is intentionally absent
# from fstab: the initramfs mounts it before switch_root, so by the time
# mountall runs it is already up.
#
# There are exactly two writable zones on this system:
#
#   1. the volatile tmpfs overlay over the squashfs root -- writable so the
#      running system can override files, but discarded on every boot, so the
#      base can never be compromised by accumulated drift; and
#   2. /var/lib/nanokvm, the userdata partition -- the only thing that
#      survives a reboot.
#
# Everything in the rootfs that needs persistence therefore reaches it by
# SYMLINK into /var/lib/nanokvm, not by bind mount. /etc/kvm used to be a bind
# mount here; a symlink does the same job without depending on mount ordering,
# without a second mount to fail, and it is visible for what it is when you
# ls the directory. Dangling until the partition mounts is fine -- nothing
# reads these paths before the initramfs has mounted it.
do_install:append:sg2002-licheervnano() {
    # Persistence symlinks: rootfs path -> userdata partition.
    install -d ${D}${localstatedir}/lib
    ln -sfn ${localstatedir}/lib/nanokvm/config ${D}${sysconfdir}/kvm

    cat >> ${D}${sysconfdir}/fstab <<'EOF'

# --- NanoKVM ---
# /boot is READ-ONLY: nothing writes to the boot partition (see above).
/dev/mmcblk0p1          /boot                vfat   ro,noatime        0  2
configfs                /sys/kernel/config   configfs  defaults       0  0
debugfs                 /sys/kernel/debug    debugfs   defaults       0  0
EOF
}
