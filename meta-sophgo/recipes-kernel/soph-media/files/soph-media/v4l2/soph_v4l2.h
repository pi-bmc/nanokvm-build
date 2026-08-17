/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * soph_v4l2 — V4L2 Media Controller front-end for the CV181x capture
 * pipeline (LT6911 HDMI bridge → CSI-2 RX → VI → VPSS → VENC).
 *
 * See DESIGN.md. The vendor drivers own the hardware; this module owns the
 * userspace contract: /dev/media0, four subdevs, one bitstream capture node.
 */
#ifndef __SOPH_V4L2_H__
#define __SOPH_V4L2_H__

#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <media/media-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-dv-timings.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

/* One of everything: one bridge, one receiver, one ISP, one scaler, one
 * encoder channel. A KVM captures exactly one input into exactly one stream,
 * so none of these is a parameter.
 */
#define SOPH_ENT_LT6911		"lt6911"
#define SOPH_ENT_CSI		"cv181x-csi"
#define SOPH_ENT_ISP		"cv181x-isp"
#define SOPH_ENT_SCALER		"cv181x-scaler"
#define SOPH_ENT_VENC		"cv181x-venc"

/* Every interior subdev has one sink and one source pad. */
#define SOPH_PAD_SINK		0
#define SOPH_PAD_SOURCE		1

/* Bounds are the bridge's: the LT6911 tops out at 1080p60 on this board. */
#define SOPH_MIN_W		640
#define SOPH_MIN_H		480
#define SOPH_MAX_W		1920
#define SOPH_MAX_H		1080
#define SOPH_DEF_FPS		30

/*
 * Everything STREAMON hands to the vendor drivers, gathered from the
 * negotiated graph state. in_* is what the bridge measured; out_* is the
 * scaler source pad / capture format; the rest come from controls.
 */
struct soph_pipe_cfg {
	u32 in_w, in_h;
	u32 in_fps;		/* measured; 0 = unknown, treat as out_fps */
	u32 out_w, out_h;
	u32 out_fps;
	u32 pixelformat;	/* V4L2_PIX_FMT_H264 / HEVC / MJPEG */
	u32 bitrate_bps;
	u32 gop;
};

/* An encoded-frame destination in flight between vb2 and the feeder. */
struct soph_venc_buf {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

struct soph_v4l2_dev;

struct soph_lt6911 {
	struct v4l2_subdev sd;
	struct media_pad pad;		/* source only */
	struct i2c_client *client;
	struct soph_v4l2_dev *dev;
	struct mutex lock;		/* i2c register window + cached state */
	struct delayed_work poll_work;
	/* Last measured input mode; locked=false means no usable signal. */
	bool locked;
	u32 width, height;
};

struct soph_subdev {
	struct v4l2_subdev sd;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt fmt[2];
};

struct soph_v4l2_dev {
	struct platform_device *pdev;
	struct media_device mdev;
	struct v4l2_device v4l2_dev;

	struct soph_lt6911 bridge;
	struct soph_subdev csi;
	struct soph_subdev isp;
	struct soph_subdev scaler;

	/* Capture node */
	struct video_device vdev;
	struct media_pad vdev_pad;
	struct media_pipeline pipe;
	struct mutex vlock;		/* ioctls + queue */
	struct vb2_queue queue;
	struct v4l2_ctrl_handler ctrls;
	struct v4l2_ctrl *ctrl_bitrate;
	struct v4l2_ctrl *ctrl_gop;

	struct v4l2_pix_format cap_fmt;
	struct v4l2_fract timeperframe;

	/* Feeder */
	struct task_struct *feeder;
	spinlock_t qlock;		/* buf_list */
	struct list_head buf_list;	/* empty vb2 buffers awaiting frames */
	wait_queue_head_t buf_wait;
	bool streaming;
	u32 sequence;

	struct soph_pipe_cfg cfg;
	bool pipe_up;
};

/* soph_pipeline.c — the ordered vendor bring-up/teardown and the per-frame
 * pull → encode → copy step. Everything vendor-specific lives behind these.
 * encode_one returns 0 with the buffer filled, -EAGAIN when there is nothing
 * to encode right now (source idle, encoder warming up), or a fatal error.
 */
int soph_pipeline_up(struct soph_v4l2_dev *dev);
void soph_pipeline_down(struct soph_v4l2_dev *dev);
int soph_pipeline_encode_one(struct soph_v4l2_dev *dev,
			     struct vb2_v4l2_buffer *vbuf);
void soph_pipeline_request_idr(struct soph_v4l2_dev *dev);
/* Pulse the LT6911's hardware reset (PWR_GPIO1 via the cif driver) and wait
 * out the MCU's firmware boot. -EPERM when bridge_reset=never.
 */
int soph_pipeline_bridge_reset(struct soph_v4l2_dev *dev);

/* soph_lt6911.c — bridge subdev plus raw accessors the pipeline uses. */
int soph_lt6911_register(struct soph_v4l2_dev *dev);
void soph_lt6911_unregister(struct soph_v4l2_dev *dev);
int soph_lt6911_read_signal(struct soph_lt6911 *br, u32 *width, u32 *height,
			    bool *locked);
int soph_lt6911_measure_fps(struct soph_lt6911 *br, u32 *fps);
int soph_lt6911_tx(struct soph_lt6911 *br, bool on);

/* soph_subdevs.c — the three interior entities. */
int soph_csi_register(struct soph_v4l2_dev *dev);
int soph_isp_register(struct soph_v4l2_dev *dev);
int soph_scaler_register(struct soph_v4l2_dev *dev);

/* soph_venc_node.c — the capture node and feeder thread. */
int soph_venc_node_register(struct soph_v4l2_dev *dev);
void soph_venc_node_unregister(struct soph_v4l2_dev *dev);

#endif /* __SOPH_V4L2_H__ */
