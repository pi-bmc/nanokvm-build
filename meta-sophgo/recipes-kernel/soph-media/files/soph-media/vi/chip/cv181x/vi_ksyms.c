// SPDX-License-Identifier: GPL-2.0-only
/*
 * Exports for the cv181x_v4l2 media-controller front-end.
 *
 * Kept in one file rather than scattered through the vendor sources so the
 * delta against upstream osdrv stays reviewable: nothing here changes
 * behavior, it only gives external linkage to the SDK backends the ioctl
 * layer already calls with kernel pointers.
 */
#include <linux/module.h>

#include <vi_defines.h>
#include <vi_sdk_layer.h>

/* The VI_IOCTL_SET_SNR_INFO backend is inline in the ioctl switch; this is
 * the same memcpy with the same bound check, callable with a kernel pointer.
 * The block must be in place before anything reaches _vi_ctrl_init (either
 * streaming start or a buffer-size query), because that is what programs the
 * CSI bridge's expected geometry.
 */
int vi_set_snr_info_kernel(u8 raw_num, const struct cvi_isp_snr_info *info)
{
	struct cvi_vi_dev *vdev = vi_sdk_get_vdev();

	if (!vdev || !info || raw_num >= ISP_PRERAW_VIRT_MAX)
		return -EINVAL;

	memcpy(&vdev->snr_info[raw_num], info, sizeof(*info));
	return 0;
}
EXPORT_SYMBOL_GPL(vi_set_snr_info_kernel);

EXPORT_SYMBOL_GPL(vi_sdk_get_vdev);
EXPORT_SYMBOL_GPL(vi_sdk_set_vdev);
EXPORT_SYMBOL_GPL(vi_open_kernel);
EXPORT_SYMBOL_GPL(vi_release_kernel);

EXPORT_SYMBOL_GPL(vi_set_dev_attr);
EXPORT_SYMBOL_GPL(vi_enable_dev);
EXPORT_SYMBOL_GPL(vi_create_pipe);
EXPORT_SYMBOL_GPL(vi_start_pipe);
EXPORT_SYMBOL_GPL(vi_set_chn_attr);
EXPORT_SYMBOL_GPL(vi_enable_chn);
EXPORT_SYMBOL_GPL(vi_disable_chn);
EXPORT_SYMBOL_GPL(vi_get_chn_frame);
EXPORT_SYMBOL_GPL(vi_release_chn_frame);

EXPORT_SYMBOL_GPL(vi_get_ion_buf);
EXPORT_SYMBOL_GPL(vi_free_ion_buf);
EXPORT_SYMBOL_GPL(vi_mac_clk_ctrl);
EXPORT_SYMBOL_GPL(_vi_sdk_release);
