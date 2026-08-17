// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exports for the cv181x_v4l2 media-controller front-end.
 *
 * The VENC MPI in cvi_venc.c already takes kernel pointers -- the ioctl
 * handler copies from userspace before calling it -- so external linkage is
 * all a kernel feeder thread needs. No behavior change.
 */
#include <linux/module.h>

#include <cvi_venc.h>

EXPORT_SYMBOL_GPL(CVI_VENC_CreateChn);
EXPORT_SYMBOL_GPL(CVI_VENC_DestroyChn);
EXPORT_SYMBOL_GPL(CVI_VENC_StartRecvFrame);
EXPORT_SYMBOL_GPL(CVI_VENC_StopRecvFrame);
EXPORT_SYMBOL_GPL(CVI_VENC_SendFrame);
EXPORT_SYMBOL_GPL(CVI_VENC_GetStream);
EXPORT_SYMBOL_GPL(CVI_VENC_ReleaseStream);
EXPORT_SYMBOL_GPL(CVI_VENC_QueryStatus);
EXPORT_SYMBOL_GPL(CVI_VENC_RequestIDR);
