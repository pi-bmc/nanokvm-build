# NanoKVM build overrides

Version-controlled customisation for the NanoKVM (SG200X / `sg2002_licheervnano_sd`)
Buildroot image. These files replace the in-place `sed`/`patch`/`git restore`
surgery the old `build-nanokvm.sh` performed against the vendor submodules.

`build-nanokvm.sh` installs them by copy before the build and restores the
submodules afterward, so the `build/` and `buildroot/` checkouts stay pristine.

## Files

| File | Installs to | Replaces |
|------|-------------|----------|
| `buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig` | `buildroot/configs/` | ~90 lines of defconfig `sed` (package selection: nanokvm + maix-cdk + shrink) |
| `build/boards/sg200x/sg2002_licheervnano_sd/memmap.py` | `build/boards/.../` | RAM-split `sed` (ION `75M`, bootlogo `5632K`) |
| `build/tools/common/sd_tools/genimage_rootless.cfg` | `build/tools/common/sd_tools/` | USB gadget / partition `sed`s |
| `build/tools/common/sd_tools/sd_gen_burn_image_rootless.sh` | `build/tools/common/sd_tools/` | USB gadget / hostname-prefix `sed`s |
| `overlay/etc/init.d/S99ipmi_sim` | `buildroot/board/cvitek/SG200X/overlay/` | `patches/buildroot.patch` |

The fixed feature set baked into the defconfig is **nanokvm=y, maix-cdk=y,
shrink=y** (tailscale, tpu-demo, tpu-sdk, oss-tarball all off).

## Regenerating the defconfig

The defconfig is the pinned base
(`buildroot/configs/cvitek_SG200X_musl_riscv64_defconfig`) with the old script's
nanokvm/maix-cdk/shrink transforms applied once. If the upstream base changes,
re-derive it by applying those same package edits to the new base rather than
editing by hand, then commit the result here.
