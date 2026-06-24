# SD card emulation on the LicheeRV Nano (SG2002) — corrected

> The original version of this note proposed forcing `sdio1` into `dr_mode =
> "peripheral"` and binding `g_mass_storage` to the SDIO pins. **That approach
> cannot work** and has been replaced. `g_mass_storage` is a *USB* gadget driver
> (it binds a USB Device Controller, not SDIO), `dr_mode` is a USB-DWC2 property
> that the SDHCI driver ignores, and the SG2002's SDIO blocks are **host-only**
> controllers with no card/target mode. None of it enumerates in an SD slot.

## The actual situation

| Goal | Feasible on SG2002? | How |
| --- | --- | --- |
| Virtual USB drive to a PC over USB-C | ✅ Yes | USB mass-storage gadget (the existing `usb.disk0` function) |
| Emulate an SD card to a **native 4-bit SD host** (PC/phone/modern camera) | ❌ No | No SD-target hardware; needs an FPGA SD-target or an SD-Mux |
| Emulate an SD card to a **SPI-mode SD host** (3D printer / Arduino / MCU) | ✅ Yes (with SPI-slave bring-up) | See `sd-spi-target/` |

## What was built

For the SPI-mode-host case (3D printers etc.), the verified protocol engine and
its native test harness live in [`../sd-spi-target/`](../sd-spi-target/):

- `sd_spi_target.c/.h` — the SD-SPI **card/target** state machine
  (CMD0/CMD8/ACMD41/CMD58/CMD16/CMD17/CMD24), driven one SPI byte at a time.
- `test_host.c` + `make test` — a simulated SD-SPI host that runs the full mount
  handshake and a write→read round-trip with CRC16 verification (passing).

What remains is the SG2002 platform binding (DW-SSI **slave** mode + pinmux +
DMA, and a file/RAM backing store) — see `sd-spi-target/README.md`. That step
needs the SDK submodules checked out and the CV1800B/SG2002 TRM, and it hinges
on confirming the target host actually speaks SD SPI mode.

## Why native SD can't be done in software here

SD identification runs at ≤400 kHz, but data transfer is host-clocked at 25 MHz
across 4 DAT lines with per-line CRC16 and no clock stretching (~40 ns/bit) —
unreachable for software on the 1 GHz C906. SPI mode is the viable path only
because the SPI-slave peripheral does the bit-level shifting in hardware.
