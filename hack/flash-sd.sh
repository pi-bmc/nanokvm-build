#!/usr/bin/env bash
# Flash the built NanoKVM SD image (Sophgo SG2002 / LicheeRV Nano) onto the
# first SD card found on this system.
#
#   hack/flash-sd.sh [-y] [--dry-run] [--no-verify] [-d /dev/sdX] [-i path/to.wic]
#
# The image is the wic built by `kas build kas.yml`, four MBR primaries:
#   p1  FAT16 "BOOT"       fip.bin (FSBL+OpenSBI+U-Boot), Image, DTB,
#                          initramfs, extlinux/extlinux.conf, slot selector
#   p2  512M               rootfs slot A (squashfs-zst)
#   p3  512M               rootfs slot B (empty placeholder)
#   p4  640M ext4          /var/lib/nanokvm, pre-populated; the initramfs
#                          grows it to fill the card on first boot
#
# "SD card" means, in preference order:
#   1. a real SD/MMC controller disk (/dev/mmcblk*, skipping the eMMC
#      boot0/boot1/rpmb side-devices), then
#   2. a removable USB disk (card reader or USB stick).
# Any disk backing a system mount (/, /boot, /home, ...) or swap is never
# considered, whatever it is. Auto-mounted partitions on the chosen card are
# unmounted before flashing. Flashing still destroys whatever is on the card
# -- hence the confirmation prompt (skip with -y).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG="${REPO_ROOT}/build/tmp-musl/deploy/images/sg2002-licheervnano/licheervnano-kvm_sd.img"
DEVICE=""
ASSUME_YES=0
DRY_RUN=0
VERIFY=1

die() { echo "flash-sd: error: $*" >&2; exit 1; }

usage() {
    sed -n '2,21p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -y|--yes)     ASSUME_YES=1 ;;
        --dry-run)    DRY_RUN=1 ;;
        --no-verify)  VERIFY=0 ;;
        -d|--device)  DEVICE="${2:?-d needs a device}"; shift ;;
        -i|--image)   IMG="${2:?-i needs a path}"; shift ;;
        -h|--help)    usage ;;
        *)            usage 1 ;;
    esac
    shift
done

[ -r "${IMG}" ] || die "image not found: ${IMG}
run 'kas build kas.yml' first, or point -i at an image"

# The deploy-dir name is a symlink to the timestamped .wic. Resolve it: the
# sibling .bmap is named after the real file, and `du`/`stat` on a symlink
# report the link (0 bytes), not the image.
IMG="$(readlink -f "${IMG}")"

# wic writes a block map next to the image. It matters here: the image is 1.7
# GiB of which only ~88 MiB is mapped (the rest is holes in the sparse rootfs
# B and data partitions), so a plain dd moves ~20x more data than the card
# actually needs. bmaptool writes just the mapped ranges. Strip a compression
# suffix when looking for it -- the bmap describes the uncompressed image.
BMAP="${IMG%.[gx]z}"; BMAP="${BMAP%.zst}"; BMAP="${BMAP%.bz2}"; BMAP="${BMAP}.bmap"
[ -r "${BMAP}" ] || BMAP=""

# Authoritative image size. For a compressed image stat() would report the
# compressed size, so prefer the bmap's own ImageSize when we have it.
if [ -n "${BMAP}" ]; then
    IMG_SIZE=$(sed -n 's:.*<ImageSize>[[:space:]]*\([0-9]\+\).*:\1:p' "${BMAP}" | head -1)
else
    IMG_SIZE=$(stat -Lc %s "${IMG}")
fi
[ -n "${IMG_SIZE}" ] || die "could not determine the size of ${IMG}"

human() { numfmt --to=iec --suffix=B "$1" 2>/dev/null || echo "$1 bytes"; }

# Disks that hold the running system: never candidates, not even with -d.
system_disks() {
    local mp src
    for mp in / /boot /boot/efi /boot/firmware /home /usr /var; do
        src=$(findmnt -no SOURCE "${mp}" 2>/dev/null) || continue
        # Resolve partition -> whole disk (PKNAME empty when src IS the disk).
        lsblk -no PKNAME "${src}" 2>/dev/null | sed 's|^|/dev/|'
        echo "${src}"
    done
    # Swap devices too.
    lsblk -rnpo NAME,PKNAME,FSTYPE | awk '$3 == "swap" { print $1; if ($2 != "") print "/dev/" $2 }'
}

# NB: collect first, then match. `system_disks | grep -qxF` looks equivalent
# but is not: grep exits at the first match, which can SIGPIPE the producer,
# and under `set -o pipefail` the pipeline then reports 141 even though grep
# matched -- i.e. the guard says "not a system disk" about a disk that IS one.
# On a script whose next step is dd-over-the-whole-disk, that must not happen.
is_system_disk() {
    local dev="$1" all
    all=$(system_disks)
    grep -qxF "${dev}" <<<"${all}"
}

find_sd() {
    local name type tran rm size
    # Pass 1: SD/MMC controller disks (skip eMMC boot0/boot1/rpmb siblings).
    # Pass 2: removable USB disks. Devices reporting 0 bytes are empty
    # card-reader slots, not cards -- skip them.
    for pass in mmc usb; do
        while read -r name type tran rm size; do
            [ "${type}" = "disk" ] || continue
            [ "${size:-0}" -gt 0 ] || continue
            case "${name}" in *boot[01]|*rpmb) continue ;; esac
            case "${pass}" in
                mmc) case "${name}" in /dev/mmcblk*) ;; *) continue ;; esac ;;
                usb) { [ "${tran}" = "usb" ] && [ "${rm}" = "1" ]; } || continue ;;
            esac
            is_system_disk "${name}" && continue
            echo "${name}"
            return 0
        done < <(lsblk -dbnpo NAME,TYPE,TRAN,RM,SIZE)
    done
    return 1
}

if [ -n "${DEVICE}" ]; then
    [ -b "${DEVICE}" ] || die "not a block device: ${DEVICE}"
    is_system_disk "${DEVICE}" && die "refusing ${DEVICE}: it backs a system mount"
    [ "$(lsblk -dbnpo SIZE "${DEVICE}")" -gt 0 ] || \
        die "refusing ${DEVICE}: it reports 0 bytes (empty card-reader slot?)"
else
    DEVICE=$(find_sd) || die "no SD card found (no non-system, non-empty /dev/mmcblk* disk or removable USB disk)
insert a card, or pass one explicitly with -d"
fi

# A card too small for the image is the failure worth catching early: dd would
# write p1 and part of the rootfs, then die on ENOSPC, leaving a card that
# still boots FSBL and U-Boot from the FAT partition but has a truncated
# rootfs -- a half-working state that looks like a kernel bug, not a bad flash.
DEV_SIZE=$(blockdev --getsize64 "${DEVICE}" 2>/dev/null || lsblk -dbnpo SIZE "${DEVICE}")
if [ "${DEV_SIZE}" -lt "${IMG_SIZE}" ]; then
    die "card is too small: ${DEVICE} holds $(human "${DEV_SIZE}"), image needs $(human "${IMG_SIZE}")"
fi

echo "Image : ${IMG}"
echo "        $(human "${IMG_SIZE}")${BMAP:+ (bmap: writing mapped blocks only)}"
echo "Target: ${DEVICE} ($(human "${DEV_SIZE}"))"
lsblk -o NAME,SIZE,MODEL,TRAN,RM,MOUNTPOINTS "${DEVICE}"

if [ "${DRY_RUN}" = "1" ]; then
    echo "dry run: would flash the image above; stopping here"
    exit 0
fi

if [ "${ASSUME_YES}" != "1" ]; then
    printf "This ERASES %s. Type 'yes' to continue: " "${DEVICE}"
    read -r reply
    [ "${reply}" = "yes" ] || die "aborted"
fi

SUDO=""
[ "$(id -u)" = "0" ] || SUDO="sudo"

# Desktop automounters re-grab partitions as soon as the table is re-read, so
# unmounting is a loop, not a single pass.
unmount_all() {
    local mp busy attempt
    for attempt in 1 2 3; do
        [ "${attempt}" = "1" ] || echo "retrying unmount (pass ${attempt})"
        busy=0
        while read -r mp; do
            [ -n "${mp}" ] || continue
            echo "unmounting ${mp}"
            ${SUDO} umount "${mp}" || busy=1
        done < <(lsblk -lnpo MOUNTPOINT "${DEVICE}" | grep -v '^$' || true)
        [ "${busy}" = "0" ] && return 0
        sleep 1
    done
    die "could not unmount every partition of ${DEVICE}"
}

unmount_all

if [ -n "${BMAP}" ] && command -v bmaptool >/dev/null 2>&1; then
    ${SUDO} bmaptool copy --bmap "${BMAP}" "${IMG}" "${DEVICE}"
else
    [ -n "${BMAP}" ] && echo "note: bmaptool not installed, falling back to dd (writes the full $(human "${IMG_SIZE}"))"
    ${SUDO} dd if="${IMG}" of="${DEVICE}" bs=4M conv=fsync oflag=direct status=progress
fi
sync
${SUDO} blockdev --flushbufs "${DEVICE}" 2>/dev/null || true

# Read the card back and compare. bmaptool's own checksumming only validates
# what it READ from the image; nothing so far has looked at what actually
# landed on the card, which is the failure mode a worn or counterfeit card
# produces (writes ack'd, data not retained).
if [ "${VERIFY}" = "1" ]; then
    echo "verifying..."
    if [ -n "${BMAP}" ] && command -v python3 >/dev/null 2>&1; then
        ${SUDO} python3 - "${DEVICE}" "${BMAP}" <<'PY'
import hashlib, sys, xml.etree.ElementTree as ET

dev, bmap = sys.argv[1], sys.argv[2]
root = ET.parse(bmap).getroot()

def text(tag, default=None):
    e = root.find(tag)
    return e.text.strip() if e is not None and e.text else default

bs = int(text('BlockSize'))
img_size = int(text('ImageSize'))
algo = (text('ChecksumType', 'sha256') or 'sha256').lower()

bad = total = 0
with open(dev, 'rb') as f:
    for rng in root.find('BlockMap'):
        want = rng.get('chksum')
        if not want:
            continue
        first, _, last = rng.text.strip().partition('-')
        first = int(first)
        last = int(last) if last else first
        total += 1

        h = hashlib.new(algo)
        f.seek(first * bs)
        # The bmap checksums the image, which is zero-padded to a whole block;
        # the device holds whatever was there beyond the image end, so clamp
        # the read at ImageSize and pad the same way bmaptool does.
        remaining = (last - first + 1) * bs
        readable = max(0, min(remaining, img_size - first * bs))
        pad = remaining - readable
        while readable:
            chunk = f.read(min(readable, 1 << 20))
            if not chunk:
                break
            h.update(chunk)
            readable -= len(chunk)
        while pad:
            n = min(pad, 1 << 20)
            h.update(b'\0' * n)
            pad -= n

        if h.hexdigest() != want:
            bad += 1
            print("  MISMATCH at blocks %d-%d (byte offset %d)"
                  % (first, last, first * bs), file=sys.stderr)

print("  %d/%d ranges match" % (total - bad, total))
sys.exit(1 if bad else 0)
PY
    else
        ${SUDO} cmp -n "${IMG_SIZE}" "${IMG}" "${DEVICE}"
        echo "  image matches card"
    fi
fi

${SUDO} blockdev --rereadpt "${DEVICE}" 2>/dev/null || true

# The partition re-read makes desktop automounters grab the fresh FAT
# partition; wait for udev to finish and unmount again so the card can be
# pulled straight away.
udevadm settle 2>/dev/null || true
sleep 2
unmount_all
sync

echo "done: ${DEVICE} now carries the NanoKVM image"
echo "(p1 FAT16 BOOT: fip.bin + Image + DTB + initramfs + extlinux;"
echo " p2/p3 rootfs A/B; p4 ext4 nkvm-data, grown to fill the card on first boot)"
echo "all partitions unmounted -- safe to remove the card"
