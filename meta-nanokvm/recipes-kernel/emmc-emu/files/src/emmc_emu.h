/* SPDX-License-Identifier: GPL-2.0 */
/*
 * emmc_emu.h - NanoKVM eMMC device (card) emulator.
 *
 * The SG2002 (CV181x) has only SD/eMMC *host* controllers (SDHCI). To present
 * an eMMC *card* to an external host (a Raspberry Pi running U-Boot) we abandon
 * the host controller entirely, re-mux the SDIO1 pads as plain GPIO, and
 * bit-bang the eMMC card side of the bus in software. This header is the shared
 * contract between the four translation units:
 *
 *   emmc_crc.c   - CRC7 (commands/responses) and CRC16 (data)
 *   emmc_gpio.c  - pad ownership + fast MMIO line access
 *   emmc_phy.c   - the timing-critical bit layer (receive cmd / drive resp+data)
 *   emmc_proto.c - the eMMC state machine, register models, command handlers
 *   emmc_main.c  - platform driver, RT sampling kthread, char dev, backing store
 *
 * Everything here is derived from real silicon facts verified against the
 * board's own kernel/u-boot sources; see README.md for the citation trail.
 */
#ifndef EMMC_EMU_H
#define EMMC_EMU_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/cdev.h>
#include <linux/device.h>
#else
/* Userspace self-test harness: provide just enough to compile the pure
 * register/protocol logic (emmc_proto.c) outside the kernel. */
#include "emmc_host_shim.h"
#endif

/* ------------------------------------------------------------------------- */
/* Hardware register map (SG2002 / CV181x)                                   */
/* ------------------------------------------------------------------------- */

/*
 * The six SDIO1 signals live on the PWR-domain GPIO bank (DesignWare APB GPIO
 * instance "porte" / gpio4) at 0x05021000. Function selection for each pad is
 * in the main IOMUX block at 0x03001000. funcsel value 0 = SDIO1 controller,
 * value 3 = PWR_GPIO (plain GPIO). See cv181x_pinlist_swconfig.h /
 * cv181x_reg_fmux_gpio.h.
 */
#define EMMC_PWR_GPIO_BASE	0x05021000UL
#define EMMC_PWR_GPIO_SIZE	0x1000

/* DesignWare APB GPIO register offsets (single port A used on this bank). */
#define DWGPIO_SWPORTA_DR	0x00	/* data output			*/
#define DWGPIO_SWPORTA_DDR	0x04	/* direction: 1 = output	*/
#define DWGPIO_INTEN		0x30
#define DWGPIO_INTMASK		0x34
#define DWGPIO_INTTYPE_LEVEL	0x38
#define DWGPIO_INT_POLARITY	0x3c
#define DWGPIO_INTSTATUS	0x40
#define DWGPIO_PORTA_EOI	0x4c
#define DWGPIO_EXT_PORTA	0x50	/* live pad input value		*/

#define EMMC_PINMUX_BASE	0x03001000UL
#define EMMC_PINMUX_SIZE	0x1000
#define PINMUX_SD1_D3		0xd0
#define PINMUX_SD1_D2		0xd4
#define PINMUX_SD1_D1		0xd8
#define PINMUX_SD1_D0		0xdc
#define PINMUX_SD1_CMD		0xe0
#define PINMUX_SD1_CLK		0xe4
#define PINMUX_FUNC_MASK	0x7
#define PINMUX_FUNC_SD1		0x0	/* restore: pad -> SDIO1 controller	*/
#define PINMUX_FUNC_GPIO	0x3	/* claim:   pad -> PWR_GPIO		*/

/*
 * Pin index (bit position) of each signal within the PWR GPIO bank. From the
 * pad mux table: SD1_D3=PWR_GPIO_18 ... SD1_CLK=PWR_GPIO_23.
 */
#define PIN_D3			18
#define PIN_D2			19
#define PIN_D1			20
#define PIN_D0			21
#define PIN_CMD			22
#define PIN_CLK			23

#define BIT_D3			(1u << PIN_D3)
#define BIT_D2			(1u << PIN_D2)
#define BIT_D1			(1u << PIN_D1)
#define BIT_D0			(1u << PIN_D0)
#define BIT_CMD			(1u << PIN_CMD)
#define BIT_CLK			(1u << PIN_CLK)
#define BIT_DAT_ALL		(BIT_D0 | BIT_D1 | BIT_D2 | BIT_D3)
#define BIT_ALL			(BIT_DAT_ALL | BIT_CMD | BIT_CLK)

/* ------------------------------------------------------------------------- */
/* eMMC command set (the subset a U-Boot host actually issues)               */
/* ------------------------------------------------------------------------- */

enum emmc_cmd {
	CMD_GO_IDLE_STATE	= 0,	/* R-none: reset to idle		*/
	CMD_SEND_OP_COND	= 1,	/* R3: OCR query (eMMC, not ACMD41)	*/
	CMD_ALL_SEND_CID	= 2,	/* R2: 128-bit CID			*/
	CMD_SET_RELATIVE_ADDR	= 3,	/* R1: host assigns RCA (eMMC)		*/
	CMD_SET_DSR		= 4,	/* R-none				*/
	CMD_SWITCH		= 6,	/* R1b: write EXT_CSD byte		*/
	CMD_SELECT_CARD		= 7,	/* R1/R1b: (de)select by RCA		*/
	CMD_SEND_EXT_CSD	= 8,	/* R1 + 512B data: EXT_CSD		*/
	CMD_SEND_CSD		= 9,	/* R2: 128-bit CSD			*/
	CMD_SEND_CID		= 10,	/* R2					*/
	CMD_STOP_TRANSMISSION	= 12,	/* R1b: end open-ended transfer		*/
	CMD_SEND_STATUS		= 13,	/* R1: card status			*/
	CMD_SET_BLOCKLEN	= 16,	/* R1					*/
	CMD_READ_SINGLE_BLOCK	= 17,	/* R1 + data to host			*/
	CMD_READ_MULTIPLE_BLOCK	= 18,	/* R1 + data to host (open-ended)	*/
	CMD_SET_BLOCK_COUNT	= 23,	/* R1: predefined count for CMD18/25	*/
	CMD_WRITE_BLOCK		= 24,	/* R1 + data from host			*/
	CMD_WRITE_MULTIPLE_BLOCK = 25,	/* R1 + data from host (open-ended)	*/
};

/* CURRENT_STATE field (bits [12:9]) of the R1 card-status response. */
enum emmc_state {
	ST_IDLE = 0, ST_READY, ST_IDENT, ST_STBY,
	ST_TRAN, ST_DATA, ST_RCV, ST_PRG, ST_DIS,
};

/* OCR (operating conditions register) bits returned in R3. */
#define OCR_BUSY		0x80000000u	/* 0 = busy initialising	*/
#define OCR_ACCESS_SECTOR	0x40000000u	/* bits[30:29]=10b sector mode	*/
#define OCR_VDD_27_36		0x00ff8000u	/* 2.7-3.6V window		*/
#define OCR_VDD_170_195		0x00000080u	/* 1.70-1.95V (dual voltage)	*/

/* R1 card-status bits we set/clear. */
#define R1_READY_FOR_DATA	(1u << 8)
#define R1_CURRENT_STATE(s)	(((u32)(s) & 0xf) << 9)
#define R1_ILLEGAL_COMMAND	(1u << 22)
#define R1_COM_CRC_ERROR	(1u << 23)
#define R1_ERROR		(1u << 19)
#define R1_ADDRESS_OUT_OF_RANGE	(1u << 31)

#define EMMC_BLOCK_LEN		512

/* ------------------------------------------------------------------------- */
/* Response descriptor produced by the protocol layer for the PHY            */
/* ------------------------------------------------------------------------- */

enum emmc_resp_kind {
	RESP_NONE = 0,	/* no response (CMD0/CMD4)			*/
	RESP_R1,	/* 48-bit, idx echoed, CRC7			*/
	RESP_R1B,	/* R1 + busy on DAT0				*/
	RESP_R2,	/* 136-bit CID/CSD, CRC7 baked into payload	*/
	RESP_R3,	/* 48-bit OCR, fixed CRC field (1111111)	*/
};

enum emmc_data_dir {
	DATA_NONE = 0,
	DATA_TO_HOST,	/* device drives DAT (read)			*/
	DATA_FROM_HOST,	/* device samples DAT (write)			*/
};

struct emmc_response {
	enum emmc_resp_kind kind;
	enum emmc_data_dir  data_dir;
	u8  bits[17];		/* framed response, MSB of bits[0] first   */
	u8  nbits;		/* 48 for R1/R3, 136 for R2		   */
	u32 nblocks;		/* data phase block count (>=1 if data)	   */
	u64 block_addr;		/* starting block (already in block units) */
	bool open_ended;	/* CMD18/25: continue until CMD12	   */
	const u8 *fixed_block;	/* if set, data phase streams this 512B	   */
				/* buffer instead of the backing store	   */
				/* (used for CMD8 SEND_EXT_CSD)		   */
};

/* ------------------------------------------------------------------------- */
/* Device context                                                            */
/* ------------------------------------------------------------------------- */

struct emmc_dev {
	struct device		*dev;

	/* MMIO bases (ioremapped). */
	void __iomem		*gpio;		/* PWR GPIO bank	*/
	void __iomem		*pinmux;	/* IOMUX block		*/

	/* Cached register pointers for the hot path. */
	void __iomem		*reg_dr;	/* gpio + DR		*/
	void __iomem		*reg_ddr;	/* gpio + DDR		*/
	void __iomem		*reg_in;	/* gpio + EXT_PORTA	*/
	u32			ddr_shadow;	/* mirror of DDR	*/
	u32			dr_shadow;	/* mirror of DR		*/
	bool			pads_claimed;
	u32			pinmux_saved[6];	/* original funcsel	*/

	/* Card register models. */
	u8			cid[16];
	u8			csd[16];
	u8			ext_csd[512];
	u32			ocr;
	u16			rca;
	bool			selected;
	bool			powered_up;	/* OCR busy cleared	*/
	enum emmc_state		state;
	u32			block_len;	/* set by CMD16		*/
	bool			high_capacity;	/* sector addressing	*/
	u8			spec_vers;	/* CSD SPEC_VERS (<4 => no EXT_CSD) */

	/* Predefined block count from CMD23 (0 = none / open-ended). */
	u32			predef_blocks;

	/* Backing store (RAM image; optionally file-loaded). */
	u8			*store;
	u64			store_bytes;
	u64			capacity_blocks;

	/* RT sampling thread. */
	struct task_struct	*kthr;
	int			cpu;
	bool			run;

	/* Char device (userspace daemon / backing-store window). */
	dev_t			devt;
	struct cdev		cdev;
	struct class		*class;

	/* Stats (read via sysfs / debugfs). */
	atomic_t		cmd_count;
	atomic_t		crc_errors;
	atomic_t		resync_count;
	u32			last_cmd;
	u32			last_arg;

	spinlock_t		lock;
};

/* ------------------------------------------------------------------------- */
/* Inter-module API                                                          */
/* ------------------------------------------------------------------------- */

/* emmc_gpio.c */
int  emmc_gpio_map(struct emmc_dev *d);
void emmc_gpio_unmap(struct emmc_dev *d);
int  emmc_gpio_claim_pads(struct emmc_dev *d);
void emmc_gpio_release_pads(struct emmc_dev *d);
void emmc_gpio_idle(struct emmc_dev *d);	/* all lines input (Hi-Z)	*/

/* emmc_proto.c */
void emmc_proto_init_registers(struct emmc_dev *d);
void emmc_proto_reset(struct emmc_dev *d);
/*
 * Decode one received 48-bit command frame and produce the response/data plan.
 * Runs inside the IRQ-off critical section, so it must be allocation-free and
 * fast (only small CRC7 computations). Returns true if the frame was a valid,
 * handled command.
 */
bool emmc_proto_handle(struct emmc_dev *d, const u8 *cmd_frame,
		       struct emmc_response *resp);

/* Backing-store block access (called from the data phase). */
int  emmc_store_read(struct emmc_dev *d, u64 block, u8 *buf512);
int  emmc_store_write(struct emmc_dev *d, u64 block, const u8 *buf512);

/* emmc_phy.c - the bit layer. All run with local IRQs disabled. */
void emmc_phy_setup(struct emmc_dev *d);
/*
 * Block (with a bounded spin) until a command start bit is seen, then capture
 * the 48-bit frame into frame[6]. Returns:
 *   1  - a frame was captured (CRC validated by caller)
 *   0  - timed out with the bus idle (caller may re-enable IRQs and retry)
 *  <0  - framing/resync error
 */
int  emmc_phy_recv_command(struct emmc_dev *d, u8 *frame, u32 spin_budget);
void emmc_phy_send_response(struct emmc_dev *d, const struct emmc_response *r);
int  emmc_phy_send_data_block(struct emmc_dev *d, const u8 *buf512);
int  emmc_phy_recv_data_block(struct emmc_dev *d, u8 *buf512);

/* Shared module parameters (defined in emmc_main.c). */
extern uint emmc_capacity_mb;
extern uint emmc_clk_spin;
extern bool emmc_force_legacy;

#endif /* EMMC_EMU_H */
