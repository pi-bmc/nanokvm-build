// SPDX-License-Identifier: GPL-2.0-only
/*
 * Module glue: the media device, the graph, and event routing.
 *
 * The graph is wired here with IMMUTABLE|ENABLED links because the hardware
 * has exactly one path — the media controller topology is documentation and
 * format negotiation, not routing. See DESIGN.md for why this is not (yet)
 * an OF-graph of independent drivers.
 */
#include <linux/module.h>

#include "soph_v4l2.h"

static struct soph_v4l2_dev *soph_dev;
static struct platform_device *soph_pdev;

/* Subdev events bubble up here; the source-change event matters to the
 * process holding /dev/video0, so mirror it onto the video node too.
 */
static void soph_notify(struct v4l2_subdev *sd, unsigned int notification,
			void *arg)
{
	struct soph_v4l2_dev *dev =
		container_of(sd->v4l2_dev, struct soph_v4l2_dev, v4l2_dev);

	if (notification == V4L2_DEVICE_NOTIFY_EVENT &&
	    video_is_registered(&dev->vdev))
		v4l2_event_queue(&dev->vdev, arg);
}

static int soph_create_links(struct soph_v4l2_dev *dev)
{
	const u32 flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
	int ret;

	ret = media_create_pad_link(&dev->bridge.sd.entity, 0,
				    &dev->csi.sd.entity, SOPH_PAD_SINK,
				    flags);
	if (ret)
		return ret;
	ret = media_create_pad_link(&dev->csi.sd.entity, SOPH_PAD_SOURCE,
				    &dev->isp.sd.entity, SOPH_PAD_SINK,
				    flags);
	if (ret)
		return ret;
	ret = media_create_pad_link(&dev->isp.sd.entity, SOPH_PAD_SOURCE,
				    &dev->scaler.sd.entity, SOPH_PAD_SINK,
				    flags);
	if (ret)
		return ret;
	return media_create_pad_link(&dev->scaler.sd.entity, SOPH_PAD_SOURCE,
				     &dev->vdev.entity, 0, flags);
}

static int __init soph_v4l2_init(void)
{
	struct soph_v4l2_dev *dev;
	int ret;

	/* The vendor drivers this fronts are platform devices probed from
	 * DT; this module is pure glue with no hardware of its own, so it
	 * carries its own platform device the way vimc/vivid do.
	 */
	soph_pdev = platform_device_register_simple("cv181x-v4l2", -1, NULL,
						    0);
	if (IS_ERR(soph_pdev))
		return PTR_ERR(soph_pdev);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err_pdev;
	}
	soph_dev = dev;
	dev->pdev = soph_pdev;

	dev->mdev.dev = &soph_pdev->dev;
	strscpy(dev->mdev.model, "CV181x capture", sizeof(dev->mdev.model));
	media_device_init(&dev->mdev);

	dev->v4l2_dev.mdev = &dev->mdev;
	dev->v4l2_dev.notify = soph_notify;
	ret = v4l2_device_register(&soph_pdev->dev, &dev->v4l2_dev);
	if (ret)
		goto err_mdev;

	ret = soph_lt6911_register(dev);
	if (ret)
		goto err_v4l2;
	ret = soph_csi_register(dev);
	if (ret)
		goto err_subdevs;
	ret = soph_isp_register(dev);
	if (ret)
		goto err_subdevs;
	ret = soph_scaler_register(dev);
	if (ret)
		goto err_subdevs;
	ret = soph_venc_node_register(dev);
	if (ret)
		goto err_subdevs;

	ret = soph_create_links(dev);
	if (ret)
		goto err_node;

	/* Subdev device nodes (/dev/v4l-subdev*) appear here. */
	ret = v4l2_device_register_subdev_nodes(&dev->v4l2_dev);
	if (ret)
		goto err_node;

	ret = media_device_register(&dev->mdev);
	if (ret)
		goto err_node;

	dev_info(&soph_pdev->dev,
		 "cv181x v4l2 front-end up: %s -> %s -> %s -> %s -> %s\n",
		 SOPH_ENT_LT6911, SOPH_ENT_CSI, SOPH_ENT_ISP,
		 SOPH_ENT_SCALER, SOPH_ENT_VENC);
	return 0;

err_node:
	soph_venc_node_unregister(dev);
err_subdevs:
	soph_lt6911_unregister(dev);
err_v4l2:
	/* Interior subdevs are unregistered by v4l2_device_unregister. */
	v4l2_device_unregister(&dev->v4l2_dev);
err_mdev:
	media_device_cleanup(&dev->mdev);
	kfree(dev);
	soph_dev = NULL;
err_pdev:
	platform_device_unregister(soph_pdev);
	return ret;
}

static void __exit soph_v4l2_exit(void)
{
	struct soph_v4l2_dev *dev = soph_dev;

	if (!dev)
		return;

	media_device_unregister(&dev->mdev);
	soph_venc_node_unregister(dev);
	soph_lt6911_unregister(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	media_device_cleanup(&dev->mdev);
	kfree(dev);
	soph_dev = NULL;
	platform_device_unregister(soph_pdev);
}

module_init(soph_v4l2_init);
module_exit(soph_v4l2_exit);

MODULE_DESCRIPTION("V4L2 media-controller front-end for the CV181x capture pipeline");
MODULE_AUTHOR("NanoKVM");
MODULE_LICENSE("GPL");
