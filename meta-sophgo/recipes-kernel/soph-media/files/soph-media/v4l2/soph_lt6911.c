// SPDX-License-Identifier: GPL-2.0-only
/*
 * LT6911UXC HDMI → MIPI CSI-2 bridge subdev.
 *
 * The part on this board answers the UXC signature (bank 0x81 regs
 * 0x00..0x02 = 17 04 83), which pins three register-map facts the C-variant
 * gets wrong: the TX control is bank 0x81 reg 0x1D, lock is bank 0x86 reg
 * 0xA3, and the CSI H-active is already in pixels (no doubling).
 *
 * The central hazard is the register window. Bank 0x80 reg 0xEE gates all
 * register access, and while it is open the bridge's own firmware is not
 * driving the part — its CSI transmitter idles while the timing registers
 * keep returning their last latched measurement. Every accessor here opens
 * the window, does its business, and closes it again; and the poller backs
 * off from 500ms to 3s once the pipeline is streaming, because each poll
 * costs a transmitter stall.
 *
 * Detection is polled. There is no hotplug GPIO or IRQ wired on this board;
 * the lock byte is the only truth.
 */
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

#include "soph_v4l2.h"

#define LT6911_ADDR		0x2b

#define LT_REG_BANK		0xFF

#define LT_BANK_CTL		0x80
#define LT_CTL_ACCESS		0xEE	/* 1 = window open */
#define LT_CTL_WATCHDOG		0x10	/* 0 = stopped while window open */

#define LT_BANK_TX		0x81
#define LT_TX_CTRL		0x1D
#define LT_TX_ON		0xFB
#define LT_TX_OFF		0x00

#define LT_BANK_LOCK		0x86
#define LT_LOCK			0xA3
#define LT_LOCK_STABLE		0x55

#define LT_BANK_GEOM		0x85
#define LT_GEOM_VACTIVE		0xF0	/* 2 bytes, big-endian, lines */
#define LT_GEOM_HACTIVE		0xEA	/* 2 bytes, big-endian, pixels */
#define LT_CLK_TRIGGER		0x40
#define LT_CLK_MEASURE		0x21
#define LT_CLK_RESULT		0x48	/* 3 bytes; byte 0 low nibble only */

#define LT_BANK_TIMING		0xD4
#define LT_TIMING_HTOTAL	0x26	/* 2 bytes, big-endian, pixels */
#define LT_TIMING_VTOTAL	0x32	/* 2 bytes, big-endian, lines */

/* Poll cadence, from the field-tuned userspace implementation: 500ms finds a
 * new source promptly; 3s while streaming keeps the transmitter stalls rare
 * enough that the receiver holds lock.
 */
#define LT_POLL_IDLE_MS		500
#define LT_POLL_STREAM_MS	3000

/* The LT6911UXC has an INT pin its firmware raises on hotplug and mode
 * changes, but nothing proves it is routed to a SoC GPIO on this board —
 * Sipeed's own firmware polls, and their DTS declares no interrupt. If the
 * routing is ever confirmed, set this to the GPIO number and the interrupt
 * simply kicks the same poll state machine immediately; polling stays on
 * as the safety net either way.
 */
static int irq_gpio = -1;
module_param(irq_gpio, int, 0444);
MODULE_PARM_DESC(irq_gpio,
	"SoC GPIO the bridge INT pin is wired to (-1 = poll only)");

static bool tx_start = true;
module_param(tx_start, bool, 0444);
MODULE_PARM_DESC(tx_start,
	"Write the bridge TX-on register at stream start (default on). "
	"Sipeed's own bring-up writes no bridge register at all, so a board "
	"whose transmitter free-runs can turn this off to save the window "
	"open immediately before VI starts looking for data.");

static struct soph_lt6911 *sd_to_lt6911(struct v4l2_subdev *sd)
{
	return container_of(sd, struct soph_lt6911, sd);
}

/* i2c helpers. The vendor protocol is write-then-read as two transactions,
 * not a repeated-start combined transfer; the part is known to tolerate the
 * former and has not been proven with the latter, so match the vendor.
 */
static int lt_write(struct soph_lt6911 *br, u8 reg, u8 val)
{
	u8 buf[2] = { reg, val };
	int ret;

	ret = i2c_master_send(br->client, buf, 2);
	if (ret < 0)
		return ret;
	return ret == 2 ? 0 : -EIO;
}

static int lt_read(struct soph_lt6911 *br, u8 reg, u8 *buf, int len)
{
	int ret;

	ret = i2c_master_send(br->client, &reg, 1);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;
	ret = i2c_master_recv(br->client, buf, len);
	if (ret < 0)
		return ret;
	return ret == len ? 0 : -EIO;
}

/* Bank select is register 0xFF in every bank. Nothing reports the current
 * bank, so it is always re-selected, never assumed.
 */
static int lt_bank(struct soph_lt6911 *br, u8 bank)
{
	return lt_write(br, LT_REG_BANK, bank);
}

/* The window also stops the bridge's internal watchdog: a running watchdog
 * resets the firmware periodically, and a reset landing mid-read returns a
 * torn measurement.
 */
static int lt_window_open(struct soph_lt6911 *br)
{
	int ret;

	ret = lt_bank(br, LT_BANK_CTL);
	if (ret)
		return ret;
	ret = lt_write(br, LT_CTL_ACCESS, 0x01);
	if (ret)
		return ret;
	return lt_write(br, LT_CTL_WATCHDOG, 0x00);
}

static void lt_window_close(struct soph_lt6911 *br)
{
	/* Best effort: a close that fails leaves the firmware halted, and
	 * there is nothing better to do about it than say so.
	 */
	if (lt_bank(br, LT_BANK_CTL) || lt_write(br, LT_CTL_ACCESS, 0x00))
		dev_warn(&br->client->dev,
			 "lt6911: failed to close register window; bridge firmware may be halted\n");
}

/* Read lock + geometry under one window. Lock is read first and out of its
 * own bank, because the geometry registers hold their last measurement after
 * the source goes away.
 */
int soph_lt6911_read_signal(struct soph_lt6911 *br, u32 *width, u32 *height,
			    bool *locked)
{
	u8 lock, v[2], h[2];
	int ret;

	mutex_lock(&br->lock);
	ret = lt_window_open(br);
	if (ret)
		goto out_unlock;

	ret = lt_bank(br, LT_BANK_LOCK);
	if (ret)
		goto out_close;
	ret = lt_read(br, LT_LOCK, &lock, 1);
	if (ret)
		goto out_close;

	if (lock != LT_LOCK_STABLE) {
		*locked = false;
		*width = 0;
		*height = 0;
		goto out_close;
	}

	ret = lt_bank(br, LT_BANK_GEOM);
	if (ret)
		goto out_close;
	ret = lt_read(br, LT_GEOM_VACTIVE, v, 2);
	if (ret)
		goto out_close;
	ret = lt_read(br, LT_GEOM_HACTIVE, h, 2);
	if (ret)
		goto out_close;

	*width = (u32)h[0] << 8 | h[1];
	*height = (u32)v[0] << 8 | v[1];
	*locked = (*width != 0 && *height != 0);

out_close:
	lt_window_close(br);
out_unlock:
	mutex_unlock(&br->lock);
	return ret;
}

/* Measure the source frame rate. Expensive: the window is held open for the
 * full 30ms the clock counter needs, so this runs once per pipeline build,
 * never from the poller.
 *
 * The clock register returns half the pixel clock in kHz; the 2000 below is
 * the halving and the kHz together. Totals, not actives — blanking is part
 * of the frame period. Zero means "could not measure", never "0 fps": the
 * caller falls back to its output rate, which makes the rate converter a
 * no-op rather than a guess.
 */
int soph_lt6911_measure_fps(struct soph_lt6911 *br, u32 *fps)
{
	u8 ht[2], vt[2], c[3];
	u32 htotal, vtotal, half_khz;
	int ret;

	*fps = 0;

	mutex_lock(&br->lock);
	ret = lt_window_open(br);
	if (ret)
		goto out_unlock;

	ret = lt_bank(br, LT_BANK_TIMING);
	if (ret)
		goto out_close;
	ret = lt_read(br, LT_TIMING_HTOTAL, ht, 2);
	if (ret)
		goto out_close;
	ret = lt_read(br, LT_TIMING_VTOTAL, vt, 2);
	if (ret)
		goto out_close;

	htotal = (u32)ht[0] << 8 | ht[1];
	vtotal = (u32)vt[0] << 8 | vt[1];
	if (!htotal || !vtotal)
		goto out_close;

	ret = lt_bank(br, LT_BANK_GEOM);
	if (ret)
		goto out_close;
	ret = lt_write(br, LT_CLK_TRIGGER, LT_CLK_MEASURE);
	if (ret)
		goto out_close;
	msleep(30);
	ret = lt_read(br, LT_CLK_RESULT, c, 3);
	if (ret)
		goto out_close;

	half_khz = (u32)(c[0] & 0x0F) << 16 | (u32)c[1] << 8 | c[2];
	if (half_khz)
		*fps = half_khz * 2000 / (htotal * vtotal);

out_close:
	lt_window_close(br);
out_unlock:
	mutex_unlock(&br->lock);
	return ret;
}

/* Transmitter on/off. Note the TX register is not in the window's bank, so
 * the bank is re-selected after opening.
 */
int soph_lt6911_tx(struct soph_lt6911 *br, bool on)
{
	int ret;

	if (on && !tx_start)
		return 0;

	mutex_lock(&br->lock);
	ret = lt_window_open(br);
	if (ret)
		goto out_unlock;
	ret = lt_bank(br, LT_BANK_TX);
	if (!ret)
		ret = lt_write(br, LT_TX_CTRL, on ? LT_TX_ON : LT_TX_OFF);
	lt_window_close(br);
out_unlock:
	mutex_unlock(&br->lock);
	return ret;
}

/* ------------------------------------------------------------------ */
/* Poller: the only hotplug/mode-change detection this board has.      */

static void lt6911_poll(struct work_struct *work)
{
	struct soph_lt6911 *br =
		container_of(to_delayed_work(work), struct soph_lt6911,
			     poll_work);
	static const struct v4l2_event ev = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};
	bool locked = false;
	u32 w = 0, h = 0;
	unsigned int delay;

	if (!soph_lt6911_read_signal(br, &w, &h, &locked)) {
		if (locked != br->locked || w != br->width || h != br->height) {
			br->locked = locked;
			br->width = w;
			br->height = h;
			v4l2_subdev_notify_event(&br->sd, &ev);
		}
	}

	delay = br->dev->streaming ? LT_POLL_STREAM_MS : LT_POLL_IDLE_MS;
	schedule_delayed_work(&br->poll_work, msecs_to_jiffies(delay));
}

static irqreturn_t lt6911_irq(int irq, void *data)
{
	struct soph_lt6911 *br = data;

	mod_delayed_work(system_wq, &br->poll_work, 0);
	return IRQ_HANDLED;
}

static void lt6911_setup_irq(struct soph_lt6911 *br)
{
	int irq;

	if (irq_gpio < 0)
		return;
	if (gpio_request_one(irq_gpio, GPIOF_IN, "lt6911-int")) {
		dev_warn(&br->client->dev,
			 "lt6911: gpio %d unavailable, polling only\n",
			 irq_gpio);
		return;
	}
	irq = gpio_to_irq(irq_gpio);
	if (irq < 0 ||
	    request_irq(irq, lt6911_irq,
			IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			"lt6911", br)) {
		dev_warn(&br->client->dev,
			 "lt6911: no irq for gpio %d, polling only\n",
			 irq_gpio);
		gpio_free(irq_gpio);
		irq_gpio = -1;
	}
}

static void lt6911_teardown_irq(struct soph_lt6911 *br)
{
	if (irq_gpio < 0)
		return;
	free_irq(gpio_to_irq(irq_gpio), br);
	gpio_free(irq_gpio);
}

/* ------------------------------------------------------------------ */
/* Subdev ops                                                          */

static int lt6911_s_stream(struct v4l2_subdev *sd, int enable)
{
	return soph_lt6911_tx(sd_to_lt6911(sd), enable);
}

static int lt6911_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct soph_lt6911 *br = sd_to_lt6911(sd);

	*status = br->locked ? 0 : V4L2_IN_ST_NO_SIGNAL;
	return 0;
}

/* Build DV timings from what the bridge measures. The bridge reports active
 * geometry and (on demand) frame rate but not blanking, so the pixelclock
 * here is active-area rate — good enough for mode negotiation, which keys
 * on width/height.
 */
static void lt6911_fill_timings(struct soph_lt6911 *br,
				struct v4l2_dv_timings *timings)
{
	u32 fps = 0;

	memset(timings, 0, sizeof(*timings));
	timings->type = V4L2_DV_BT_656_1120;
	timings->bt.width = br->width;
	timings->bt.height = br->height;
	timings->bt.interlaced = V4L2_DV_PROGRESSIVE;

	soph_lt6911_measure_fps(br, &fps);
	if (!fps)
		fps = SOPH_DEF_FPS;
	timings->bt.pixelclock = (u64)br->width * br->height * fps;
}

static int lt6911_query_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
				   struct v4l2_dv_timings *timings)
{
	struct soph_lt6911 *br = sd_to_lt6911(sd);
	bool locked = false;
	u32 w = 0, h = 0;
	int ret;

	ret = soph_lt6911_read_signal(br, &w, &h, &locked);
	if (ret)
		return ret;
	br->locked = locked;
	br->width = w;
	br->height = h;
	if (!locked)
		return -ENOLINK;

	lt6911_fill_timings(br, timings);
	return 0;
}

static int lt6911_g_dv_timings(struct v4l2_subdev *sd, unsigned int pad,
			       struct v4l2_dv_timings *timings)
{
	struct soph_lt6911 *br = sd_to_lt6911(sd);

	if (!br->locked)
		return -ENOLINK;
	lt6911_fill_timings(br, timings);
	return 0;
}

static const struct v4l2_dv_timings_cap lt6911_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	.bt = {
		.min_width = SOPH_MIN_W,
		.max_width = SOPH_MAX_W,
		.min_height = SOPH_MIN_H,
		.max_height = SOPH_MAX_H,
		.min_pixelclock = 25000000,
		.max_pixelclock = 150000000,
		.standards = V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_DMT,
		.capabilities = V4L2_DV_BT_CAP_PROGRESSIVE,
	},
};

static int lt6911_dv_timings_cap(struct v4l2_subdev *sd,
				 struct v4l2_dv_timings_cap *cap)
{
	*cap = lt6911_timings_cap;
	return 0;
}

static int lt6911_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_UYVY8_1X16;
	return 0;
}

static int lt6911_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *format)
{
	struct soph_lt6911 *br = sd_to_lt6911(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;

	memset(fmt, 0, sizeof(*fmt));
	fmt->code = MEDIA_BUS_FMT_UYVY8_1X16;
	fmt->width = br->locked ? br->width : SOPH_MAX_W;
	fmt->height = br->locked ? br->height : SOPH_MAX_H;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	return 0;
}

static int lt6911_subscribe_event(struct v4l2_subdev *sd, struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops lt6911_core_ops = {
	.subscribe_event = lt6911_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops lt6911_video_ops = {
	.s_stream = lt6911_s_stream,
	.g_input_status = lt6911_g_input_status,
};

static const struct v4l2_subdev_pad_ops lt6911_pad_ops = {
	.enum_mbus_code = lt6911_enum_mbus_code,
	.get_fmt = lt6911_get_fmt,
	.query_dv_timings = lt6911_query_dv_timings,
	.g_dv_timings = lt6911_g_dv_timings,
	.dv_timings_cap = lt6911_dv_timings_cap,
};

static const struct v4l2_subdev_ops lt6911_ops = {
	.core = &lt6911_core_ops,
	.video = &lt6911_video_ops,
	.pad = &lt6911_pad_ops,
};

int soph_lt6911_register(struct soph_v4l2_dev *dev)
{
	struct soph_lt6911 *br = &dev->bridge;
	struct i2c_adapter *adap;
	struct v4l2_subdev *sd = &br->sd;
	int ret;

	br->dev = dev;
	mutex_init(&br->lock);
	INIT_DELAYED_WORK(&br->poll_work, lt6911_poll);

	/* The board DT deliberately declares no child on i2c4 — the bridge
	 * was driven from userspace via /dev/i2c-4. A dummy client keeps
	 * that arrangement honest: this module owns the address while
	 * loaded, without inventing a DT binding for a driver split that
	 * is transitional (see DESIGN.md).
	 */
	adap = i2c_get_adapter(4);
	if (!adap)
		return -EPROBE_DEFER;
	br->client = i2c_new_dummy_device(adap, LT6911_ADDR);
	i2c_put_adapter(adap);
	if (IS_ERR(br->client))
		return PTR_ERR(br->client);

	v4l2_subdev_init(sd, &lt6911_ops);
	sd->owner = THIS_MODULE;
	sd->dev = &br->client->dev;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;
	snprintf(sd->name, sizeof(sd->name), SOPH_ENT_LT6911 " %d-%04x",
		 i2c_adapter_id(br->client->adapter), br->client->addr);
	sd->entity.function = MEDIA_ENT_F_DV_DECODER;

	br->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &br->pad);
	if (ret)
		goto err_client;

	ret = v4l2_device_register_subdev(&dev->v4l2_dev, sd);
	if (ret)
		goto err_entity;

	lt6911_setup_irq(br);
	schedule_delayed_work(&br->poll_work,
			      msecs_to_jiffies(LT_POLL_IDLE_MS));
	return 0;

err_entity:
	media_entity_cleanup(&sd->entity);
err_client:
	i2c_unregister_device(br->client);
	br->client = NULL;
	return ret;
}

void soph_lt6911_unregister(struct soph_v4l2_dev *dev)
{
	struct soph_lt6911 *br = &dev->bridge;

	if (!br->client)
		return;
	lt6911_teardown_irq(br);
	cancel_delayed_work_sync(&br->poll_work);
	v4l2_device_unregister_subdev(&br->sd);
	media_entity_cleanup(&br->sd.entity);
	i2c_unregister_device(br->client);
	br->client = NULL;
}
