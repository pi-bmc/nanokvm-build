# Mount /boot, configfs and debugfs from fstab instead of from S01fs.
#
# Ordering is the whole point. sysvinit runs:
#     rcS S03mountall.sh   -> mount -a   (fstab)
#     rcS S04udev          -> udevd + `udevadm trigger` coldplug replay
# The USB gadget is now built by a udev rule on the udc "add" event
# (60-nanokvm-usbgadget.rules). That event was emitted during kernel init, long
# before userspace, so udev only sees it at the coldplug replay in S04 — by
# which time these three mounts must already exist:
#
#   /boot               the rule's script reads /boot/usb.ecm0, usb.vid, ...
#   /sys/kernel/config  the gadget is assembled under configfs/usb_gadget
#
# S01fs used to mount them at rc5.d/S06, i.e. after S04udev. Leaving them there
# would have made the gadget come up with none of its /boot/usb.* config
# visible, silently dropping the ECM NIC and the VID/PID overrides.
#
# base-files owns /etc/fstab, so append here rather than shipping a second
# /etc/fstab from nanokvm-gadget (which would be an ipk file conflict).
#
# /data is intentionally absent: mmcblk0p3 does not exist until S01fs creates it
# on first boot, and a missing fstab device makes mountall noisy.
do_install:append:sg2002-licheervnano() {
    cat >> ${D}${sysconfdir}/fstab <<'EOF'

# --- NanoKVM ---
/dev/mmcblk0p1       /boot                vfat       defaults,noatime      0  2
configfs             /sys/kernel/config   configfs   defaults              0  0
debugfs              /sys/kernel/debug    debugfs    defaults              0  0
EOF
}
