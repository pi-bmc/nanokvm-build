// SPDX-License-Identifier: GPL-2.0-only
/*
 * The three interior entities of the CV181x capture graph: CSI-2 receiver,
 * ISP front-end (VI) and scaler (VPSS). Each is a real subdev with real pad
 * state — formats negotiated here are what STREAMON hands to the vendor
 * drivers — but none of them implements s_stream: the vendor bring-up
 * interleaves the entities (VI's device is enabled before the receiver is
 * configured, the bridge transmitter starts between the two), so the whole
 * ordered sequence lives in soph_pipeline.c and runs from the capture node's
 * start_streaming. The subdevs' job is topology and format negotiation.
 */
#include "soph_v4l2.h"

/* The one bus format the front of the pipe speaks: the LT6911 emits YUV422
 * over CSI-2 and VI consumes it as UYVY. The scaler's source pad carries the
 * same code but its own size — NV21 conversion happens inside VPSS on the way
 * to the encoder and is not visible on the media bus.
 */
#define SOPH_MBUS_CODE	MEDIA_BUS_FMT_UYVY8_1X16

static void soph_fill_fmt(struct v4l2_mbus_framefmt *fmt, u32 w, u32 h)
{
	fmt->code = SOPH_MBUS_CODE;
	fmt->width = w;
	fmt->height = h;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SRGB;
	fmt->ycbcr_enc = V4L2_YCBCR_ENC_601;
	fmt->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	fmt->xfer_func = V4L2_XFER_FUNC_SRGB;
}

static struct soph_subdev *to_soph_subdev(struct v4l2_subdev *sd)
{
	return container_of(sd, struct soph_subdev, sd);
}

static int soph_sd_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;
	code->code = SOPH_MBUS_CODE;
	return 0;
}

/* These subdevs keep their pad formats in the driver struct rather than in
 * core-managed subdev state; TRY requests are answered from the same place
 * (a TRY here differs from ACTIVE only in not being stored), which keeps
 * the implementation independent of whether the fh carries a try-state.
 */
static int soph_sd_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *format)
{
	struct soph_subdev *ssd = to_soph_subdev(sd);

	if (format->pad >= ARRAY_SIZE(ssd->fmt))
		return -EINVAL;

	format->format = ssd->fmt[format->pad];
	return 0;
}

/* Whether this subdev may emit a size different from what it was fed.
 * Only the scaler resizes; everything else propagates sink → source.
 */
static bool soph_sd_can_scale(struct v4l2_subdev *sd)
{
	return !strcmp(sd->name, SOPH_ENT_SCALER);
}

static int soph_sd_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_state *state,
			   struct v4l2_subdev_format *format)
{
	struct soph_subdev *ssd = to_soph_subdev(sd);
	struct v4l2_mbus_framefmt *fmt = &format->format;

	if (format->pad >= ARRAY_SIZE(ssd->fmt))
		return -EINVAL;

	fmt->width = clamp_t(u32, fmt->width, SOPH_MIN_W, SOPH_MAX_W);
	fmt->height = clamp_t(u32, fmt->height, SOPH_MIN_H, SOPH_MAX_H);
	/* The scaler writes NV21: even sizes only. */
	fmt->width &= ~1U;
	fmt->height &= ~1U;
	soph_fill_fmt(fmt, fmt->width, fmt->height);

	/* A source pad follows its sink unless this entity scales. */
	if (format->pad == SOPH_PAD_SOURCE && !soph_sd_can_scale(sd)) {
		fmt->width = ssd->fmt[SOPH_PAD_SINK].width;
		fmt->height = ssd->fmt[SOPH_PAD_SINK].height;
	}

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	ssd->fmt[format->pad] = *fmt;

	/* Propagate through non-scaling entities so a walk of the graph
	 * always sees a consistent chain.
	 */
	if (format->pad == SOPH_PAD_SINK && !soph_sd_can_scale(sd))
		ssd->fmt[SOPH_PAD_SOURCE] = *fmt;
	return 0;
}

static const struct v4l2_subdev_pad_ops soph_sd_pad_ops = {
	.enum_mbus_code = soph_sd_enum_mbus_code,
	.get_fmt = soph_sd_get_fmt,
	.set_fmt = soph_sd_set_fmt,
	.link_validate = v4l2_subdev_link_validate_default,
};

static const struct v4l2_subdev_ops soph_sd_ops = {
	.pad = &soph_sd_pad_ops,
};

static const struct media_entity_operations soph_sd_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int soph_subdev_register(struct soph_v4l2_dev *dev,
				struct soph_subdev *ssd, const char *name,
				u32 function)
{
	struct v4l2_subdev *sd = &ssd->sd;
	int ret;

	v4l2_subdev_init(sd, &soph_sd_ops);
	sd->owner = THIS_MODULE;
	sd->dev = &dev->pdev->dev;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strscpy(sd->name, name, sizeof(sd->name));
	sd->entity.function = function;
	sd->entity.ops = &soph_sd_media_ops;

	ssd->pads[SOPH_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	ssd->pads[SOPH_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;
	soph_fill_fmt(&ssd->fmt[SOPH_PAD_SINK], SOPH_MAX_W, SOPH_MAX_H);
	soph_fill_fmt(&ssd->fmt[SOPH_PAD_SOURCE], SOPH_MAX_W, SOPH_MAX_H);

	ret = media_entity_pads_init(&sd->entity, 2, ssd->pads);
	if (ret)
		return ret;

	ret = v4l2_device_register_subdev(&dev->v4l2_dev, sd);
	if (ret)
		media_entity_cleanup(&sd->entity);
	return ret;
}

int soph_csi_register(struct soph_v4l2_dev *dev)
{
	return soph_subdev_register(dev, &dev->csi, SOPH_ENT_CSI,
				    MEDIA_ENT_F_VID_IF_BRIDGE);
}

int soph_isp_register(struct soph_v4l2_dev *dev)
{
	return soph_subdev_register(dev, &dev->isp, SOPH_ENT_ISP,
				    MEDIA_ENT_F_PROC_VIDEO_PIXEL_FORMATTER);
}

int soph_scaler_register(struct soph_v4l2_dev *dev)
{
	return soph_subdev_register(dev, &dev->scaler, SOPH_ENT_SCALER,
				    MEDIA_ENT_F_PROC_VIDEO_SCALER);
}
