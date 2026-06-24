# NanoKVM build overrides

Version-controlled customisation for the NanoKVM (SG200X / `sg2002_licheervnano_sd`)
Buildroot image. These files replace the in-place `sed`/`patch`/`git restore`
surgery the old `build-nanokvm.sh` performed against the vendor submodules.

`build-nanokvm.sh` installs them by copy before the build and restores the
submodules afterward, so the `build/` and `buildroot/` checkouts stay pristine.

## Files

| File | Installs to | Replaces |
| --- | --- | --- |
| `buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig` | `buildroot/configs/` | ~90 lines of defconfig `sed` (package selection: nanokvm + maix-cdk + shrink) |
| `build/boards/sg200x/sg2002_licheervnano_sd/memmap.py` | `build/boards/.../` | RAM-split `sed` (ION `75M`, bootlogo `5632K`) |
| `build/tools/common/sd_tools/genimage_rootless.cfg` | `build/tools/common/sd_tools/` | USB gadget / partition `sed`s |
| `build/tools/common/sd_tools/sd_gen_burn_image_rootless.sh` | `build/tools/common/sd_tools/` | USB gadget / hostname-prefix `sed`s |
| `overlay/etc/init.d/S99ipmi_sim` | `buildroot/board/cvitek/SG200X/overlay/` | `patches/buildroot.patch` |

The fixed feature set baked into the defconfig is **nanokvm=y, maix-cdk=y,
shrink=y** (tailscale, tpu-demo, tpu-sdk, oss-tarball all off).

## Source patches (`../patches/build/`)

Localized edits to vendor **source** files (vs. whole-file overlays) are kept as
patches in the repo root so the submodule git history stays pristine.
`build-nanokvm.sh` applies them with `git -C build apply` (idempotent) and the
EXIT trap reverts the submodule.

`patches/<submodule>/*.patch` is applied with `git -C <submodule> apply`.

| Patch | Target | Purpose |
| --- | --- | --- |
| `build/0001-licheervnano-uboot-spi2-slave-pinmux.patch` | `build:.../u-boot/cvi_board_init.c` | Re-mux SDIO1 WiFi pads (P18/P21/P22/P23) to **SPI2** |
| `build/0002-licheervnano-dts-wifi-off-spi2-slave.patch` | `build:.../dts_riscv/...dts` | Disable `wifi-sd@4320000`; strip SPI2 LCD; SPI2 → `compatible = "cvitek,dw-ssi-sd-slave"`, enabled |
| `build/0003-licheervnano-kernel-disable-aic-wlan.patch` | `build:.../linux/...defconfig` | `# CONFIG_AIC_WLAN_SUPPORT is not set` + `CONFIG_SPI_DW_SD_SLAVE=y` |
| `linux_5.10/0001-spi-register-dw-ssi-sd-slave.patch` | `linux_5.10:drivers/spi/{Kconfig,Makefile}` | Register the DW-SSI slave SD-emulation driver |

The SD-emulation driver + engine sources (`../sd-spi-target/`) are copied into
`linux_5.10/drivers/spi/` by `build-nanokvm.sh` (the patch only wires Kconfig/
Makefile). See `../sd-spi-target/README.md` for the driver and its remaining
hardware-validation caveats (the SPI2 instance must be synthesised slave-capable).

## Regenerating the defconfig

The defconfig is the pinned base
(`buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig`) with the old script's
nanokvm/maix-cdk/shrink transforms applied once. If the upstream base changes,
re-derive it by applying those same package edits to the new base rather than
editing by hand, then commit the result here.
