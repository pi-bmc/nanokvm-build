// SPDX-License-Identifier: GPL-2.0
/*
 * emmc_main.c - NanoKVM eMMC device emulator: platform driver, real-time
 * sampling thread, and the char-device backing-store window.
 *
 * Lifecycle:
 *   probe  -> allocate backing store, map GPIO/pinmux, claim the SDIO1 pads,
 *             build the card registers, create /dev/emmc-emu0, start the RT
 *             sampler kthread.
 *   thread -> the service loop: with local IRQs disabled across each command
 *             transaction, capture a command, ask emmc_proto to decide the
 *             response, then drive the response (and any data block) on the bus.
 *   remove -> tear all of that down in reverse.
 *
 * Single-core reality (see README): this part has one CPU. While the host is
 * actively clocking us the sampler must spin hard; when the bus is idle it
 * backs off with usleep so the rest of the system can run. The RT FIFO policy
 * keeps the scheduler from preempting us mid-frame.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/file.h>
#include <linux/version.h>

#include "emmc_emu.h"
#include "emmc_crc.h"
#include "emmc_uapi.h"

/* ------------------------------------------------------------------------- */
/* Module parameters                                                         */
/* ------------------------------------------------------------------------- */

/*
 * Default kept small on purpose: the backing store is RAM (vzalloc), and the
 * board has only ~256 MiB DDR shared with the multimedia stack. A UEFI
 * variable store needs single-digit MiB. Raise with care; legacy mode caps the
 * advertised size at 1024 MiB regardless.
 */
uint emmc_capacity_mb = 16;
module_param(emmc_capacity_mb, uint, 0444);
MODULE_PARM_DESC(emmc_capacity_mb,
		 "Emulated eMMC size in MiB (RAM-backed; default 16, legacy cap 1024)");

uint emmc_clk_spin = 200000;
module_param(emmc_clk_spin, uint, 0644);
MODULE_PARM_DESC(emmc_clk_spin,
		 "Per-edge MMIO sample budget before declaring the clock stalled");

bool emmc_force_legacy = true;
module_param(emmc_force_legacy, bool, 0444);
MODULE_PARM_DESC(emmc_force_legacy,
		 "Force SPEC_VERS<4 (no EXT_CSD) - the robust detection path");

static int emmc_cpu = 0;
module_param(emmc_cpu, int, 0444);
MODULE_PARM_DESC(emmc_cpu, "CPU to pin the sampling thread to");

static char *emmc_image;
module_param(emmc_image, charp, 0444);
MODULE_PARM_DESC(emmc_image,
		 "Optional path to a file image to preload into the backing store");

/* Idle backoff: after this many empty capture attempts, sleep briefly. */
#define IDLE_SPIN_THRESHOLD	64

static struct emmc_dev *g_dev;	/* single instance */
static struct class *emmc_class;

/* ------------------------------------------------------------------------- */
/* Backing store                                                             */
/* ------------------------------------------------------------------------- */

static int store_alloc(struct emmc_dev *d)
{
	u64 want = (u64)emmc_capacity_mb << 20;

	if (emmc_force_legacy && want > (1ULL << 30))
		want = (1ULL << 30);		/* legacy CSD cap */
	if (want < (1ULL << 20))
		want = (1ULL << 20);
	/* Round down to a whole number of 256 KiB CSD capacity units. */
	want &= ~((1ULL << 18) - 1);

	d->store = vzalloc(want);
	if (!d->store)
		return -ENOMEM;
	d->store_bytes = want;
	return 0;
}

static void store_preload(struct emmc_dev *d)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;

	if (!emmc_image)
		return;

	f = filp_open(emmc_image, O_RDONLY, 0);
	if (IS_ERR(f)) {
		dev_warn(d->dev, "image %s: open failed (%ld)\n",
			 emmc_image, PTR_ERR(f));
		return;
	}
	n = kernel_read(f, d->store, d->store_bytes, &pos);
	filp_close(f, NULL);
	dev_info(d->dev, "preloaded %zd bytes from %s\n", n, emmc_image);
}

/* ------------------------------------------------------------------------- */
/* The real-time sampling thread                                             */
/* ------------------------------------------------------------------------- */

static void run_data_to_host(struct emmc_dev *d, struct emmc_response *r,
			     u8 *xfer)
{
	u32 i, n = r->open_ended ? 0xffffffff : r->nblocks;

	for (i = 0; i < n; i++) {
		if (r->fixed_block)
			memcpy(xfer, r->fixed_block, EMMC_BLOCK_LEN);
		else
			emmc_store_read(d, r->block_addr + i, xfer);

		if (emmc_phy_send_data_block(d, xfer))
			break;		/* host stopped clocking */
		if (r->fixed_block)
			break;		/* single fixed block (EXT_CSD) */
	}
	d->state = ST_TRAN;
}

static void run_data_from_host(struct emmc_dev *d, struct emmc_response *r,
			       u8 *xfer)
{
	u32 i, n = r->open_ended ? 0xffffffff : r->nblocks;

	for (i = 0; i < n; i++) {
		if (emmc_phy_recv_data_block(d, xfer))
			break;
		emmc_store_write(d, r->block_addr + i, xfer);
	}
	d->state = ST_TRAN;
}

static int emmc_thread(void *data)
{
	struct emmc_dev *d = data;
	u8 frame[6];
	u8 *xfer;
	u32 idle = 0;

	xfer = kmalloc(EMMC_BLOCK_LEN, GFP_KERNEL);
	if (!xfer)
		return -ENOMEM;

	/* Module-friendly RT wrapper (raw sched_setscheduler isn't exported). */
	sched_set_fifo(current);
	dev_info(d->dev, "sampler running on CPU%d (RT FIFO)\n", d->cpu);

	while (!kthread_should_stop() && d->run) {
		struct emmc_response resp;
		unsigned long flags;
		int got;

		local_irq_save(flags);
		got = emmc_phy_recv_command(d, frame, emmc_clk_spin);
		if (got == 1) {
			u8 crc_calc = emmc_crc7(frame, 5);
			u8 crc_recv = frame[5] >> 1;

			if (crc_calc != crc_recv) {
				atomic_inc(&d->crc_errors);
				local_irq_restore(flags);
				continue;
			}

			emmc_proto_handle(d, frame, &resp);
			if (resp.kind != RESP_NONE)
				emmc_phy_send_response(d, &resp);

			if (resp.data_dir == DATA_TO_HOST)
				run_data_to_host(d, &resp, xfer);
			else if (resp.data_dir == DATA_FROM_HOST)
				run_data_from_host(d, &resp, xfer);

			local_irq_restore(flags);
			atomic_inc(&d->cmd_count);
			idle = 0;
			continue;
		}
		local_irq_restore(flags);

		/* Bus idle: yield so the rest of the (single-core) system runs. */
		if (++idle >= IDLE_SPIN_THRESHOLD) {
			usleep_range(50, 150);
			idle = IDLE_SPIN_THRESHOLD;
		} else {
			cond_resched();
		}
	}

	kfree(xfer);
	dev_info(d->dev, "sampler stopped\n");
	return 0;
}

/* ------------------------------------------------------------------------- */
/* Char device: backing-store window + control                               */
/* ------------------------------------------------------------------------- */

static ssize_t emmc_cdev_read(struct file *f, char __user *ubuf, size_t len,
			      loff_t *off)
{
	struct emmc_dev *d = f->private_data;

	if (*off >= d->store_bytes)
		return 0;
	if (*off + len > d->store_bytes)
		len = d->store_bytes - *off;
	if (copy_to_user(ubuf, d->store + *off, len))
		return -EFAULT;
	*off += len;
	return len;
}

static ssize_t emmc_cdev_write(struct file *f, const char __user *ubuf,
			       size_t len, loff_t *off)
{
	struct emmc_dev *d = f->private_data;

	if (*off >= d->store_bytes)
		return -ENOSPC;
	if (*off + len > d->store_bytes)
		len = d->store_bytes - *off;
	if (copy_from_user(d->store + *off, ubuf, len))
		return -EFAULT;
	*off += len;
	return len;
}

static long emmc_cdev_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	struct emmc_dev *d = f->private_data;

	switch (cmd) {
	case EMMC_IOC_GET_INFO: {
		struct emmc_info info = {
			.capacity_bytes = d->store_bytes,
			.capacity_blocks = d->capacity_blocks,
			.last_cmd = d->last_cmd,
			.last_arg = d->last_arg,
			.cmd_count = atomic_read(&d->cmd_count),
			.crc_errors = atomic_read(&d->crc_errors),
			.rca = d->rca,
			.state = d->state,
			.high_capacity = d->high_capacity,
			.spec_vers = d->spec_vers,
		};
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	case EMMC_IOC_SYNC:
		/* RAM-backed; nothing to flush, but provided for the daemon. */
		return 0;
	}
	return -ENOTTY;
}

static int emmc_cdev_mmap(struct file *f, struct vm_area_struct *vma)
{
	struct emmc_dev *d = f->private_data;
	unsigned long len = vma->vm_end - vma->vm_start;

	if (len > d->store_bytes)
		return -EINVAL;
	return remap_vmalloc_range(vma, d->store, vma->vm_pgoff);
}

static int emmc_cdev_open(struct inode *ino, struct file *f)
{
	f->private_data = container_of(ino->i_cdev, struct emmc_dev, cdev);
	return 0;
}

static const struct file_operations emmc_fops = {
	.owner		= THIS_MODULE,
	.open		= emmc_cdev_open,
	.read		= emmc_cdev_read,
	.write		= emmc_cdev_write,
	.unlocked_ioctl	= emmc_cdev_ioctl,
	.mmap		= emmc_cdev_mmap,
	.llseek		= default_llseek,
};

static int cdev_setup(struct emmc_dev *d)
{
	struct device *dev;
	int ret;

	ret = alloc_chrdev_region(&d->devt, 0, 1, "emmc-emu");
	if (ret)
		return ret;

	cdev_init(&d->cdev, &emmc_fops);
	d->cdev.owner = THIS_MODULE;
	ret = cdev_add(&d->cdev, d->devt, 1);
	if (ret)
		goto unregister;

	dev = device_create(emmc_class, d->dev, d->devt, d, "emmc-emu0");
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto del_cdev;
	}
	return 0;

del_cdev:
	cdev_del(&d->cdev);
unregister:
	unregister_chrdev_region(d->devt, 1);
	return ret;
}

static void cdev_teardown(struct emmc_dev *d)
{
	device_destroy(emmc_class, d->devt);
	cdev_del(&d->cdev);
	unregister_chrdev_region(d->devt, 1);
}

/* ------------------------------------------------------------------------- */
/* Platform driver                                                           */
/* ------------------------------------------------------------------------- */

static int emmc_probe(struct platform_device *pdev)
{
	struct emmc_dev *d;
	int ret;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	d->dev = &pdev->dev;
	d->cpu = emmc_cpu;
	d->run = true;
	spin_lock_init(&d->lock);
	atomic_set(&d->cmd_count, 0);
	atomic_set(&d->crc_errors, 0);
	atomic_set(&d->resync_count, 0);
	platform_set_drvdata(pdev, d);
	g_dev = d;

	ret = store_alloc(d);
	if (ret)
		return ret;
	store_preload(d);

	ret = emmc_gpio_map(d);
	if (ret)
		goto free_store;

	emmc_proto_init_registers(d);
	emmc_proto_reset(d);

	ret = emmc_gpio_claim_pads(d);
	if (ret)
		goto unmap;
	emmc_phy_setup(d);

	ret = cdev_setup(d);
	if (ret)
		goto release_pads;

	d->kthr = kthread_create(emmc_thread, d, "emmc-emu");
	if (IS_ERR(d->kthr)) {
		ret = PTR_ERR(d->kthr);
		goto teardown_cdev;
	}
	kthread_bind(d->kthr, d->cpu);
	wake_up_process(d->kthr);

	dev_info(d->dev,
		 "eMMC emulator up: %llu MiB, %s mode, /dev/emmc-emu0\n",
		 d->store_bytes >> 20,
		 emmc_force_legacy ? "legacy(SPEC_VERS<4)" : "v4+EXT_CSD");
	return 0;

teardown_cdev:
	cdev_teardown(d);
release_pads:
	emmc_gpio_release_pads(d);
unmap:
	emmc_gpio_unmap(d);
free_store:
	vfree(d->store);
	g_dev = NULL;
	return ret;
}

static int emmc_remove(struct platform_device *pdev)
{
	struct emmc_dev *d = platform_get_drvdata(pdev);

	d->run = false;
	if (d->kthr)
		kthread_stop(d->kthr);

	cdev_teardown(d);
	emmc_gpio_release_pads(d);
	emmc_gpio_unmap(d);
	vfree(d->store);
	g_dev = NULL;
	return 0;
}

static const struct of_device_id emmc_of_match[] = {
	{ .compatible = "nanokvm,emmc-emu" },
	{ }
};
MODULE_DEVICE_TABLE(of, emmc_of_match);

static struct platform_driver emmc_driver = {
	.probe	= emmc_probe,
	.remove	= emmc_remove,
	.driver	= {
		.name		= "emmc-emu",
		.of_match_table	= emmc_of_match,
	},
};

/*
 * Self-instantiated platform device so the module works with a bare insmod and
 * no device-tree node. A platform_device named "emmc-emu" binds to the driver
 * by name. If you instead add a "nanokvm,emmc-emu" DT node, set
 * emmc_self_device=0 to avoid a duplicate instance.
 *
 * NOTE: the wifisd@4320000 SDIO1 *host* controller is disabled in the device
 * tree by the linux-sophgo bbappend (and the WiFi stack is removed from the
 * image), so nothing else drives these pads. See README.md.
 */
static bool emmc_self_device = true;
module_param(emmc_self_device, bool, 0444);
MODULE_PARM_DESC(emmc_self_device,
		 "Register a platform device automatically (set 0 if using DT)");

static struct platform_device *emmc_self_pdev;

static int __init emmc_init(void)
{
	int ret;

	emmc_class = class_create(THIS_MODULE, "emmc-emu");
	if (IS_ERR(emmc_class))
		return PTR_ERR(emmc_class);

	ret = platform_driver_register(&emmc_driver);
	if (ret) {
		class_destroy(emmc_class);
		return ret;
	}

	if (emmc_self_device) {
		emmc_self_pdev = platform_device_register_simple("emmc-emu",
								 -1, NULL, 0);
		if (IS_ERR(emmc_self_pdev)) {
			ret = PTR_ERR(emmc_self_pdev);
			emmc_self_pdev = NULL;
			platform_driver_unregister(&emmc_driver);
			class_destroy(emmc_class);
			return ret;
		}
	}
	return 0;
}

static void __exit emmc_exit(void)
{
	if (emmc_self_pdev)
		platform_device_unregister(emmc_self_pdev);
	platform_driver_unregister(&emmc_driver);
	class_destroy(emmc_class);
}

module_init(emmc_init);
module_exit(emmc_exit);

MODULE_AUTHOR("NanoKVM");
MODULE_DESCRIPTION("Software eMMC device (card) emulator over SDIO1 GPIO");
MODULE_LICENSE("GPL v2");
