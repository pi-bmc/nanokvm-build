// SPDX-License-Identifier: GPL-2.0-only
/*
 * The ordered vendor bring-up/teardown and the per-frame encode step.
 *
 * This is a kernel port of the field-debugged userspace implementation
 * (nanokvm-app pkg/video/cvi/pipeline_setup.go + capturer.go), which is in
 * turn the distilled behavior of Sipeed's stock firmware. The ordering
 * below is not stylistic anywhere — each step's placement is load-bearing
 * and carries the comment explaining why.
 *
 * The one architectural rule, worth restating: in kernel space only VI→VPSS
 * is bound. The encoder is fed by hand from the capture node's feeder
 * thread, gated on a free vb2 buffer, so the consumer decides when a frame
 * moves. Frames nobody collects are shed silently by VPSS (StartFail, by
 * design). Binding VPSS→VENC instead lets the producer decide, and at
 * 1080p60 that floods the encoder's queue and earns one KERN_ERR per
 * dropped frame out of vb_qbuf — enough to starve userspace off a 115200
 * serial console.
 */
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/sizes.h>
#include <linux/vmalloc.h>

#include "soph_v4l2.h"

/* base_ctx.h first: the venc comm header consumes its VB_POOL typedef, the
 * same order the vendor's own cvi_venc.h establishes.
 */
#include <base_ctx.h>

#include <linux/cvi_common.h>
#include <linux/cvi_comm_video.h>
#include <linux/cvi_comm_vi.h>
#include <linux/cvi_comm_vpss.h>
#include <linux/cvi_comm_venc.h>
#include <linux/cvi_errno.h>
#include <linux/cif_uapi.h>
#include <linux/vi_isp.h>
#include <linux/vb_uapi.h>

#include <base_cb.h>
#include <vb.h>

/* ------------------------------------------------------------------ */
/* Vendor entry points. Declared here rather than via the vendor       */
/* modules' internal headers, which drag in their whole private world; */
/* the signatures are pinned by the ksyms files next to each driver.   */

struct cvi_vi_dev;
extern struct cvi_vi_dev *vi_sdk_get_vdev(void);
extern int vi_open_kernel(void);
extern int vi_release_kernel(void);
extern int vi_set_snr_info_kernel(u8 raw_num,
				  const struct cvi_isp_snr_info *info);
extern CVI_S32 vi_set_dev_attr(VI_DEV ViDev, const VI_DEV_ATTR_S *pstDevAttr);
extern CVI_S32 vi_enable_dev(VI_DEV ViDev);
extern CVI_S32 vi_create_pipe(VI_PIPE ViPipe, VI_PIPE_ATTR_S *pstPipeAttr);
extern CVI_S32 vi_start_pipe(VI_PIPE ViPipe);
extern CVI_S32 vi_set_chn_attr(VI_PIPE ViPipe, VI_CHN ViChn,
			       VI_CHN_ATTR_S *pstChnAttr);
extern CVI_S32 vi_enable_chn(VI_CHN ViChn);
extern CVI_S32 vi_disable_chn(VI_CHN ViChn);
extern int vi_get_ion_buf(struct cvi_vi_dev *vdev);
extern int vi_free_ion_buf(struct cvi_vi_dev *vdev);

extern int vpss_open_kernel(void);
extern int vpss_release_kernel(void);
extern CVI_S32 vpss_create_grp(VPSS_GRP VpssGrp,
			       const VPSS_GRP_ATTR_S *pstGrpAttr);
extern CVI_S32 vpss_destroy_grp(VPSS_GRP VpssGrp);
extern CVI_S32 vpss_start_grp(VPSS_GRP VpssGrp);
extern CVI_S32 vpss_stop_grp(VPSS_GRP VpssGrp);
extern CVI_S32 vpss_set_chn_attr(VPSS_GRP VpssGrp, VPSS_CHN VpssChn,
				 const VPSS_CHN_ATTR_S *pstChnAttr);
extern CVI_S32 vpss_enable_chn(VPSS_GRP VpssGrp, VPSS_CHN VpssChn);
extern CVI_S32 vpss_disable_chn(VPSS_GRP VpssGrp, VPSS_CHN VpssChn);
extern CVI_S32 vpss_get_chn_frame(VPSS_GRP VpssGrp, VPSS_CHN VpssChn,
				  VIDEO_FRAME_INFO_S *pstFrameInfo,
				  CVI_S32 s32MilliSec);
extern CVI_S32 vpss_release_chn_frame(VPSS_GRP VpssGrp, VPSS_CHN VpssChn,
				      const VIDEO_FRAME_INFO_S *pstVideoFrame);

extern CVI_S32 sys_bind(MMF_CHN_S *pstSrcChn, MMF_CHN_S *pstDestChn);
extern CVI_S32 sys_unbind(MMF_CHN_S *pstSrcChn, MMF_CHN_S *pstDestChn);

extern CVI_S32 CVI_VENC_CreateChn(VENC_CHN VeChn,
				  const VENC_CHN_ATTR_S *pstAttr);
extern CVI_S32 CVI_VENC_DestroyChn(VENC_CHN VeChn);
extern CVI_S32 CVI_VENC_StartRecvFrame(VENC_CHN VeChn,
				       const VENC_RECV_PIC_PARAM_S *pstRecvParam);
extern CVI_S32 CVI_VENC_StopRecvFrame(VENC_CHN VeChn);
extern CVI_S32 CVI_VENC_SendFrame(VENC_CHN VeChn,
				  const VIDEO_FRAME_INFO_S *pstFrame,
				  CVI_S32 s32MilliSec);
extern CVI_S32 CVI_VENC_GetStream(VENC_CHN VeChn, VENC_STREAM_S *pstStream,
				  CVI_S32 s32MilliSec);
extern CVI_S32 CVI_VENC_ReleaseStream(VENC_CHN VeChn,
				      VENC_STREAM_S *pstStream);
extern CVI_S32 CVI_VENC_RequestIDR(VENC_CHN VeChn, CVI_BOOL bInstant);
/* The global vcodec mutex (soph_vcodec). SendFrame takes it and a completed
 * GetStream releases it; the recovery path below unlocks it as its owner.
 */
extern void vcodec_unlock(void);

/* The LT6911's hardware reset line (PWR_GPIO1, declared as the cif node's
 * snsr-reset per the stock DTB). A pulse reboots the bridge MCU from its SPI
 * flash (~1s) and toggles HPD to the managed host, so when to pulse is
 * policy, not plumbing:
 *
 *   recover (default) - only when the bridge stops answering i2c. Normal
 *                       bring-up never touches the pin, preserving the
 *                       field-proven no-reset behavior and sparing the host
 *                       a monitor re-enumeration per STREAMON.
 *   always            - stock semantics: pulse on every bring-up, exactly
 *                       where SAMPLE_COMM_VI_StartMIPI does. A freshly
 *                       booted bridge free-runs its transmitter, so this
 *                       mode also skips the TX-start i2c write (the MCU is
 *                       mid-boot at that point and must not be spoken to).
 *   never             - the pin is left alone entirely.
 */
static char *bridge_reset = "recover";
module_param(bridge_reset, charp, 0444);
MODULE_PARM_DESC(bridge_reset,
	"When to pulse the LT6911 hardware reset: recover (default), always (stock semantics), never");

/* ------------------------------------------------------------------ */
/* Fixed pipeline coordinates. A KVM captures exactly one input into   */
/* exactly one encoded stream, so nothing here needs to be a           */
/* parameter: one MIPI device, one VI dev/pipe/chn, one VPSS grp/chn,  */
/* one VENC channel.                                                   */

#define SOPH_MIPI_DEV		0
#define SOPH_VI_DEV		0
#define SOPH_VI_PIPE		0
#define SOPH_VI_CHN		0
#define SOPH_VPSS_GRP		0
#define SOPH_VPSS_CHN		0
#define SOPH_VENC_CHN		0

/* How many finished frames VPSS holds for the feeder: one being handed
 * over while the next is written, which is what a stock board runs.
 */
#define SOPH_VPSS_DEPTH		2

/* Timeouts, from the field-tuned loop: a frame is due every 33ms at 30fps,
 * so a 100ms wait means the source stopped; the encoder gets longer because
 * an IDR on a complex screen legitimately takes a while.
 */
#define SOPH_GET_FRAME_MS	100
#define SOPH_SEND_FRAME_MS	100
#define SOPH_GET_STREAM_MS	500
/* Closing an open SendFrame/GetStream pair is mandatory (see encode_one);
 * 20 x 500ms is far beyond any legitimate encode.
 */
#define SOPH_GET_STREAM_RETRIES	20

/* First-frames pack-geometry instrumentation; see the copy loop. */
static int dbg_packs = 12;

/* The board's lane routing, bridge → SoC. Slot 0 is the clock lane. Board
 * fact, not derivable from anything.
 */
#define SOPH_LANE_IDS		{ 2, 4, 3, 1, 0 }
#define SOPH_HS_SETTLE		8

/* VB pool geometry: sized for the largest mode the bridge delivers, because
 * VB is initialised once for the life of the driver and cannot be re-laid
 * out while VI and VPSS hold blocks from it. Eight blocks ≈ 25 MiB of the
 * ~105 MiB carveout.
 */
#define SOPH_VB_BLK_CNT		8

/* ------------------------------------------------------------------ */
/* Error plumbing. The vendor MPI returns packed CVI_S32 codes          */
/* (0xC0xxxxxx as a negative int), with the error id in the low bits.  */

#define CVI_ERR_ID(e)		((u32)(e) & 0x1FFF)

/* Venc-private ids live above the common block. */
#define ERR_VENC_FRC_NO_ENC		65
#define ERR_VENC_EMPTY_STREAM_FRAME	67
#define ERR_VENC_EMPTY_PACK		68

/* "No frame right now", as distinct from failure. These drivers have no
 * ETIMEDOUT; an idle source, a warming encoder and a drained stream queue
 * all present as one of these ids.
 */
static bool soph_is_no_frame(CVI_S32 err)
{
	switch (CVI_ERR_ID(err)) {
	case EN_ERR_BUF_EMPTY:
	case EN_ERR_BUSY:
	case ERR_VENC_FRC_NO_ENC:
	case ERR_VENC_EMPTY_STREAM_FRAME:
	case ERR_VENC_EMPTY_PACK:
		return true;
	default:
		return false;
	}
}

static int soph_err(struct soph_v4l2_dev *dev, const char *what, CVI_S32 err)
{
	if (err == CVI_SUCCESS)
		return 0;
	dev_err(&dev->pdev->dev, "%s failed: 0x%08x\n", what, (u32)err);
	return -EIO;
}

/* ------------------------------------------------------------------ */
/* Pad mux and clock gates. Neither belongs to any driver: the cif     */
/* driver muxes lanes inside the D-PHY but never touches pad function  */
/* (the vendor does these writes from userspace in lt6911_probe), and  */
/* the CSI front-end clock has no claimant, so mainline's              */
/* clk_disable_unused switches it off at boot. Without the mux, RX0N   */
/* carries CAM_MCLK1 and the D-PHY locks to the SoC's own clock —      */
/* every error counter reads zero and VI never takes an interrupt.     */

#define SOPH_PADMUX_BASE	0x03001000
#define SOPH_PADMUX_LEN		0x1000
#define SOPH_PADMUX_FIRST	0x16C	/* MIPI_RX4N */
#define SOPH_PADMUX_LAST	0x190	/* MIPI_RX0P */
#define SOPH_PADMUX_MIPI	0x3

static int soph_setup_pinmux(struct soph_v4l2_dev *dev)
{
	void __iomem *base;
	u32 off;

	base = ioremap(SOPH_PADMUX_BASE, SOPH_PADMUX_LEN);
	if (!base)
		return -ENOMEM;

	for (off = SOPH_PADMUX_FIRST; off <= SOPH_PADMUX_LAST; off += 4)
		if (readl(base + off) != SOPH_PADMUX_MIPI)
			writel(SOPH_PADMUX_MIPI, base + off);

	iounmap(base);
	return 0;
}

#define SOPH_CLKGEN_BASE	0x03002000
#define SOPH_CLKGEN_LEN		0x1000

static const struct { u32 off; u32 mask; } soph_csi_clks[] = {
	{ 0x00C, BIT(2) },			/* clk_csi0_rx_vip */
	{ 0x010, BIT(9) | BIT(10) | BIT(11) | BIT(12) },  /* clk_vip_ip0..3 */
	{ 0x008, BIT(22) },			/* clk_img_v_vip */
	/* clk_img_d_vip deliberately absent: a stock working board reads
	 * it as 0.
	 */
};

/* Must run before the receiver is configured, not merely before it is
 * used — these blocks latch their configuration while clocked. RMW one bit
 * at a time; the other bits in each word belong to the clock framework.
 */
static int soph_setup_csi_clocks(struct soph_v4l2_dev *dev)
{
	void __iomem *base;
	u32 v;
	int i;

	base = ioremap(SOPH_CLKGEN_BASE, SOPH_CLKGEN_LEN);
	if (!base)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(soph_csi_clks); i++) {
		v = readl(base + soph_csi_clks[i].off);
		if ((v & soph_csi_clks[i].mask) != soph_csi_clks[i].mask)
			writel(v | soph_csi_clks[i].mask,
			       base + soph_csi_clks[i].off);
	}

	iounmap(base);
	return 0;
}

/* ------------------------------------------------------------------ */
/* CIF (MIPI RX). The cif driver registered its whole ioctl surface in  */
/* the base callback table with from_user=0, so every CVI_MIPI_* cmd    */
/* is drivable from kernel space with a kernel pointer — no exports     */
/* needed at all.                                                       */

static int soph_cif_call(struct soph_v4l2_dev *dev, const char *what,
			 unsigned int cmd, void *arg)
{
	struct base_exe_m_cb cb = {
		.caller = E_MODULE_BASE,
		.callee = E_MODULE_CIF,
		.cmd_id = cmd,
		.data = arg,
	};
	int ret;

	ret = base_exe_module_cb(&cb);
	if (ret)
		dev_err(&dev->pdev->dev, "cif %s failed: %d\n", what, ret);
	return ret;
}

/* Pulse the bridge's hardware reset and wait out its firmware boot. Used
 * by the recovery path (a bridge that stopped answering i2c) and available
 * to bring-up in bridge_reset=always mode.
 */
int soph_pipeline_bridge_reset(struct soph_v4l2_dev *dev)
{
	unsigned int devno = SOPH_MIPI_DEV;
	int ret;

	if (!strcmp(bridge_reset, "never"))
		return -EPERM;

	dev_warn(&dev->pdev->dev, "hard-resetting the LT6911 bridge\n");

	ret = soph_cif_call(dev, "assert bridge reset", CVI_MIPI_RESET_SENSOR,
			    &devno);
	if (ret)
		return ret;
	msleep(10);
	ret = soph_cif_call(dev, "release bridge reset",
			    CVI_MIPI_UNRESET_SENSOR, &devno);
	if (ret)
		return ret;

	/* The MCU reboots from its SPI flash; nothing may talk i2c to it
	 * until that finishes. 1.5s is generous on purpose -- this path
	 * runs when the bridge was already wedged.
	 */
	msleep(1500);
	return 0;
}

/* The vendor's sequence, with its 20µs settle between starting the clock
 * and releasing reset: the receiver latches its configuration on release,
 * so it wants a stable clock first, and unresetting before SET_DEV_ATTR
 * would run it with whatever the previous session left behind.
 *
 * The RESET/UNRESET_SENSOR pair only runs in bridge_reset=always mode. With
 * the reset line now declared in DT those ioctls really pulse the bridge,
 * and the field-proven default flow never did (the pin used to be absent,
 * making them no-ops).
 */
static int soph_setup_mipi(struct soph_v4l2_dev *dev)
{
	struct combo_dev_attr_s attr = {
		.input_mode = INPUT_MODE_MIPI,
		.mac_clk = RX_MAC_CLK_600M,
		.mclk = {
			.cam = 0,
			/* The bridge runs from its own crystal. */
			.freq = CAMPLL_FREQ_NONE,
		},
		.mipi_attr = {
			.raw_data_type = YUV422_8BIT,
			.lane_id = SOPH_LANE_IDS,
			.wdr_mode = CVI_MIPI_WDR_MODE_NONE,
			.dphy = {
				.enable = 1,
				.hs_settle = SOPH_HS_SETTLE,
			},
		},
		.devno = SOPH_MIPI_DEV,
		.img_size = {
			.width = dev->cfg.in_w,
			.height = dev->cfg.in_h,
		},
	};
	unsigned int devno = SOPH_MIPI_DEV;
	bool hw_reset = !strcmp(bridge_reset, "always");
	int ret;

	if (hw_reset) {
		ret = soph_cif_call(dev, "assert bridge reset",
				    CVI_MIPI_RESET_SENSOR, &devno);
		if (ret)
			return ret;
	}
	ret = soph_cif_call(dev, "reset mipi", CVI_MIPI_RESET_MIPI, &devno);
	if (ret)
		return ret;
	ret = soph_cif_call(dev, "set dev attr", CVI_MIPI_SET_DEV_ATTR, &attr);
	if (ret)
		return ret;
	ret = soph_cif_call(dev, "enable clock", CVI_MIPI_ENABLE_SENSOR_CLOCK,
			    &devno);
	if (ret)
		return ret;

	usleep_range(20, 40);

	if (!hw_reset)
		return 0;
	return soph_cif_call(dev, "release bridge reset",
			     CVI_MIPI_UNRESET_SENSOR, &devno);
}

static void soph_mipi_clock_off(struct soph_v4l2_dev *dev)
{
	unsigned int devno = SOPH_MIPI_DEV;

	soph_cif_call(dev, "disable clock", CVI_MIPI_DISABLE_SENSOR_CLOCK,
		      &devno);
}

/* ------------------------------------------------------------------ */
/* VB: the common frame pools. Global to the driver stack, initialised */
/* once and left up — VI and VPSS take their frame buffers from here   */
/* and vpss_set_chn_attr dereferences a NULL pool array (a kernel      */
/* oops, not an error return) if VB was never brought up.              */

static u32 soph_vb_blk_size(u32 w, u32 h)
{
	/* NV21 with the 64-byte stride the scaler writes at. Guessing low
	 * does not fail cleanly — allocations come back short — so the
	 * alignment is applied rather than assumed away.
	 */
	return (ALIGN(w, 64) * h * 3) / 2;
}

static int soph_setup_vb(struct soph_v4l2_dev *dev)
{
	static struct cvi_vb_cfg cfg;	/* ~1KiB: keep off the stack */
	struct vb_ext_control ctl = { .id = VB_IOCTL_INIT };
	int ret;

	if (vb_is_inited())
		return 0;

	memset(&cfg, 0, sizeof(cfg));
	cfg.comm_pool_cnt = 1;
	cfg.comm_pool[0].blk_size = soph_vb_blk_size(SOPH_MAX_W, SOPH_MAX_H);
	cfg.comm_pool[0].blk_cnt = SOPH_VB_BLK_CNT;
	cfg.comm_pool[0].remap_mode = 0;	/* VB_REMAP_MODE_NONE: the
						 * pixels never need a CPU
						 * mapping — they move
						 * VI→VPSS→VENC entirely in
						 * kernel space. */
	strscpy(cfg.comm_pool[0].pool_name, "soph_v4l2",
		sizeof(cfg.comm_pool[0].pool_name));

	ret = vb_set_config_kernel(&cfg);
	if (ret)
		return soph_err(dev, "vb set config", ret);

	return soph_err(dev, "vb init", (CVI_S32)vb_ctrl(&ctl));
}

/* ------------------------------------------------------------------ */
/* VI                                                                   */

static u32 soph_src_fps(struct soph_v4l2_dev *dev)
{
	/* Falling back to the destination rate when the measurement failed
	 * is not a guess at the real rate — it is the one value that makes
	 * the rate converter a no-op, which is the honest thing to do when
	 * the input is unknown.
	 */
	return dev->cfg.in_fps ? dev->cfg.in_fps : dev->cfg.out_fps;
}

/* Dev before the receiver, as the vendor does: the device has to be
 * listening on the interface before the receiver starts driving it.
 */
static int soph_setup_vi_dev(struct soph_v4l2_dev *dev)
{
	VI_DEV_ATTR_S da = {
		.enIntfMode = VI_MODE_MIPI_YUV422,
		.enWorkMode = VI_WORK_MODE_1Multiplex,
		.enScanMode = VI_SCAN_PROGRESSIVE,
		/* -1 in every slot: not an analogue input. */
		.as32AdChnId = { -1, -1, -1, -1 },
		.enDataSeq = VI_DATA_SEQ_UYVY,
		.enInputDataType = VI_DATA_TYPE_YUV,
		.stSize = {
			.u32Width = dev->cfg.in_w,
			.u32Height = dev->cfg.in_h,
		},
		.stWDRAttr = { .enWDRMode = WDR_MODE_NONE },
		.chn_num = 1,
		.snrFps = dev->cfg.out_fps,
	};
	int ret;

	ret = soph_err(dev, "vi set dev attr",
		       vi_set_dev_attr(SOPH_VI_DEV, &da));
	if (ret)
		return ret;
	return soph_err(dev, "vi enable dev", vi_enable_dev(SOPH_VI_DEV));
}

/* The "sensor" geometry block the ISP init consumes. Without it the CSI
 * bridge compares arriving frames against width 0, fails every one, and the
 * error handler resets the ISP and re-triggers preraw forever. Must be in
 * place before anything reaches _vi_ctrl_init — which streaming start does.
 */
static int soph_setup_snr_info(struct soph_v4l2_dev *dev)
{
	struct cvi_isp_snr_info info = {
		.raw_num = SOPH_VI_DEV,
		/* Bayer BG. Must not be an RGBIR pattern (9/11), which
		 * would flip is_rgbir_sensor and change the pipeline shape.
		 */
		.color_mode = 0,
		.pixel_rate = 0,	/* declared but never read */
		.snr_fmt = {
			.frm_num = 1,	/* linear; >1 enables HDR */
			.img_size = { {
				.width = dev->cfg.in_w,
				.height = dev->cfg.in_h,
				.start_x = 0,
				.start_y = 0,
				.active_w = dev->cfg.in_w,
				.active_h = dev->cfg.in_h,
				.max_width = dev->cfg.in_w,
				.max_height = dev->cfg.in_h,
			} },
		},
	};

	return soph_err(dev, "vi set snr info",
			vi_set_snr_info_kernel(SOPH_VI_DEV, &info));
}

static int soph_setup_vi_pipe(struct soph_v4l2_dev *dev)
{
	u32 fps = soph_src_fps(dev);
	VI_PIPE_ATTR_S pa = {
		.enPipeBypassMode = VI_PIPE_BYPASS_NONE,
		.u32MaxW = dev->cfg.in_w,
		.u32MaxH = dev->cfg.in_h,
		.enPixFmt = PIXEL_FORMAT_NV21,
		.enCompressMode = COMPRESS_MODE_NONE,
		.enBitWidth = DATA_BITWIDTH_8,
		/* Source rate on both sides: VI does not convert, and
		 * nothing in the driver reads this pair except the proc
		 * node. The drop happens at the VPSS channel.
		 */
		.stFrameRate = {
			.s32SrcFrameRate = fps,
			.s32DstFrameRate = fps,
		},
		/* The bridge already delivers YUV: bypassing the ISP skips
		 * the Bayer stages instead of running them over data they
		 * were never meant to see.
		 */
		.bYuvBypassPath = 1,
	};
	VI_CHN_ATTR_S ca = {
		.stSize = {
			.u32Width = dev->cfg.in_w,
			.u32Height = dev->cfg.in_h,
		},
		.enPixelFormat = PIXEL_FORMAT_NV21,
		.enDynamicRange = DYNAMIC_RANGE_SDR8,
		.enVideoFormat = VIDEO_FORMAT_LINEAR,
		.enCompressMode = COMPRESS_MODE_NONE,
		/* Zero: nothing reads frames here, they go straight to VPSS
		 * over the bind, and a depth VI holds frames for is a depth
		 * nobody drains.
		 */
		.u32Depth = 0,
		.stFrameRate = {
			.s32SrcFrameRate = fps,
			.s32DstFrameRate = fps,
		},
	};
	struct cvi_vi_dev *vdev = vi_sdk_get_vdev();
	int ret;

	if (!vdev)
		return -ENODEV;

	ret = soph_err(dev, "vi create pipe",
		       vi_create_pipe(SOPH_VI_PIPE, &pa));
	if (ret)
		return ret;
	ret = soph_err(dev, "vi set chn attr",
		       vi_set_chn_attr(SOPH_VI_PIPE, SOPH_VI_CHN, &ca));
	if (ret)
		return ret;

	/* The per-pipe CMDQ buffers, then the channel. vi_enable_chn also
	 * starts the driver's streaming machinery.
	 */
	ret = soph_err(dev, "vi ion buf", vi_get_ion_buf(vdev));
	if (ret)
		return ret;
	return soph_err(dev, "vi enable chn", vi_enable_chn(SOPH_VI_CHN));
}

/* ------------------------------------------------------------------ */
/* VPSS                                                                 */

static int soph_setup_vpss(struct soph_v4l2_dev *dev)
{
	u32 src = soph_src_fps(dev);
	VPSS_GRP_ATTR_S ga = {
		.u32MaxW = dev->cfg.in_w,
		.u32MaxH = dev->cfg.in_h,
		.enPixelFormat = PIXEL_FORMAT_NV21,
		/* Equal on purpose: the group has no converter and an
		 * unequal pair buys a warning per bring-up and nothing
		 * else.
		 */
		.stFrameRate = {
			.s32SrcFrameRate = src,
			.s32DstFrameRate = src,
		},
		.u8VpssDev = 0,
	};
	VPSS_CHN_ATTR_S ca = {
		.u32Width = dev->cfg.out_w,
		.u32Height = dev->cfg.out_h,
		.enVideoFormat = VIDEO_FORMAT_LINEAR,
		.enPixelFormat = PIXEL_FORMAT_NV21,
		/* The one place in the whole pipeline that actually drops
		 * frames. The converter only engages when dst is strictly
		 * less than src — an equal pair is "do not convert", and a
		 * 60fps host would walk straight through into an encoder
		 * built for 30.
		 */
		.stFrameRate = {
			.s32SrcFrameRate = src,
			.s32DstFrameRate = dev->cfg.out_fps,
		},
		.u32Depth = SOPH_VPSS_DEPTH,
		.stAspectRatio = { .enMode = ASPECT_RATIO_NONE },
	};
	int ret;

	ret = soph_err(dev, "vpss create grp",
		       vpss_create_grp(SOPH_VPSS_GRP, &ga));
	if (ret)
		return ret;
	ret = soph_err(dev, "vpss set chn attr",
		       vpss_set_chn_attr(SOPH_VPSS_GRP, SOPH_VPSS_CHN, &ca));
	if (ret)
		return ret;
	return soph_err(dev, "vpss enable chn",
			vpss_enable_chn(SOPH_VPSS_GRP, SOPH_VPSS_CHN));
}

/* ------------------------------------------------------------------ */
/* VENC                                                                 */

static u32 soph_bitstream_buf_size(u32 w, u32 h)
{
	/* The ring has to survive the worst case — the IDR sent when a
	 * viewer joins on a screen full of text — without stalling the
	 * encoder.
	 */
	return max_t(u32, w * h / 2, SZ_1M);
}

static int soph_setup_venc(struct soph_v4l2_dev *dev)
{
	VENC_CHN_ATTR_S attr = { 0 };
	u32 kbps = dev->cfg.bitrate_bps / 1000;

	attr.stVencAttr.u32MaxPicWidth = dev->cfg.out_w;
	attr.stVencAttr.u32MaxPicHeight = dev->cfg.out_h;
	attr.stVencAttr.u32PicWidth = dev->cfg.out_w;
	attr.stVencAttr.u32PicHeight = dev->cfg.out_h;
	attr.stVencAttr.u32BufSize =
		soph_bitstream_buf_size(dev->cfg.out_w, dev->cfg.out_h);
	attr.stVencAttr.u32Profile = 0;		/* baseline */
	attr.stVencAttr.bByFrame = 1;		/* one GetStream per frame */
	attr.stVencAttr.bSingleCore = 1;

	switch (dev->cfg.pixelformat) {
	case V4L2_PIX_FMT_H264:
		attr.stVencAttr.enType = PT_H264;
		attr.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
		attr.stRcAttr.stH264Cbr.u32Gop = dev->cfg.gop;
		attr.stRcAttr.stH264Cbr.u32StatTime = 1;
		attr.stRcAttr.stH264Cbr.u32SrcFrameRate = dev->cfg.out_fps;
		attr.stRcAttr.stH264Cbr.fr32DstFrameRate = dev->cfg.out_fps;
		attr.stRcAttr.stH264Cbr.u32BitRate = kbps;
		break;
	case V4L2_PIX_FMT_HEVC:
		attr.stVencAttr.enType = PT_H265;
		attr.stRcAttr.enRcMode = VENC_RC_MODE_H265CBR;
		attr.stRcAttr.stH265Cbr.u32Gop = dev->cfg.gop;
		attr.stRcAttr.stH265Cbr.u32StatTime = 1;
		attr.stRcAttr.stH265Cbr.u32SrcFrameRate = dev->cfg.out_fps;
		attr.stRcAttr.stH265Cbr.fr32DstFrameRate = dev->cfg.out_fps;
		attr.stRcAttr.stH265Cbr.u32BitRate = kbps;
		break;
	case V4L2_PIX_FMT_MJPEG:
		attr.stVencAttr.enType = PT_MJPEG;
		attr.stRcAttr.enRcMode = VENC_RC_MODE_MJPEGCBR;
		attr.stRcAttr.stMjpegCbr.u32StatTime = 1;
		attr.stRcAttr.stMjpegCbr.u32SrcFrameRate = dev->cfg.out_fps;
		attr.stRcAttr.stMjpegCbr.fr32DstFrameRate = dev->cfg.out_fps;
		attr.stRcAttr.stMjpegCbr.u32BitRate = kbps;
		break;
	default:
		return -EINVAL;
	}

	attr.stGopAttr.enGopMode = VENC_GOPMODE_NORMALP;
	attr.stGopAttr.stNormalP.s32IPQpDelta = 2;

	/* First CreateChn loads the VPU firmware from
	 * /usr/share/fw_vcodec/ via filp_open, so this must run after the
	 * rootfs is up — which STREAMON guarantees.
	 */
	return soph_err(dev, "venc create chn",
			CVI_VENC_CreateChn(SOPH_VENC_CHN, &attr));
}

/* ------------------------------------------------------------------ */
/* Reclaim: destroy pipeline objects a previous life left behind.       */
/* These objects live in the vendor drivers and survive both userspace  */
/* processes and this module's streaming sessions — a failed half-      */
/* bring-up, or a vendor MPI userspace that ran before us, leaves       */
/* group 0 and channel 0 allocated and every create refused with        */
/* "resource exists", permanently. So bring-up destroys before it       */
/* creates, unconditionally; every call here is expected to fail in     */
/* the ordinary case, which is why none is checked.                     */

static void soph_reclaim_stale(struct soph_v4l2_dev *dev)
{
	MMF_CHN_S src = { .enModId = CVI_ID_VI, .s32DevId = SOPH_VI_DEV,
			  .s32ChnId = SOPH_VI_CHN };
	MMF_CHN_S dst = { .enModId = CVI_ID_VPSS, .s32DevId = SOPH_VPSS_GRP,
			  .s32ChnId = SOPH_VPSS_CHN };
	struct cvi_vi_dev *vdev = vi_sdk_get_vdev();

	sys_unbind(&src, &dst);
	if (!dev->venc_dead) {
		CVI_VENC_StopRecvFrame(SOPH_VENC_CHN);
		CVI_VENC_DestroyChn(SOPH_VENC_CHN);
	}
	vpss_stop_grp(SOPH_VPSS_GRP);
	vpss_disable_chn(SOPH_VPSS_GRP, SOPH_VPSS_CHN);
	vpss_destroy_grp(SOPH_VPSS_GRP);
	vi_disable_chn(SOPH_VI_CHN);
	if (vdev)
		vi_free_ion_buf(vdev);
}

/* ------------------------------------------------------------------ */
/* Bring-up. The order is the vendor's, kept step for step; see         */
/* DESIGN.md for the annotated list.                                    */

int soph_pipeline_up(struct soph_v4l2_dev *dev)
{
	MMF_CHN_S src = { .enModId = CVI_ID_VI, .s32DevId = SOPH_VI_DEV,
			  .s32ChnId = SOPH_VI_CHN };
	MMF_CHN_S dst = { .enModId = CVI_ID_VPSS, .s32DevId = SOPH_VPSS_GRP,
			  .s32ChnId = SOPH_VPSS_CHN };
	VENC_RECV_PIC_PARAM_S recv = { .s32RecvPicNum = -1 };
	bool vi_opened = false, vpss_opened = false, bound = false;
	bool venc_created = false, tx_started = false;
	int ret;

	if (dev->pipe_up)
		return 0;

	/* A hung encoder core survives STREAMOFF (teardown skips it), and
	 * any register access to it -- CreateChn included -- has been
	 * observed to reset the SoC. Refuse bring-up until the modules are
	 * reloaded.
	 */
	if (dev->venc_dead) {
		dev_err_ratelimited(&dev->pdev->dev,
				    "capture disabled: encoder core hung; reload the media modules\n");
		return -EIO;
	}

	/* Take the drivers' open references first: VI hangs its one-time
	 * init (clocks, sw init) and VPSS its clock enables off the open
	 * count, and dropping them again is the only way to reach the
	 * release paths that reset per-mode state (frame geometry latches,
	 * the MAC clock) on teardown.
	 */
	ret = vi_open_kernel();
	if (ret)
		return ret;
	vi_opened = true;
	ret = vpss_open_kernel();
	if (ret)
		goto err;
	vpss_opened = true;

	soph_reclaim_stale(dev);

	ret = soph_setup_vb(dev);
	if (ret)
		goto err;
	ret = soph_setup_pinmux(dev);
	if (ret)
		goto err;
	ret = soph_setup_csi_clocks(dev);
	if (ret)
		goto err;
	ret = soph_setup_vi_dev(dev);
	if (ret)
		goto err;
	ret = soph_setup_snr_info(dev);
	if (ret)
		goto err;
	ret = soph_setup_mipi(dev);
	if (ret)
		goto err;

	/* Put the bridge on the lanes now that the receiver is configured
	 * and out of reset, and before VI starts looking for data. Not in
	 * always-reset mode: the bridge MCU is mid-boot right now and must
	 * not be spoken to -- and a freshly booted bridge free-runs its
	 * transmitter anyway, which is exactly why stock never writes it.
	 */
	if (strcmp(bridge_reset, "always")) {
		ret = soph_lt6911_tx(&dev->bridge, true);
		if (ret)
			goto err;
		tx_started = true;
	}

	ret = soph_setup_vi_pipe(dev);
	if (ret)
		goto err;

	/* No ISP DMA pool: with a YUV source and an offline scaler the
	 * driver's own size query answers zero — frames land in VB blocks
	 * instead, and _vi_ctrl_init runs from streaming start with the
	 * snr_info above already in place.
	 */

	ret = soph_setup_vpss(dev);
	if (ret)
		goto err;
	ret = soph_setup_venc(dev);
	if (ret)
		goto err;
	venc_created = true;

	/* VI → VPSS in kernel space, and there it stops. See the header
	 * comment for why the second bind is deliberately absent.
	 */
	ret = soph_err(dev, "bind vi->vpss", sys_bind(&src, &dst));
	if (ret)
		goto err;
	bound = true;

	/* Start the pipe only once the whole chain exists downstream. */
	ret = soph_err(dev, "vi start pipe", vi_start_pipe(SOPH_VI_PIPE));
	if (ret)
		goto err;
	ret = soph_err(dev, "vpss start grp", vpss_start_grp(SOPH_VPSS_GRP));
	if (ret)
		goto err;
	ret = soph_err(dev, "venc start recv",
		       CVI_VENC_StartRecvFrame(SOPH_VENC_CHN, &recv));
	if (ret)
		goto err;

	dev->pipe_up = true;
	return 0;

err:
	/* Unwind in teardown order; a half-built pipeline left standing
	 * turns the next attempt's "resource exists" into a permanent
	 * failure.
	 */
	if (bound)
		sys_unbind(&src, &dst);
	if (venc_created) {
		CVI_VENC_StopRecvFrame(SOPH_VENC_CHN);
		CVI_VENC_DestroyChn(SOPH_VENC_CHN);
	}
	soph_reclaim_stale(dev);
	if (tx_started)
		soph_lt6911_tx(&dev->bridge, false);
	soph_mipi_clock_off(dev);
	if (vpss_opened)
		vpss_release_kernel();
	if (vi_opened)
		vi_release_kernel();
	return ret;
}

/* Teardown, in the reverse of the order it was built. The order between
 * unbind, StopRecvFrame and DestroyChn is a lifetime rule, not style: the
 * kthread_stop for the encoder's bind-mode handler hides behind that
 * sequence, and destroying a still-bound channel vfree()s its context out
 * from under a running thread.
 */
void soph_pipeline_down(struct soph_v4l2_dev *dev)
{
	MMF_CHN_S src = { .enModId = CVI_ID_VI, .s32DevId = SOPH_VI_DEV,
			  .s32ChnId = SOPH_VI_CHN };
	MMF_CHN_S dst = { .enModId = CVI_ID_VPSS, .s32DevId = SOPH_VPSS_GRP,
			  .s32ChnId = SOPH_VPSS_CHN };
	struct cvi_vi_dev *vdev = vi_sdk_get_vdev();

	if (!dev->pipe_up)
		return;
	dev->pipe_up = false;

	sys_unbind(&src, &dst);
	if (dev->venc_dead) {
		/* The CODA core stopped answering and DestroyChn on it was
		 * observed to reset the whole SoC. Leak the channel; capture
		 * is down until a module reload either way.
		 */
		dev_warn(&dev->pdev->dev,
			 "skipping venc teardown: encoder core hung\n");
	} else {
		CVI_VENC_StopRecvFrame(SOPH_VENC_CHN);
		CVI_VENC_DestroyChn(SOPH_VENC_CHN);
	}

	vpss_stop_grp(SOPH_VPSS_GRP);
	vpss_disable_chn(SOPH_VPSS_GRP, SOPH_VPSS_CHN);
	vpss_destroy_grp(SOPH_VPSS_GRP);

	vi_disable_chn(SOPH_VI_CHN);
	if (vdev)
		vi_free_ion_buf(vdev);

	soph_mipi_clock_off(dev);

	/* Take the bridge off the lanes last, once nothing downstream is
	 * still expecting data from it. Skipped in always-reset mode, which
	 * never turned it on -- writing TX-off there would halt a free-
	 * running transmitter until the next reset pulse.
	 */
	if (strcmp(bridge_reset, "always"))
		soph_lt6911_tx(&dev->bridge, false);

	/* Drop the open references, which is the only way to reach the
	 * drivers' own release paths: VI's drops the MAC clock this
	 * bring-up took and clears the frame-size latch so the next
	 * bring-up can program a new geometry.
	 */
	vpss_release_kernel();
	vi_release_kernel();
}

/* ------------------------------------------------------------------ */
/* The per-frame step, called from the feeder thread with a free vb2    */
/* buffer in hand — which is the entire back-pressure story: nothing    */
/* is pulled from VPSS until there is somewhere for the result to go.   */

int soph_pipeline_encode_one(struct soph_v4l2_dev *dev,
			     struct vb2_v4l2_buffer *vbuf)
{
	VIDEO_FRAME_INFO_S frame;
	VENC_STREAM_S stream;
	CVI_S32 cret;
	void *dst;
	size_t dst_size, used = 0;
	bool keyframe = false;
	bool sent;
	u64 pts = 0;
	u32 i, tries;
	int ret;

	memset(&frame, 0, sizeof(frame));
	cret = vpss_get_chn_frame(SOPH_VPSS_GRP, SOPH_VPSS_CHN, &frame,
				  SOPH_GET_FRAME_MS);
	if (cret != CVI_SUCCESS)
		/* Timeout: the source is idle (static screen, no input).
		 * Not an error; VPSS simply had nothing for us.
		 */
		return -EAGAIN;

	cret = CVI_VENC_SendFrame(SOPH_VENC_CHN, &frame, SOPH_SEND_FRAME_MS);

	/* Release immediately, before looking at the send result: a frame
	 * that cannot be released is a block permanently lost from the
	 * pool.
	 */
	vpss_release_chn_frame(SOPH_VPSS_GRP, SOPH_VPSS_CHN, &frame);

	sent = (cret == CVI_SUCCESS);
	if (!sent && !soph_is_no_frame(cret))
		return soph_err(dev, "venc send frame", cret);
	/* A refused SendFrame (encoder queue full) still falls through to
	 * GetStream: the encoder is busy exactly when its output needs
	 * draining, and skipping the collection would deadlock after a
	 * handful of frames.
	 */

	/* The vendor contract, spelled out in its own source ("user should
	 * keep get frame until success"): a successful SendFrame takes the
	 * global vcodec mutex and only a completed GetStream releases it. A
	 * timed-out GetStream returns with the lock still held by THIS task,
	 * so once a frame is sent the pair must be closed before this
	 * function returns -- abandoning it leaks the mutex, and the next
	 * DestroyChn deadlocks in EnterVcodecLock. Observed on the board as
	 * STREAMOFF hung forever after the feeder exited mid-pair.
	 *
	 * Hence: sent frames retry GetStream on the benign codes, generously
	 * bounded; an unsent cycle keeps the old single-try behaviour (no
	 * lock is at stake).
	 */
	tries = sent ? SOPH_GET_STREAM_RETRIES : 1;
	for (i = 0; i < tries; i++) {
		memset(&stream, 0, sizeof(stream));
		/* pstPack stays NULL: GetStream allocates the pack array and
		 * the caller frees it -- on every attempt, success or not.
		 */
		cret = CVI_VENC_GetStream(SOPH_VENC_CHN, &stream,
					  SOPH_GET_STREAM_MS);
		if (cret == CVI_SUCCESS || !soph_is_no_frame(cret))
			break;
		vfree(stream.pstPack);
		stream.pstPack = NULL;
		if (i == 0)
			dev_info_ratelimited(&dev->pdev->dev,
					     "venc busy after send, retrying (0x%08x)\n",
					     (u32)cret);
	}
	if (cret != CVI_SUCCESS) {
		vfree(stream.pstPack);
		if (sent) {
			/* The encoder never finished a frame it accepted. The
			 * global vcodec mutex is held BY THIS TASK (SendFrame
			 * took it; only a completed GetStream releases it), so
			 * release it here as its legal owner before failing
			 * the stream — leaving it held turns one stalled
			 * frame into a chip-wide wedge: every later CreateChn
			 * (including the app's own rebuild) blocks forever in
			 * EnterVcodecLock, observed on the board as D-state
			 * STREAMON. With the lock released, normal teardown
			 * and the next STREAMON recover the channel fully.
			 */
			vcodec_unlock();
			dev->venc_dead = true;
			dev_crit(&dev->pdev->dev,
				 "venc never returned a sent frame (0x%08x); released the vcodec lock; venc teardown disabled (hung core) -- capture needs a module reload\n",
				 (u32)cret);
			return -EIO;
		}
		if (soph_is_no_frame(cret))
			return -EAGAIN;
		return soph_err(dev, "venc get stream", cret);
	}

	dst = vb2_plane_vaddr(&vbuf->vb2_buf, 0);
	dst_size = vb2_plane_size(&vbuf->vb2_buf, 0);
	ret = 0;

	for (i = 0; i < stream.u32PackCount; i++) {
		VENC_PACK_S *p = &stream.pstPack[i];
		u32 len = p->u32Len - p->u32Offset;

		/* Bring-up instrumentation: the first frames' pack geometry,
		 * to pin the vendor's offset/len convention against what the
		 * bitstream actually decodes as. Cheap and rate-limited by
		 * nature; remove once the convention is proven on hardware.
		 */
		if (dbg_packs > 0) {
			dbg_packs--;
			dev_info(&dev->pdev->dev,
				 "pack %u/%u: len=%u off=%u type=%u virt=%p phys=%llx head=%*ph\n",
				 i, stream.u32PackCount, p->u32Len,
				 p->u32Offset, p->DataType.enH264EType,
				 p->pu8Addr, p->u64PhyAddr,
				 8, p->pu8Addr + p->u32Offset);
		}

		/* pu8Addr is a kernel virtual address and the driver has
		 * already done the cache maintenance; SPS/PPS arrive as
		 * leading packs of the IDR access unit and concatenate
		 * like any other NAL — the VPU emits Annex-B start codes
		 * itself.
		 */
		if (used + len > dst_size) {
			dev_warn_ratelimited(&dev->pdev->dev,
				"encoded frame (%zu+%u) exceeds buffer (%zu), dropping\n",
				used, len, dst_size);
			ret = -EAGAIN;
			break;
		}
		memcpy(dst + used, p->pu8Addr + p->u32Offset, len);
		used += len;

		if (i == 0)
			pts = p->u64PTS;
		if (p->DataType.enH264EType == H264E_NALU_IDRSLICE ||
		    p->DataType.enH265EType == H265E_NALU_IDRSLICE)
			keyframe = true;
	}

	CVI_VENC_ReleaseStream(SOPH_VENC_CHN, &stream);
	vfree(stream.pstPack);

	if (ret)
		return ret;

	vb2_set_plane_payload(&vbuf->vb2_buf, 0, used);
	vbuf->vb2_buf.timestamp = pts * NSEC_PER_USEC;
	vbuf->flags &= ~(V4L2_BUF_FLAG_KEYFRAME | V4L2_BUF_FLAG_PFRAME);
	vbuf->flags |= keyframe ? V4L2_BUF_FLAG_KEYFRAME : V4L2_BUF_FLAG_PFRAME;
	return 0;
}

void soph_pipeline_request_idr(struct soph_v4l2_dev *dev)
{
	if (dev->pipe_up)
		CVI_VENC_RequestIDR(SOPH_VENC_CHN, CVI_TRUE);
}
