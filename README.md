# NanoKVM Yocto Build

A Yocto/OpenEmbedded build for the Sipeed NanoKVM / LicheeRV Nano
(`sg2002-licheervnano`, Sophgo SG2002, riscv64/musl). Produces an SD-card
image with an immutable squashfs root (A/B slots + volatile overlay, assembled
by a small initramfs) running the pure-Go NanoKVM BMC server
([pi-bmc/nanokvm-app](https://github.com/pi-bmc/nanokvm-app)).

Local layers (in this repo):

- `meta-sophgo` — machine config, mainline kernel/U-Boot/OpenSBI, FSBL
- `meta-nanokvm` — distro, the `nanokvm-image`/`nanokvm-initramfs-image`
  targets, the NanoKVM server recipe, and all image glue
- `meta-raspberrypi` — vendored layer for the "rpi" multiconfig: builds the
  aarch64 U-Boot + TF-A boot image the USB mass-storage gadget serves to the
  managed Raspberry Pi

Fetched layers (by kas): `poky` (meta, meta-poky) and `meta-openembedded`
(meta-oe, meta-networking, meta-python), branch `scarthgap`. Nothing else —
meta-riscv is not needed (oe-core carries the riscv64 tune) and the Sophgo
vendor SDK layer is gone entirely (the build is fully open-source).

## Building

[kas](https://kas.readthedocs.io/) fetches the upstream layers and configures
the build; it is the single supported entry point:

```
pip3 install kas
kas build kas.yml
```

Key settings (see `kas.yml`): `MACHINE = "sg2002-licheervnano"`,
`DISTRO = "nanokvm"`, `TCLIBC = "musl"`, plus a second `rpi` multiconfig
(aarch64/poky/glibc) for the Pi boot image.

## Output

`build/tmp-musl/deploy/images/sg2002-licheervnano/`:

- `licheervnano-kvm_sd.img.xz` (alias of `nanokvm-image-*.wic.xz`) — flash to
  a microSD card (Balena Etcher, `bmaptool`, or `xzcat | dd`).

The SD layout is p1 FAT boot (kernel, DTB, fip.bin, initramfs, extlinux,
config seeds), p2/p3 512 MB rootfs A/B squashfs slots, and an ext4 data
partition created on first boot from the remaining space (mounted at
`/var/lib/nanokvm`; all persistent state lives there).
