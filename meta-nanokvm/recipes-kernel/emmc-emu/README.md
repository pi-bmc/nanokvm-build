# NanoKVM software eMMC device emulator

Make the NanoKVM (Sophgo **SG2002 / CV181x**, RISC-V) present itself as an
**eMMC card** to an external host — a Raspberry Pi running **U-Boot** — over the
six SDIO1 pads, so the Pi can use it as block storage (and, ultimately, as the
backing store for its U-Boot EFI variables → "instant board control").

This replaces the SDIO1 **WiFi** function on those pads.

> **Read this first — what this is and isn't.**
> The SG2002 has only SD/eMMC **host** controllers (SDHCI). It has **no SD/eMMC
> device (card) peripheral** — the dedicated silicon an ESP32-C6 has as its
> "SDIO Slave" block. So there is nothing to offload the bit-level protocol to;
> this driver does it **in software on the CPU**, bit-banging the card side of
> the bus from a real-time kthread. On a **single 1 GHz C906 core** that is good
> enough to pass U-Boot's identification handshake at the host's **400 kHz**
> floor (the "is an eMMC connected?" milestone). Sustained block data at the
> eMMC default 26 MHz is *not* achievable on one core and will stall the box for
> the duration of a transfer; see **Timing reality** below. The genuinely robust
> path for line-rate data is an external SD-slave chip (an ESP32-C6 is exactly
> that part) — this driver is the no-extra-hardware software approximation.

---

## How it works

```
  Raspberry Pi (HOST, U-Boot)                 NanoKVM SG2002 (this driver)
  ┌───────────────────────┐                   ┌──────────────────────────────┐
  │ bcm2835/iproc SDHCI    │   CLK ───────────▶│ porte gpio23  (sampled)      │
  │  drives CLK @ 400 kHz  │   CMD ◀──────────▶│ porte gpio22  (bit-banged)   │
  │  sends CMD0/1/2/3/9/7  │   DAT0 ◀─────────▶│ porte gpio21  (bit-banged)   │
  │  expects R1/R2/R3      │   DAT1..3 ────────│ porte gpio20..18             │
  └───────────────────────┘                   └──────────────────────────────┘
```

Five translation units:

| File | Role |
|------|------|
| `emmc_crc.c/.h` | CRC7 (commands/responses) and CRC16-CCITT (data). Verified against the canonical CMD0 vector (`0x4A`) and by independent long-division fuzzing. |
| `emmc_io.h` | Inline hot-path line accessors — one relaxed MMIO op each, software DDR/DR shadows. |
| `emmc_gpio.c` | ioremaps the PWR GPIO bank + IOMUX, re-muxes the six SD1 pads from SDIO1 (funcsel 0) to PWR_GPIO (funcsel 3), restores on unload. |
| `emmc_phy.c` | The software shift register. Recovers bit timing by polling CLK edges; samples CMD/DAT on rising edges, drives outputs on falling edges. This is the part the ESP32-C6 does in silicon. |
| `emmc_proto.c` | The eMMC **memory** state machine and register models (OCR/CID/CSD/EXT_CSD, CMD17/18/24/25). This is the part the ESP `sdio_slave` driver does **not** have (it's an SDIO *I/O* function). |
| `emmc_main.c` | Platform driver, RT sampler kthread, `/dev/emmc-emu0` backing-store window, module params. |

### The "fool U-Boot" path

From the board's own `u-boot/drivers/mmc/mmc.c`, an eMMC bring-up is:
`CMD0 → CMD1 (OCR) → CMD2 (CID) → CMD3 (RCA) → CMD9 (CSD) → CMD7 (select)` and
then, **only if `CSD.SPEC_VERS ≥ 4`**, `CMD8 (EXT_CSD)`. `mmc_startup_v4()`
returns early otherwise:

```c
if (IS_SD(mmc) || (mmc->version < MMC_VERSION_4))
    return 0;            /* no EXT_CSD, no data transfer to be detected */
```

So the default mode advertises **`SPEC_VERS = 3`** and reports capacity from the
**CSD `C_SIZE`** — the host is satisfied with command/response frames alone, **no
data phase**. That is the milestone this driver targets and the most robust
thing one core can do.

The CSD also advertises a deliberately low `TRAN_SPEED` (~100 kHz). The bcm2835
host floors the bus at `MIN_FREQ = 400000`, so the negotiated clock pins to
**400 kHz = 2.5 µs/bit** — the slowest the host will ever clock, which is what
makes software sampling viable.

---

## Timing reality (please read before judging results)

- **Bit period at 400 kHz = 2.5 µs.** A single relaxed MMIO read of the GPIO
  input register is on the order of tens of ns here, so there are ~tens of
  samples per bit — enough to detect edges and sample reliably **inside an
  IRQ-off window**.
- **Command + response ≈ 48–136 bits ≈ 120–340 µs.** That is the IRQ-off
  window per transaction. Fine on one core.
- **A 512-byte data block at 400 kHz, 1-bit = ~10 ms of continuous IRQ-off
  sampling.** That stalls the single core (we `touch_softlockup_watchdog()` to
  survive it, but timers/RCU on that CPU are frozen for the duration). At the
  eMMC default 26 MHz it's a non-starter — one MMIO access already exceeds the
  ~38 ns bit period.
- Therefore: **detection / `mmc info` — realistic. Bulk `mmc read/write` —
  best-effort and disruptive.** Capacities ≤ 1 GiB work without any data phase
  via the legacy CSD path; > 1 GiB needs EXT_CSD (`emmc_force_legacy=0`) and
  therefore the data phase.

If you need real throughput, run the PHY on the SG2002's **second C906** as
bare-metal firmware (no Linux jitter, no shared core) or put an **ESP32-C6**
between the boards as a true hardware SD slave. Both are larger projects; this
driver is the single-core, no-extra-hardware version.

---

## Build & install (Yocto)

Two recipes live here:

- `emmc-emu_0.1.bb` — the kernel module (`inherit module`), auto-loaded.
- `emmc-emud_0.1.bb` — the userspace backing-store daemon.

Add to your image (e.g. `meta-nanokvm/recipes-core/images/nanokvm-image.bb`):

```
IMAGE_INSTALL:append = " kernel-module-emmc-emu emmc-emud"
```

**Required device-tree change:** disable the SDIO1 host controller so it does
not power-sequence or re-mux these pads. In
`arch/riscv/boot/dts/cvitek/sg2002_licheervnano_sd.dts` set the `wifisd`
node `status = "disabled";` (see `emmc-emu.dts` for the overlay form). The
emulator self-instantiates from module init, so no emulator DT node is needed.

Module parameters (`/etc/modprobe.d/emmc-emu.conf` or insmod args):

| Param | Default | Meaning |
|-------|---------|---------|
| `emmc_capacity_mb` | 16 | Emulated size, RAM-backed (legacy cap 1024; keep small — board RAM is ~256 MiB). |
| `emmc_force_legacy` | 1 | SPEC_VERS<4, no EXT_CSD (robust detection path). |
| `emmc_clk_spin` | 200000 | Per-edge MMIO sample budget before "clock stalled". |
| `emmc_cpu` | 0 | CPU to pin the sampler to. |
| `emmc_image` | (none) | File to preload into the backing store. |
| `emmc_self_device` | 1 | Auto-register the platform device (0 if using DT). |

---

## Bring-up & test

On the NanoKVM:

```sh
modprobe emmc_emu emmc_capacity_mb=256
dmesg | grep emmc-emu          # "eMMC emulator up: 256 MiB, legacy ... /dev/emmc-emu0"
emmc-emud --info               # live bus stats from the module
```

Wire NanoKVM SD1 pads → Pi SD/eMMC pins (CLK/CMD/DAT0..3 + GND). On the Pi's
U-Boot console:

```
=> mmc dev 1
=> mmc info        # success == "Device: ... / Capacity: 256 MiB" → it's fooled
```

Watch `emmc-emud --info` on the NanoKVM: `last cmd` should climb through
`CMD0,1,2,3,9,7` and `serviced` should increase. `crc_errors` climbing means
sampling is losing bits (lower the bus clock / shorten wiring / check grounds).

Persist host writes (the UEFI-var use case):

```sh
emmc-emud --image /var/lib/nanokvm/emmc.img --load --interval 5
```

---

## IDE note

Opening the `src/*.c` files in a userspace IDE shows errors like
`linux/spinlock.h not found` / `unknown type 'u8'`. Those are **false
positives** — the IDE's C analyzer has no kernel headers and no `__KERNEL__`.
The Yocto `module` build (and any `make KERNEL_SRC=...`) compiles cleanly.

## Citation trail (verified against this repo)

- SD1 pads = PWR GPIO bank `porte @ 0x05021000`, bits 18–23; funcsel in IOMUX
  `@ 0x03001000 + 0xd0..0xe4`, value 3 = GPIO — `drivers/pinctrl/cvitek/
  cv181x_pinlist_swconfig.h`, `cv181x_reg_fmux_gpio.h`.
- DW-APB GPIO offsets `DR 0x00 / DDR 0x04 / EXT_PORTA 0x50` — `drivers/gpio/
  gpio-dwapb.c`.
- Host clock floor `MIN_FREQ 400000`, quirks `BROKEN_R1B | NO_HISPD_BIT` —
  `u-boot/drivers/mmc/bcm2835_sdhci.c`.
- eMMC init order, OCR/CSD parse, EXT_CSD bypass for `SPEC_VERS<4` —
  `u-boot/drivers/mmc/mmc.c` (`mmc_startup`, `mmc_startup_v4`).
- Single Linux core — `arch/riscv/boot/dts/cvitek/soph_base_riscv.dtsi`
  (`cpus { cpu@0 ... }`).
- Why the ESP reference can't be ported for the hard part — ESP-IDF
  `components/esp_driver_sdio/src/sdio_slave.c` is hardware-descriptor/DMA
  programming with **no** bit-level protocol or CRC code; the silicon does it.
