// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exports for the cv181x_v4l2 media-controller front-end.
 *
 * One file, no behavior change: external linkage for the vpss.h surface the
 * ioctl layer already drives with kernel pointers.
 */
#include <linux/module.h>

#include <vpss.h>

EXPORT_SYMBOL_GPL(vpss_create_grp);
EXPORT_SYMBOL_GPL(vpss_destroy_grp);
EXPORT_SYMBOL_GPL(vpss_start_grp);
EXPORT_SYMBOL_GPL(vpss_stop_grp);
EXPORT_SYMBOL_GPL(vpss_set_chn_attr);
EXPORT_SYMBOL_GPL(vpss_enable_chn);
EXPORT_SYMBOL_GPL(vpss_disable_chn);
EXPORT_SYMBOL_GPL(vpss_get_chn_frame);
EXPORT_SYMBOL_GPL(vpss_release_chn_frame);
EXPORT_SYMBOL_GPL(vpss_open_kernel);
EXPORT_SYMBOL_GPL(vpss_release_kernel);
