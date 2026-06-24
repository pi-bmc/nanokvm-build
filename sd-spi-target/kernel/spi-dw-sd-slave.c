// SPDX-License-Identifier: GPL-2.0
/*
 * spi-dw-sd-slave.c - DesignWare APB SSI in SLAVE mode, emulating an SD card.
 *
 * The stock spi-dw driver is master-only. This driver instead programs a
 * DW_apb_ssi instance as a bus *slave* and runs the SD "SPI mode" card protocol
 * (sd_spi_target.c) over its RX/TX FIFOs, so an external SPI-mode SD host (3D
 * printer, MCU SD library, ...) sees the board as a microSD card.
 *
 * Backing store is a vmalloc'd RAM disk, staged from user space through a misc
 * char device (/dev/<name>): `dd if=image.img of=/dev/sdslave0` to load,
 * `dd if=/dev/sdslave0 of=out.img` to read back.
 *
 * STATUS: compile-tested target is the cvitek 5.10 tree; NOT yet hardware
 * validated. Two things must be confirmed against the SG2002 TRM before this
 * can work on silicon:
 *   1. SPI2 (DW_apb_ssi @ 0x041A0000) must be SYNTHESISED slave-capable. The
 *      generic SLV_OE/SRL CTRLR0 bits exist, but a given instance can be built
 *      master-only. If master-only, no driver can make it a slave.
 *   2. The host's SPI clock must be within the instance's max slave frequency
 *      and within this ISR's servicing latency (use the FIFO depth as budget;
 *      move to DMA for higher rates).
 *
 * The u-boot pinmux (cvi_board_init.c) must route the pads to SPI2, and the
 * stock master driver must NOT bind this node (use the compatible below).
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/spinlock.h>

#include "sd_spi_target.h"

/* ---- DW_apb_ssi registers (legacy snps,dw-apb-ssi layout) -------------- */
#define DW_SPI_CTRLR0	0x00
#define DW_SPI_CTRLR1	0x04
#define DW_SPI_SSIENR	0x08
#define DW_SPI_SER	0x10
#define DW_SPI_BAUDR	0x14
#define DW_SPI_TXFTLR	0x18
#define DW_SPI_RXFTLR	0x1c
#define DW_SPI_TXFLR	0x20
#define DW_SPI_RXFLR	0x24
#define DW_SPI_SR	0x28
#define DW_SPI_IMR	0x2c
#define DW_SPI_ISR	0x30
#define DW_SPI_ICR	0x48
#define DW_SPI_DR	0x60

/* CTRLR0 fields (legacy layout) */
#define CTRLR0_DFS_8	0x07		/* data frame size = 8 bits (N-1) */
#define CTRLR0_FRF_SPI	(0x0 << 4)
#define CTRLR0_SCPH	(1 << 6)
#define CTRLR0_SCPOL	(1 << 7)
#define CTRLR0_TMOD_TR	(0x0 << 8)	/* transmit & receive */
#define CTRLR0_SLV_OE	(1 << 10)	/* 1 = slave output DISABLED */

/* SR */
#define SR_BUSY		(1 << 0)
#define SR_TFNF		(1 << 1)	/* tx FIFO not full */
#define SR_RFNE		(1 << 3)	/* rx FIFO not empty */

/* interrupts (IMR/ISR/RISR) */
#define INT_TXEI	(1 << 0)
#define INT_RXOI	(1 << 3)
#define INT_RXFI	(1 << 4)

struct dw_sd_slave {
	struct device		*dev;
	void __iomem		*regs;
	int			irq;
	struct clk		*clk;

	sd_spi_target_t		sd;		/* protocol engine */
	spinlock_t		lock;		/* guards sd + disk */

	u8			*disk;		/* vmalloc RAM backing store */
	u32			disk_blocks;

	struct miscdevice	misc;
	char			misc_name[24];

	bool			mode3;		/* SPI CPOL=CPHA=1 */
};

/* ---- backing store callbacks (called from the ISR under ->lock) -------- */
static int sd_disk_read(void *u, uint32_t blk, uint8_t buf[SD_BLOCK_SIZE])
{
	struct dw_sd_slave *d = u;

	if (blk >= d->disk_blocks)
		return -1;
	memcpy(buf, d->disk + (size_t)blk * SD_BLOCK_SIZE, SD_BLOCK_SIZE);
	return 0;
}

static int sd_disk_write(void *u, uint32_t blk, const uint8_t buf[SD_BLOCK_SIZE])
{
	struct dw_sd_slave *d = u;

	if (blk >= d->disk_blocks)
		return -1;
	memcpy(d->disk + (size_t)blk * SD_BLOCK_SIZE, buf, SD_BLOCK_SIZE);
	return 0;
}

/* ---- hardware ---------------------------------------------------------- */
static inline u32 sr(struct dw_sd_slave *d, u32 off) { return readl_relaxed(d->regs + off); }
static inline void sw(struct dw_sd_slave *d, u32 off, u32 v) { writel_relaxed(v, d->regs + off); }

/*
 * Re-sync the controller to a known state. SSIENR 1->0->1 is the DW soft-reset:
 * it clears both FIFOs (config in CTRLR0 persists), after which we queue exactly
 * ONE idle byte. Keeping a single byte in the TX FIFO minimises how long the SD
 * host waits for a response (the Ncr/Nac window) - we react one byte behind, so
 * any extra queued idle bytes would push the reply further out. The ISR refills
 * one byte per received byte; at the low SPI rates of SD-SPI hosts that keeps
 * up without underrun. (Soft-reset-to-known-state resync pattern is borrowed
 * from drivers/spi/spi-slave-mt27xx.c.)
 */
static void dw_sd_slave_resync(struct dw_sd_slave *d)
{
	sw(d, DW_SPI_SSIENR, 0);
	sw(d, DW_SPI_SSIENR, 1);
	sw(d, DW_SPI_DR, 0xFF);
}

static void dw_sd_slave_hw_init(struct dw_sd_slave *d)
{
	u32 ctrlr0;

	/* Disable to program; clears FIFOs. */
	sw(d, DW_SPI_SSIENR, 0);

	ctrlr0 = CTRLR0_DFS_8 | CTRLR0_FRF_SPI | CTRLR0_TMOD_TR;
	if (d->mode3)
		ctrlr0 |= CTRLR0_SCPH | CTRLR0_SCPOL;
	/* SLV_OE = 0 -> slave transmit output ENABLED (we drive MISO). */
	sw(d, DW_SPI_CTRLR0, ctrlr0);

	/*
	 * NOTE: on this legacy instance master/slave selection is a synthesis
	 * parameter, not a register bit. This driver assumes the SPI2 instance
	 * is slave-capable (TRM-confirm). For DWC_SSI/KeemBay variants the
	 * master bit (CTRLR0[31]) would need clearing here instead.
	 */

	sw(d, DW_SPI_CTRLR1, 0);
	sw(d, DW_SPI_RXFTLR, 0);		/* IRQ when >=1 byte received */
	sw(d, DW_SPI_TXFTLR, 0);
	sw(d, DW_SPI_SER, 0);

	sw(d, DW_SPI_IMR, 0);
	dw_sd_slave_resync(d);			/* enable + prime one idle byte */
	sw(d, DW_SPI_IMR, INT_RXFI | INT_RXOI);
}

static irqreturn_t dw_sd_slave_isr(int irq, void *dev_id)
{
	struct dw_sd_slave *d = dev_id;
	u32 isr = sr(d, DW_SPI_ISR);

	if (!isr)
		return IRQ_NONE;

	spin_lock(&d->lock);

	if (isr & INT_RXOI) {
		/*
		 * Overrun: we fell behind the host clock (or the host deselected
		 * CS mid-frame). Soft-reset to a known state; the engine itself
		 * re-syncs on the next command-frame start bit (bits [7:6]==01).
		 */
		(void)sr(d, DW_SPI_ICR);
		dw_sd_slave_resync(d);
		dev_warn_ratelimited(d->dev, "rx overrun, resync\n");
		spin_unlock(&d->lock);
		return IRQ_HANDLED;
	}

	/*
	 * For every received byte, advance the protocol engine and push its
	 * response to the TX FIFO. Hardware shifted out the previously-queued
	 * byte during this same transfer, so our reply lands one byte later -
	 * harmless: SD-SPI tolerates a variable response gap (Ncr/Nac).
	 */
	while (sr(d, DW_SPI_SR) & SR_RFNE) {
		u8 mosi = sr(d, DW_SPI_DR) & 0xFF;
		u8 miso = sd_spi_target_step(&d->sd, mosi);

		/* Wait briefly for TX space; FIFO should not be full here. */
		if (sr(d, DW_SPI_SR) & SR_TFNF)
			sw(d, DW_SPI_DR, miso);
		else
			dev_warn_ratelimited(d->dev, "tx fifo full\n");
	}

	spin_unlock(&d->lock);
	return IRQ_HANDLED;
}

/* ---- misc char device: stage / read back the RAM disk ------------------ */
static struct dw_sd_slave *misc_to_slave(struct file *f)
{
	return container_of(f->private_data, struct dw_sd_slave, misc);
}

static ssize_t sd_disk_chr_read(struct file *f, char __user *ubuf, size_t len, loff_t *ppos)
{
	struct dw_sd_slave *d = misc_to_slave(f);
	size_t total = (size_t)d->disk_blocks * SD_BLOCK_SIZE;
	unsigned long flags;
	u8 *tmp;

	if (*ppos >= total)
		return 0;
	if (len > total - *ppos)
		len = total - *ppos;
	tmp = vmalloc(len);
	if (!tmp)
		return -ENOMEM;

	spin_lock_irqsave(&d->lock, flags);
	memcpy(tmp, d->disk + *ppos, len);
	spin_unlock_irqrestore(&d->lock, flags);

	if (copy_to_user(ubuf, tmp, len)) {
		vfree(tmp);
		return -EFAULT;
	}
	vfree(tmp);
	*ppos += len;
	return len;
}

static ssize_t sd_disk_chr_write(struct file *f, const char __user *ubuf, size_t len, loff_t *ppos)
{
	struct dw_sd_slave *d = misc_to_slave(f);
	size_t total = (size_t)d->disk_blocks * SD_BLOCK_SIZE;
	unsigned long flags;
	u8 *tmp;

	if (*ppos >= total)
		return -ENOSPC;
	if (len > total - *ppos)
		len = total - *ppos;
	tmp = vmalloc(len);
	if (!tmp)
		return -ENOMEM;
	if (copy_from_user(tmp, ubuf, len)) {
		vfree(tmp);
		return -EFAULT;
	}

	spin_lock_irqsave(&d->lock, flags);
	memcpy(d->disk + *ppos, tmp, len);
	spin_unlock_irqrestore(&d->lock, flags);

	vfree(tmp);
	*ppos += len;
	return len;
}

static const struct file_operations sd_disk_fops = {
	.owner		= THIS_MODULE,
	.read		= sd_disk_chr_read,
	.write		= sd_disk_chr_write,
	.llseek		= default_llseek,
};

/* ---- probe / remove ---------------------------------------------------- */
static u32 size_mb = 16;
module_param(size_mb, uint, 0444);
MODULE_PARM_DESC(size_mb, "Emulated SD card size in MiB (default 16)");

static bool mode3;
module_param(mode3, bool, 0444);
MODULE_PARM_DESC(mode3, "Use SPI mode 3 (CPOL=CPHA=1); default mode 0");

static int dw_sd_slave_probe(struct platform_device *pdev)
{
	struct dw_sd_slave *d;
	struct resource *res;
	sd_blockstore_t store;
	int ret;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->dev = &pdev->dev;
	d->mode3 = mode3;
	spin_lock_init(&d->lock);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	d->regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(d->regs))
		return PTR_ERR(d->regs);

	d->irq = platform_get_irq(pdev, 0);
	if (d->irq < 0)
		return d->irq;

	d->clk = devm_clk_get_optional(&pdev->dev, NULL);
	if (IS_ERR(d->clk))
		return PTR_ERR(d->clk);
	ret = clk_prepare_enable(d->clk);
	if (ret)
		return ret;

	d->disk_blocks = (size_mb ? size_mb : 16) * (1024u * 1024u / SD_BLOCK_SIZE);
	d->disk = vmalloc((size_t)d->disk_blocks * SD_BLOCK_SIZE);
	if (!d->disk) {
		ret = -ENOMEM;
		goto err_clk;
	}
	memset(d->disk, 0, (size_t)d->disk_blocks * SD_BLOCK_SIZE);

	store.read = sd_disk_read;
	store.write = sd_disk_write;
	store.block_count = d->disk_blocks;
	store.user = d;
	sd_spi_target_init(&d->sd, &store);

	platform_set_drvdata(pdev, d);

	ret = devm_request_irq(&pdev->dev, d->irq, dw_sd_slave_isr,
			       IRQF_SHARED, dev_name(&pdev->dev), d);
	if (ret)
		goto err_disk;

	dw_sd_slave_hw_init(d);

	scnprintf(d->misc_name, sizeof(d->misc_name), "sdslave%d", pdev->id < 0 ? 0 : pdev->id);
	d->misc.minor = MISC_DYNAMIC_MINOR;
	d->misc.name = d->misc_name;
	d->misc.fops = &sd_disk_fops;
	ret = misc_register(&d->misc);
	if (ret)
		goto err_disk;

	dev_info(d->dev, "SD-SPI slave ready: /dev/%s, %u MiB, mode %d\n",
		 d->misc_name, size_mb, d->mode3 ? 3 : 0);
	return 0;

err_disk:
	vfree(d->disk);
err_clk:
	clk_disable_unprepare(d->clk);
	return ret;
}

static int dw_sd_slave_remove(struct platform_device *pdev)
{
	struct dw_sd_slave *d = platform_get_drvdata(pdev);

	sw(d, DW_SPI_IMR, 0);
	sw(d, DW_SPI_SSIENR, 0);
	misc_deregister(&d->misc);
	vfree(d->disk);
	clk_disable_unprepare(d->clk);
	return 0;
}

static const struct of_device_id dw_sd_slave_of_match[] = {
	{ .compatible = "cvitek,dw-ssi-sd-slave" },
	{ }
};
MODULE_DEVICE_TABLE(of, dw_sd_slave_of_match);

static struct platform_driver dw_sd_slave_driver = {
	.probe	= dw_sd_slave_probe,
	.remove	= dw_sd_slave_remove,
	.driver	= {
		.name		= "dw-ssi-sd-slave",
		.of_match_table	= dw_sd_slave_of_match,
	},
};
module_platform_driver(dw_sd_slave_driver);

MODULE_DESCRIPTION("DesignWare APB SSI slave-mode SD card emulation");
MODULE_LICENSE("GPL");
