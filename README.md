# NanoKVM Yocto Build

A Yocto/OpenEmbedded build for Sophgo cv181x/sg200x based boards — primarily the
Sipeed LicheeRV Nano / NanoKVM (`sg2002-licheervnano`).

The board support, BSP recipes and image are provided by three local layers:

- `meta-sophgo` — machine config, kernel, U-Boot, OpenSBI, FSBL, osdrv
- `meta-sophgo-sdk` — Sophgo multimedia middleware, codec firmware, pinmux, PMIC
- `meta-nanokvm` — the `nanokvm-image` target and NanoKVM server/app recipes

All sources are fetched from upstream git by the recipes; there are no submodules.

# Building

## With kas (recommended)

[kas](https://kas.readthedocs.io/) fetches the upstream layers (poky,
meta-openembedded, meta-riscv) automatically and configures the build:

```
pip3 install kas
kas build kas.yml
```

## Manual setup

`setup-yocto.sh` clones the upstream layers into `yocto-layers/` and writes the
build configuration into `build-yocto/`:

```
./setup-yocto.sh
source build-yocto/env.sh
bitbake nanokvm-image
```

# Configuration

Key settings (see `kas.yml`):

- `MACHINE = "sg2002-licheervnano"`
- `DISTRO  = "nanokvm"`
- `TCLIBC  = "musl"` (matches the original Buildroot build)
- Yocto release branch: `scarthgap`

`DL_DIR` and `SSTATE_DIR` are shared at the repo root (`downloads/`,
`sstate-cache/`) so they persist across build directories.

# Output

The resulting SD-card image is produced under `build-yocto/tmp/deploy/images/sg2002-licheervnano/`.
Flash it to a microSD card with a tool such as Balena Etcher.

# Compatibility

Targets RISC-V NanoKVM products (Cube, Lite, PCIe) and other Sophgo
cv181x/sg200x boards such as the LicheeRV Nano and MilkV Duo256/DuoS.
