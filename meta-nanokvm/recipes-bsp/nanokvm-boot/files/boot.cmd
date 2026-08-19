# NanoKVM boot script, sourced by U-Boot's RAUC bootmeth (boot/bootmeth_rauc.c).
#
# distro_rauc_boot() picks a slot from BOOT_ORDER, decrements that slot's
# BOOT_<slot>_LEFT, saves the environment and only then sources this script,
# having set:
#
#   devnum           MMC device index               "0"
#   distro_bootpart  the slot's boot partition      "1" for both slots
#   distro_rootpart  the slot's root partition      "2" = slot a, "3" = slot b
#   raucargs         slot name for the cmdline      "rauc.slot=a"
#
# Both slots name p1 as their boot partition -- CONFIG_BOOTMETH_RAUC_PARTITIONS
# is "1,2 1,3" -- because the bootmeth probes a filesystem on the boot
# partition (the FAT p1 works, a raw squashfs would not) while the root
# partition only goes through part_get_info(). So the slot is distro_rootpart,
# and that is what selects the FIT here.
#
# The counter is spent before this script runs, so a failure that ends up back
# at bootcmd ("bootflow scan -b; reset") costs exactly one try and reboots into
# the next attempt. That is the intended way to walk BOOT_ORDER: bootmeth_rauc
# offers one slot per boot and has no in-session retry.

# Nothing should source this script except the RAUC bootmeth, which pins
# bootmeths=rauc in the environment. If it happens anyway -- a blank
# environment, a hand-typed `source` -- boot slot a rather than doing something
# undefined with unset variables.
if test -z "${distro_rootpart}"; then
	echo "NanoKVM: no RAUC slot selected; falling back to slot a"
	setenv devnum 0
	setenv distro_bootpart 1
	setenv distro_rootpart 2
	setenv raucargs rauc.slot=a
fi

if test "${distro_rootpart}" = "3"; then
	setenv nkvm_itb boot_b.itb
else
	setenv nkvm_itb boot_a.itb
fi

# net.ifnames=0 keeps the kernel interface names (eth0) instead of udev's
# predictable ones, matching network.eth0.name in the server's config.
#
# loglevel=0 keeps the media drivers from taking the board down with their own
# error reporting. They report back-pressure with pr_err, once per dropped
# frame; at 60fps, on a 115200 serial console, each of those is a synchronous
# in-kernel write on the only core there is, and the board stops scheduling
# userspace altogether -- ping still answers, nothing else does, and only a
# power cycle recovers it. Sipeed's own firmware boots with loglevel=0 for what
# is presumably the same reason. Nothing is lost: the messages still reach the
# ring buffer, so dmesg is unaffected, and only the write to the serial line
# goes away. Raise it at the U-Boot prompt when debugging something that has to
# be seen live.
setenv bootargs console=ttyS0,115200 earlycon=sbi net.ifnames=0 loglevel=0 ${raucargs}

echo "NanoKVM: booting ${nkvm_itb} from mmc ${devnum}:${distro_bootpart} (${raucargs})"

# One read of one hash-verified file. bootm checks every sub-image hash before
# it hands the hardware anything (verify=yes is pinned in the environment), so
# a corrupt read is a clean refusal rather than a silently damaged kernel.
if load mmc ${devnum}:${distro_bootpart} ${kernel_addr_r} ${nkvm_itb}; then
	bootm ${kernel_addr_r}
fi

# Falling through here returns failure to the bootmeth, which returns it to
# `bootflow scan -b`, which returns to bootcmd's `reset`. The try is already
# spent, so the next boot moves on through BOOT_ORDER.
echo "NanoKVM: slot ${raucargs} failed to boot; resetting"
