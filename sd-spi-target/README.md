# SD card emulation (SPI mode) for the SG2002 / LicheeRV Nano

Make the board present itself as a microSD card to a **simple SPI-mode SD host**
— 3D-printer mainboards (Marlin/RepRap), Arduino/MCU `SD.h`-class readers, and
similar embedded hosts that talk to SD cards over plain SPI (CS/CLK/MOSI/MISO).

> **This only works if the external host uses SD *SPI mode*.** PCs, phones, and
> modern cameras use native 4-bit SD mode and will not fall back to SPI — for
> those, software emulation is impossible on this SoC (see *Why not native SD*).

## What's here (done + verified)

| File | Status |
|------|--------|
| `sd_spi_target.c/.h` | SD-SPI **card/target** protocol engine — portable, no deps |
| `test_host.c` | Native SD-SPI **host** simulator that drives the engine |
| `Makefile` | `make test` builds and runs the round-trip test |

The engine implements the SPI-mode handshake a host issues at mount, then
single-block read/write:

```
CMD0  GO_IDLE_STATE      -> R1 = 0x01 (idle)
CMD8  SEND_IF_COND       -> R7, echoes voltage + 0xAA check pattern (v2 card)
CMD58 READ_OCR           -> R3, OCR with CCS=1 once initialised (SDHC)
CMD55+CMD41 (ACMD41)     -> polled until the idle bit clears
CMD16 SET_BLOCKLEN       -> accepted (fixed 512 on SDHC)
CMD17 READ_SINGLE_BLOCK  -> R1, 0xFE token, 512 data bytes, CRC16
CMD24 WRITE_SINGLE_BLOCK -> R1, absorb 512 + CRC16, data-response 0x05, busy
unknown                  -> R1 with the illegal-command bit (0x04)
```

`make test` exercises all of the above against an in-RAM disk, including a
write→read round-trip with CRC16 verification:

```
$ make test
CMD0  -> R1=0x01
CMD8  -> R1=0x01 R7=000001AA
CMD58 -> R1=0x01 OCR=00FF8000
ACMD41-> R1=0x00 after 2 tries
CMD58 -> R1=0x00 OCR=C0FF8000 (CCS=1)
CMD24 -> R1=0x00 data-resp=0x05
CMD17 -> R1=0x00 data CRC16=0x6B2F (calc 0x6B2F)
CMD1  -> R1=0x04 (illegal bit=1)
ALL TESTS PASSED
```

The engine is driven one SPI byte at a time:
`miso = sd_spi_target_step(&t, mosi)`. That maps directly onto an SPI-slave
RX/TX FIFO — for each byte the host clocks in, you emit the returned byte.

## What's NOT done — SG2002 platform binding (needs the SDK + datasheet)

This is the hardware bring-up that turns the verified engine into a working
device. It needs the submodules checked out and the CV1800B/SG2002 TRM, so it's
deliberately left as a documented integration rather than guessed registers:

1. **Confirm SPI-slave capability.** The SG2002 SPI controllers are Synopsys
   DW_apb_ssi (you can see `SPI2` muxed on P21–P23 in
   [../best-practice.md](../best-practice.md)). Verify in the TRM that the
   instance you use supports **slave mode**, its **max slave clock** exceeds your
   host's SPI rate (3D printers are typically ≤ a few MHz, well within range),
   and that the RX/TX FIFOs can be DMA-serviced.
2. **Device tree:** enable that SPI controller in **slave** mode, pinmux CS/CLK/
   MOSI/MISO to your FPC pins, and *disable* any conflicting function on those
   pads. (Do this in the board `.dts` via the existing `nanokvm/` overlay flow,
   not by hand-editing the submodule.)
3. **HAL glue:** feed each received SPI byte to `sd_spi_target_step()` and push
   the result to the TX FIFO. Run it on a **dedicated context** (the C906L +
   FreeRTOS small core is the natural home) or a DMA + tight ISR so it keeps up
   with back-to-back bytes and the host's chip-select framing.
4. **Backing store:** implement `sd_blockstore_t.read/write` over a file
   (`/var/virtual_sd.img`) or a RAM buffer. Mind the same concurrent-access
   caveat as any shared FAT image: don't mutate it from Linux while the host is
   mid-transfer.

## Open questions to resolve during bring-up

- **CS framing:** this engine tracks the command/response stream but does not see
  chip-select. With the DW-SSI slave, use the CS-deassert/idle to resync
  (`t->state = SD_ST_CMD; t->cmd_len = 0;`) if a host aborts mid-frame.
- **Clock rate headroom:** measure the host's actual SPI clock; if it exceeds the
  SPI-slave max or your servicing latency, you'll see CRC/timeout errors at mount.
- **CRC enforcement:** CRC is off by default in SPI mode (only CMD0/CMD8 carry a
  checked CRC7). The engine computes correct CRC16 on reads and ignores it on
  writes, which matches typical hosts; enable checking if your host sends CMD59.

## Why not native 4-bit/1-bit SD mode

The SG2002's SDIO0/SDIO1 are DesignWare MSHC **host** controllers — there is no
register to make them a bus *target*, so no driver (Linux or bare-metal) can
turn them into a card. Bit-banging native SD on GPIO fails too: identification
runs at ≤400 kHz (borderline feasible), but data transfer is clocked by the host
at **25 MHz across 4 DAT lines with per-line CRC16 and no clock stretching** —
~40 ns/bit, far below what the 1 GHz C906 can sample/respond to in software.
Real native-SD emulators put the line-rate path in an **FPGA/CPLD**. SPI mode is
viable precisely because the SPI-slave peripheral does the bit-level shifting in
hardware and hands software whole bytes.

If your host turns out to use native SD mode, the options are an FPGA SD-target
or an **SD-Mux** (a real card electronically switched between the board and the
host) — neither of which this firmware addresses.
