// SPDX-License-Identifier: GPL-2.0-only
/*
 * Module glue: the media device, the graph, and event routing.
 *
 * The graph is wired here with IMMUTABLE|ENABLED links because the hardware
 * has exactly one path — the media controller topology is documentation and
 * format negotiation, not routing. See DESIGN.md for why this is not (yet)
 * an OF-graph of independent drivers.
 *
 * This is a real platform driver bound to a self-registered platform
 * device, not a bare device: V4L2 core paths dereference
 * dev->driver — v4l2_device_register builds its default name from
 * driver->name, and subdev_open takes a module reference through
 * mdev->dev->driver->owner. Both were NULL oopses on the board with a
 * driverless device (module init killed mid-load; subdev node open killed
 * the caller), so the probe/bind shape is load-bearing, exactly as it is
 * for vivid and vimc.
 */
#include <linux/module.h>

#include "soph_v4l2.h"

#define SOPH_DRV_NAME "soph-v4l2"

static struct platform_device *soph_pdev;

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

static int soph_v4l2_probe(struct platform_device *pdev)
{
	struct soph_v4l2_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;
	dev->pdev = pdev;
	platform_set_drvdata(pdev, dev);

	dev->mdev.dev = &pdev->dev;
	strscpy(dev->mdev.model, "CV181x capture", sizeof(dev->mdev.model));
	media_device_init(&dev->mdev);

	dev->v4l2_dev.mdev = &dev->mdev;
	dev->v4l2_dev.notify = soph_notify;
	/* Redundant now that a driver is bound (the core would build
	 * "soph-v4l2 soph-v4l2" from driver+device name), but explicit is
	 * still better than depending on that deref.
	 */
	strscpy(dev->v4l2_dev.name, SOPH_DRV_NAME,
		sizeof(dev->v4l2_dev.name));
	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
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

	dev_info(&pdev->dev,
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
	return ret;
}

static void soph_v4l2_remove(struct platform_device *pdev)
{
	struct soph_v4l2_dev *dev = platform_get_drvdata(pdev);

	if (!dev)
		return;

	media_device_unregister(&dev->mdev);
	soph_venc_node_unregister(dev);
	soph_lt6911_unregister(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	media_device_cleanup(&dev->mdev);
	kfree(dev);
}

static struct platform_driver soph_v4l2_driver = {
	.probe = soph_v4l2_probe,
	.remove = soph_v4l2_remove,
	.driver = {
		.name = SOPH_DRV_NAME,
	},
};

static int __init soph_v4l2_init(void)
{
	int ret;

	ret = platform_driver_register(&soph_v4l2_driver);
	if (ret)
		return ret;

	/* The hardware is owned by the vendor drivers, probed from their own
	 * DT nodes; this module is pure glue with no node of its own, so it
	 * carries its own platform device the way vivid does. Registering
	 * the device after the driver binds it immediately.
	 */
	soph_pdev = platform_device_register_simple(SOPH_DRV_NAME, -1, NULL,
						    0);
	if (IS_ERR(soph_pdev)) {
		platform_driver_unregister(&soph_v4l2_driver);
		return PTR_ERR(soph_pdev);
	}

	/* Binding is synchronous on this bus; a probe failure leaves no
	 * drvdata behind. Surface it as the module load error instead of
	 * sitting there half-registered.
	 */
	if (!platform_get_drvdata(soph_pdev)) {
		platform_device_unregister(soph_pdev);
		platform_driver_unregister(&soph_v4l2_driver);
		return -ENODEV;
	}

	return 0;
}

static void __exit soph_v4l2_exit(void)
{
	platform_device_unregister(soph_pdev);
	platform_driver_unregister(&soph_v4l2_driver);
}

module_init(soph_v4l2_init);
module_exit(soph_v4l2_exit);

MODULE_DESCRIPTION("V4L2 media-controller front-end for the CV181x capture pipeline");
MODULE_AUTHOR("NanoKVM");
MODULE_LICENSE("GPL");
