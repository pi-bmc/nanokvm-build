// SPDX-License-Identifier: GPL-2.0-only
/*
 * The capture node: /dev/video0, delivering H.264/H.265/MJPEG bitstream
 * from the hardware encoder through a plain vb2 MMAP/read() queue.
 *
 * The feeder thread is where the stock pacing model lives. It waits for a
 * free vb2 buffer before it pulls anything from VPSS, so the consumer —
 * ultimately the process doing DQBUF — decides when a frame moves through
 * the encoder. If userspace stops dequeueing, the thread parks, VPSS holds
 * at most its channel depth and sheds the rest silently, and no queue
 * anywhere fills up. That is the same self-limiting loop the stock
 * firmware runs in userspace, moved behind STREAMON.
 */
#include <linux/kthread.h>
#include <linux/sizes.h>

#include "soph_v4l2.h"

#include <media/v4l2-ioctl.h>

static const u32 soph_formats[] = {
	V4L2_PIX_FMT_H264,
	V4L2_PIX_FMT_HEVC,
	V4L2_PIX_FMT_MJPEG,
};

static u32 soph_sizeimage(u32 w, u32 h)
{
	/* Mirrors the encoder's own bitstream ring sizing: the worst case
	 * is the IDR sent when a viewer joins on a screen full of text.
	 */
	return max_t(u32, w * h / 2, SZ_1M);
}

static struct soph_venc_buf *to_soph_buf(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct soph_venc_buf, vb);
}

/* ------------------------------------------------------------------ */
/* Feeder                                                              */

static struct soph_venc_buf *soph_pop_buf(struct soph_v4l2_dev *dev)
{
	struct soph_venc_buf *buf = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev->qlock, flags);
	if (!list_empty(&dev->buf_list)) {
		buf = list_first_entry(&dev->buf_list, struct soph_venc_buf,
				       list);
		list_del(&buf->list);
	}
	spin_unlock_irqrestore(&dev->qlock, flags);
	return buf;
}

static void soph_push_buf_front(struct soph_v4l2_dev *dev,
				struct soph_venc_buf *buf)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->qlock, flags);
	list_add(&buf->list, &dev->buf_list);
	spin_unlock_irqrestore(&dev->qlock, flags);
}

static int soph_feeder(void *data)
{
	struct soph_v4l2_dev *dev = data;
	struct soph_venc_buf *buf;
	int ret;

	while (!kthread_should_stop()) {
		wait_event_timeout(dev->buf_wait,
				   kthread_should_stop() ||
				   !list_empty(&dev->buf_list),
				   msecs_to_jiffies(100));
		if (kthread_should_stop())
			break;

		buf = soph_pop_buf(dev);
		if (!buf)
			continue;

		ret = soph_pipeline_encode_one(dev, &buf->vb);
		if (ret == -EAGAIN) {
			/* Nothing to encode right now. The buffer goes back
			 * to the head so ordering is preserved; pacing comes
			 * from the blocking waits inside encode_one, not
			 * from this loop.
			 */
			soph_push_buf_front(dev, buf);
			continue;
		}
		if (ret) {
			/* Fatal: hand the buffer back failed and mark the
			 * queue so userspace's next DQBUF returns an error
			 * and it knows to STREAMOFF/ON — the same rebuild
			 * cycle the stock supervisor runs.
			 */
			vb2_buffer_done(&buf->vb.vb2_buf,
					VB2_BUF_STATE_ERROR);
			vb2_queue_error(&dev->queue);
			break;
		}

		buf->vb.sequence = dev->sequence++;
		buf->vb.field = V4L2_FIELD_NONE;
		vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
	}

	/* Park until kthread_stop(), so stop_streaming's stop call never
	 * races a self-exited thread.
	 */
	while (!kthread_should_stop())
		schedule_timeout_interruptible(msecs_to_jiffies(10));
	return 0;
}

/* ------------------------------------------------------------------ */
/* vb2 ops                                                             */

static int soph_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
			    unsigned int *num_planes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct soph_v4l2_dev *dev = vb2_get_drv_priv(q);
	u32 size = dev->cap_fmt.sizeimage;

	if (*num_planes)
		return sizes[0] < size ? -EINVAL : 0;
	*num_planes = 1;
	sizes[0] = size;
	return 0;
}

static void soph_buf_queue(struct vb2_buffer *vb)
{
	struct soph_v4l2_dev *dev = vb2_get_drv_priv(vb->vb2_queue);
	struct soph_venc_buf *buf = to_soph_buf(to_vb2_v4l2_buffer(vb));
	unsigned long flags;

	spin_lock_irqsave(&dev->qlock, flags);
	list_add_tail(&buf->list, &dev->buf_list);
	spin_unlock_irqrestore(&dev->qlock, flags);
	wake_up(&dev->buf_wait);
}

static void soph_return_buffers(struct soph_v4l2_dev *dev,
				enum vb2_buffer_state state)
{
	struct soph_venc_buf *buf;

	while ((buf = soph_pop_buf(dev)))
		vb2_buffer_done(&buf->vb.vb2_buf, state);
}

static int soph_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct soph_v4l2_dev *dev = vb2_get_drv_priv(q);
	bool locked = false;
	u32 w = 0, h = 0, fps = 0;
	int ret;

	/* The bridge is authoritative for the input mode; read it fresh
	 * rather than trusting a poll that may be seconds old. An i2c
	 * failure here means the bridge MCU is wedged (a stalled register
	 * window, a firmware hang) -- the one situation the hardware reset
	 * line exists for. Pulse it and try once more.
	 */
	ret = soph_lt6911_read_signal(&dev->bridge, &w, &h, &locked);
	if (ret) {
		if (soph_pipeline_bridge_reset(dev))
			goto err_return;
		ret = soph_lt6911_read_signal(&dev->bridge, &w, &h, &locked);
		if (ret)
			goto err_return;
	}
	dev->bridge.locked = locked;
	dev->bridge.width = w;
	dev->bridge.height = h;
	if (!locked) {
		ret = -ENOLINK;
		goto err_return;
	}

	/* The one reading in bring-up that costs the bridge a measurement
	 * window (30ms with the firmware halted): once per build, exactly
	 * like stock.
	 */
	soph_lt6911_measure_fps(&dev->bridge, &fps);

	dev->cfg.in_w = w;
	dev->cfg.in_h = h;
	dev->cfg.in_fps = fps;
	dev->cfg.out_w = dev->cap_fmt.width;
	dev->cfg.out_h = dev->cap_fmt.height;
	dev->cfg.pixelformat = dev->cap_fmt.pixelformat;
	if (dev->timeperframe.numerator && dev->timeperframe.denominator)
		dev->cfg.out_fps = dev->timeperframe.denominator /
				   dev->timeperframe.numerator;
	if (!dev->cfg.out_fps)
		dev->cfg.out_fps = SOPH_DEF_FPS;
	dev->cfg.bitrate_bps = v4l2_ctrl_g_ctrl(dev->ctrl_bitrate);
	dev->cfg.gop = v4l2_ctrl_g_ctrl(dev->ctrl_gop);

	ret = video_device_pipeline_start(&dev->vdev, &dev->pipe);
	if (ret)
		goto err_return;

	ret = soph_pipeline_up(dev);
	if (ret)
		goto err_pipe;

	dev->sequence = 0;
	dev->streaming = true;

	dev->feeder = kthread_run(soph_feeder, dev, "soph-venc-feed");
	if (IS_ERR(dev->feeder)) {
		ret = PTR_ERR(dev->feeder);
		dev->feeder = NULL;
		goto err_down;
	}

	return 0;

err_down:
	dev->streaming = false;
	soph_pipeline_down(dev);
err_pipe:
	video_device_pipeline_stop(&dev->vdev);
err_return:
	soph_return_buffers(dev, VB2_BUF_STATE_QUEUED);
	return ret;
}

static void soph_stop_streaming(struct vb2_queue *q)
{
	struct soph_v4l2_dev *dev = vb2_get_drv_priv(q);

	dev->streaming = false;
	if (dev->feeder) {
		kthread_stop(dev->feeder);
		dev->feeder = NULL;
	}
	soph_pipeline_down(dev);
	video_device_pipeline_stop(&dev->vdev);
	soph_return_buffers(dev, VB2_BUF_STATE_ERROR);
}

static const struct vb2_ops soph_vb2_ops = {
	.queue_setup = soph_queue_setup,
	.buf_queue = soph_buf_queue,
	.start_streaming = soph_start_streaming,
	.stop_streaming = soph_stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

/* ------------------------------------------------------------------ */
/* ioctl ops                                                           */

static int soph_querycap(struct file *file, void *priv,
			 struct v4l2_capability *cap)
{
	strscpy(cap->driver, "soph_v4l2", sizeof(cap->driver));
	strscpy(cap->card, "CV181x HDMI capture", sizeof(cap->card));
	return 0;
}

static int soph_enum_fmt(struct file *file, void *priv,
			 struct v4l2_fmtdesc *f)
{
	if (f->index >= ARRAY_SIZE(soph_formats))
		return -EINVAL;
	f->pixelformat = soph_formats[f->index];
	return 0;
}

static void soph_fill_pix(struct v4l2_pix_format *pix)
{
	pix->width = clamp_t(u32, pix->width, SOPH_MIN_W, SOPH_MAX_W) & ~1U;
	pix->height = clamp_t(u32, pix->height, SOPH_MIN_H, SOPH_MAX_H) & ~1U;
	pix->field = V4L2_FIELD_NONE;
	pix->colorspace = V4L2_COLORSPACE_SRGB;
	pix->bytesperline = 0;
	pix->sizeimage = soph_sizeimage(pix->width, pix->height);
}

static int soph_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format *pix = &f->fmt.pix;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(soph_formats); i++)
		if (soph_formats[i] == pix->pixelformat)
			break;
	if (i == ARRAY_SIZE(soph_formats))
		pix->pixelformat = V4L2_PIX_FMT_H264;

	soph_fill_pix(pix);
	return 0;
}

static int soph_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	f->fmt.pix = dev->cap_fmt;
	return 0;
}

static int soph_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);
	int ret;

	if (vb2_is_busy(&dev->queue))
		return -EBUSY;

	ret = soph_try_fmt(file, priv, f);
	if (ret)
		return ret;

	dev->cap_fmt = f->fmt.pix;

	/* Keep the graph consistent: the capture format is the scaler's
	 * source pad format.
	 */
	dev->scaler.fmt[SOPH_PAD_SOURCE].width = f->fmt.pix.width;
	dev->scaler.fmt[SOPH_PAD_SOURCE].height = f->fmt.pix.height;
	return 0;
}

static int soph_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	unsigned int i;

	if (fsize->index)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(soph_formats); i++)
		if (soph_formats[i] == fsize->pixel_format)
			break;
	if (i == ARRAY_SIZE(soph_formats))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise.min_width = SOPH_MIN_W;
	fsize->stepwise.max_width = SOPH_MAX_W;
	fsize->stepwise.step_width = 2;
	fsize->stepwise.min_height = SOPH_MIN_H;
	fsize->stepwise.max_height = SOPH_MAX_H;
	fsize->stepwise.step_height = 2;
	return 0;
}

static int soph_g_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *parm)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	parm->parm.capture.timeperframe = dev->timeperframe;
	return 0;
}

static int soph_s_parm(struct file *file, void *priv,
		       struct v4l2_streamparm *parm)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);
	struct v4l2_fract *tpf = &parm->parm.capture.timeperframe;

	if (parm->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	if (vb2_is_busy(&dev->queue))
		return -EBUSY;

	if (!tpf->numerator || !tpf->denominator) {
		tpf->numerator = 1;
		tpf->denominator = SOPH_DEF_FPS;
	}
	/* Clamp to what the encoder RC accepts. */
	if (tpf->denominator / tpf->numerator > 60) {
		tpf->numerator = 1;
		tpf->denominator = 60;
	}
	dev->timeperframe = *tpf;
	parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	return 0;
}

static int soph_enum_input(struct file *file, void *priv,
			   struct v4l2_input *inp)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	if (inp->index)
		return -EINVAL;
	strscpy(inp->name, "HDMI", sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;
	inp->capabilities = V4L2_IN_CAP_DV_TIMINGS;
	inp->status = dev->bridge.locked ? 0 : V4L2_IN_ST_NO_SIGNAL;
	return 0;
}

static int soph_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int soph_s_input(struct file *file, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int soph_query_dv_timings(struct file *file, void *priv,
				 struct v4l2_dv_timings *timings)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	return v4l2_subdev_call(&dev->bridge.sd, pad, query_dv_timings, 0,
				timings);
}

static int soph_g_dv_timings(struct file *file, void *priv,
			     struct v4l2_dv_timings *timings)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	return v4l2_subdev_call(&dev->bridge.sd, pad, g_dv_timings, 0,
				timings);
}

/* The bridge negotiates the mode with the source itself; S_DV_TIMINGS is
 * accepted for API completeness but the measured timings stay
 * authoritative — STREAMON re-reads them.
 */
static int soph_s_dv_timings(struct file *file, void *priv,
			     struct v4l2_dv_timings *timings)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	if (vb2_is_busy(&dev->queue))
		return -EBUSY;
	return 0;
}

static int soph_dv_timings_cap(struct file *file, void *priv,
			       struct v4l2_dv_timings_cap *cap)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	return v4l2_subdev_call(&dev->bridge.sd, pad, dv_timings_cap, cap);
}

/* EDID on the video node forwards to the bridge, so the app never needs to
 * discover subdev nodes. pad is forced to the bridge's single source pad.
 */
static int soph_g_edid(struct file *file, void *priv, struct v4l2_edid *edid)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	edid->pad = 0;
	return v4l2_subdev_call(&dev->bridge.sd, pad, get_edid, edid);
}

static int soph_s_edid(struct file *file, void *priv, struct v4l2_edid *edid)
{
	struct soph_v4l2_dev *dev = video_drvdata(file);

	edid->pad = 0;
	return v4l2_subdev_call(&dev->bridge.sd, pad, set_edid, edid);
}

static int soph_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ioctl_ops soph_ioctl_ops = {
	.vidioc_querycap = soph_querycap,
	.vidioc_enum_fmt_vid_cap = soph_enum_fmt,
	.vidioc_g_fmt_vid_cap = soph_g_fmt,
	.vidioc_try_fmt_vid_cap = soph_try_fmt,
	.vidioc_s_fmt_vid_cap = soph_s_fmt,
	.vidioc_enum_framesizes = soph_enum_framesizes,
	.vidioc_g_parm = soph_g_parm,
	.vidioc_s_parm = soph_s_parm,
	.vidioc_enum_input = soph_enum_input,
	.vidioc_g_input = soph_g_input,
	.vidioc_s_input = soph_s_input,
	.vidioc_query_dv_timings = soph_query_dv_timings,
	.vidioc_g_dv_timings = soph_g_dv_timings,
	.vidioc_s_dv_timings = soph_s_dv_timings,
	.vidioc_dv_timings_cap = soph_dv_timings_cap,
	.vidioc_g_edid = soph_g_edid,
	.vidioc_s_edid = soph_s_edid,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_expbuf = vb2_ioctl_expbuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
	.vidioc_prepare_buf = vb2_ioctl_prepare_buf,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_subscribe_event = soph_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static const struct v4l2_file_operations soph_fops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
	.release = vb2_fop_release,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.unlocked_ioctl = video_ioctl2,
};

/* ------------------------------------------------------------------ */
/* Controls                                                            */

static int soph_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct soph_v4l2_dev *dev =
		container_of(ctrl->handler, struct soph_v4l2_dev, ctrls);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		soph_pipeline_request_idr(dev);
		return 0;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		/* Stored in the control; read at the next STREAMON. The
		 * vendor encoder cannot re-negotiate RC on a live channel
		 * without a reset, and a KVM changes these only around a
		 * rebuild anyway.
		 */
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct v4l2_ctrl_ops soph_ctrl_ops = {
	.s_ctrl = soph_s_ctrl,
};

/* ------------------------------------------------------------------ */

int soph_venc_node_register(struct soph_v4l2_dev *dev)
{
	struct video_device *vdev = &dev->vdev;
	struct vb2_queue *q = &dev->queue;
	int ret;

	mutex_init(&dev->vlock);
	spin_lock_init(&dev->qlock);
	INIT_LIST_HEAD(&dev->buf_list);
	init_waitqueue_head(&dev->buf_wait);

	dev->cap_fmt.pixelformat = V4L2_PIX_FMT_H264;
	dev->cap_fmt.width = SOPH_MAX_W;
	dev->cap_fmt.height = SOPH_MAX_H;
	soph_fill_pix(&dev->cap_fmt);
	dev->timeperframe.numerator = 1;
	dev->timeperframe.denominator = SOPH_DEF_FPS;

	v4l2_ctrl_handler_init(&dev->ctrls, 3);
	dev->ctrl_bitrate = v4l2_ctrl_new_std(&dev->ctrls, &soph_ctrl_ops,
					      V4L2_CID_MPEG_VIDEO_BITRATE,
					      100000, 100000000, 1000,
					      4000000);
	dev->ctrl_gop = v4l2_ctrl_new_std(&dev->ctrls, &soph_ctrl_ops,
					  V4L2_CID_MPEG_VIDEO_GOP_SIZE,
					  1, 600, 1, 120);
	v4l2_ctrl_new_std(&dev->ctrls, &soph_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 0, 0, 0, 0);
	if (dev->ctrls.error) {
		ret = dev->ctrls.error;
		goto err_ctrls;
	}

	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_READ | VB2_DMABUF;
	q->drv_priv = dev;
	q->buf_struct_size = sizeof(struct soph_venc_buf);
	q->ops = &soph_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 2;
	q->lock = &dev->vlock;
	ret = vb2_queue_init(q);
	if (ret)
		goto err_ctrls;

	vdev->fops = &soph_fops;
	vdev->ioctl_ops = &soph_ioctl_ops;
	vdev->v4l2_dev = &dev->v4l2_dev;
	vdev->queue = q;
	vdev->lock = &dev->vlock;
	vdev->release = video_device_release_empty;
	vdev->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_READWRITE;
	vdev->ctrl_handler = &dev->ctrls;
	strscpy(vdev->name, SOPH_ENT_VENC, sizeof(vdev->name));
	video_set_drvdata(vdev, dev);

	dev->vdev_pad.flags = MEDIA_PAD_FL_SINK;
	ret = media_entity_pads_init(&vdev->entity, 1, &dev->vdev_pad);
	if (ret)
		goto err_ctrls;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_entity;

	return 0;

err_entity:
	media_entity_cleanup(&vdev->entity);
err_ctrls:
	v4l2_ctrl_handler_free(&dev->ctrls);
	return ret;
}

void soph_venc_node_unregister(struct soph_v4l2_dev *dev)
{
	if (!video_is_registered(&dev->vdev))
		return;
	video_unregister_device(&dev->vdev);
	media_entity_cleanup(&dev->vdev.entity);
	v4l2_ctrl_handler_free(&dev->ctrls);
}
