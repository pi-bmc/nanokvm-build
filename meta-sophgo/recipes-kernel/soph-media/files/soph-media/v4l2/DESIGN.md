# soph_v4l2 — V4L2 Media Controller front-end for the CV181x capture pipeline

## Why this module exists

The vendor stack (soph-media: sys/base/cif/vi/vpss/cvi_vc_drv) exposes the
capture pipeline as nine bespoke char devices driven by a userspace MPI
re-implementation. Every consumer has to re-learn the bring-up order, the VB
pool rules, and — the hard one — the pacing model: the encoder must be *fed by
a consumer*, never *bound to a producer*. Binding VPSS→VENC in kernel pushes
every scaled frame into the encoder's queue whether or not it can take one; at
1080p60 the queue fills and the driver logs one KERN_ERR per dropped frame,
which on a 115200 serial console is a denial of service against the scheduler.
Stock firmware never binds that edge: userspace pulls from VPSS with
`CVI_VPSS_GetChnFrame` and pushes with `CVI_VENC_SendFrame` only when the
encoder is ready, so VPSS silently sheds what nobody collects (counted as
`StartFail`, by design; a stock board mid-stream shows ~34k frames shed against
0 lost and zero error lines).

This module moves that whole contract into the kernel behind the standard V4L2
/ Media Controller API. Userspace sees:

```text
/dev/media0            topology + link/format configuration (media-ctl)
/dev/v4l-subdev*       lt6911 bridge, CSI-2 receiver, ISP front-end, scaler
/dev/video0            H.264/H.265/MJPEG bitstream capture node (vb2)
```

and streams with plain `VIDIOC_S_FMT` / `REQBUFS` / `STREAMON` / `DQBUF`.

## Topology

```text
 [ lt6911 a ]───▶[ cv181x-csi ]───▶[ cv181x-isp ]───▶[ cv181x-scaler ]───▶[ cv181x-venc /dev/video0 ]
  HDMI bridge      MIPI CSI-2 RX      VI, YUV bypass      VPSS grp0/chn0        VENC chn0, vb2 capture
  (i2c subdev)     (cif wrapper)      (vi wrapper)        (vpss wrapper)        (bitstream node)
```

- **lt6911**: i2c subdev. Detects/reports input timings (`g_dv_timings`,
  `query_dv_timings`), starts/stops the CSI transmitter on `s_stream`, and
  raises `V4L2_EVENT_SOURCE_CHANGE` when the input mode changes or the cable
  state flips (polled, as stock firmware does).
- **cv181x-csi**: wraps the cif driver. `s_stream(1)` runs the vendor
  sequence: reset sensor → reset mipi → set combo dev attr (from the
  negotiated sink format) → enable clock → settle → release reset.
- **cv181x-isp**: wraps VI. Owns dev/pipe/chn lifetime, always NV21 with the
  ISP's Bayer stages bypassed (the bridge already delivers YUV).
- **cv181x-scaler**: wraps VPSS grp0/chn0. Its source pad format is the
  encode size; the FRC pair (src = measured input rate, dst = requested rate)
  lives here because the VPSS channel is the only stage in the vendor stack
  that actually drops frames cleanly.
- **cv181x-venc**: the capture node. `S_FMT(pixelformat)` selects the codec
  (H264 / HEVC / MJPEG); controls map to the encoder MPI (bitrate, GOP,
  force-key-frame → `CVI_VENC_RequestIDR`).

Links are IMMUTABLE|ENABLED: the hardware has exactly one path, so the graph
is documentation and format negotiation, not routing.

## Data flow and pacing (the load-bearing part)

In-kernel, only VI→VPSS is bound through the vendor bind table — the same
single edge stock uses. The encoder is fed by a kthread owned by the video
node, and the thread's gate is vb2 buffer availability:

```text
for (;;) {
    wait for a free vb2 buffer                    ← back-pressure origin
    vpss_get_chn_frame(grp0, chn0, &frm, timeout) ← pulls, VPSS sheds the rest
    CVI_VENC_SendFrame(chn0, &frm, timeout)
    CVI_VENC_GetStream(chn0, &stream, timeout)
    copy packs → vb2 buffer (Annex-B, one frame per buffer; BByFrame=1)
    CVI_VENC_ReleaseStream / vpss_release_chn_frame
    vb2_buffer_done(DONE)
}
```

If userspace stops dequeueing, the thread parks on the buffer wait, VPSS
holds at most its channel depth (2) and sheds the rest silently — identical
behavior to stock's self-limiting userspace loop, with zero error spam.

## Bring-up order (VIDIOC_STREAMON)

The exact vendor ordering, ported from the field-debugged userspace
implementation (nanokvm-app pkg/video/cvi/pipeline_setup.go):

 1. reclaim stale objects (unconditionally destroy; bounded wait)
 2. VB pools up if not already (sized for 1920x1080 NV21, 8 blocks)
 3. pad mux for the MIPI RX pads; CSI front-end clock on
 4. VI dev attr + enable (device listens before the receiver drives)
 5. snr_info block (the "sensor" description the ISP init consumes)
 6. MIPI RX: reset/attr/clock/unreset
 7. bridge subdev s_stream(1) — transmitter on, receiver configured, VI not
    yet looking
 8. VI pipe + chn (NV21, YUV bypass), ISP DMA pool
 9. VPSS grp/chn (encode size, FRC src→dst, depth 2)
10. VENC channel create
11. bind VI→VPSS — and nothing else
12. VI start pipe → VPSS start grp → VENC StartRecvFrame → feeder thread

Teardown is the exact reverse, with the encoder unwound before the channel is
destroyed (unbind → StopRecvFrame → DestroyChn — the vendor's kthread_stop
for the bind handler hides behind that order).

## Mode changes

The lt6911 poller compares measured timings; on change it queues
`V4L2_EVENT_SOURCE_CHANGE` (resolution) on the subdev and the video node.
Userspace then does the standard dance: STREAMOFF → QUERY_DV_TIMINGS →
S_DV_TIMINGS → S_FMT → STREAMON. Every STREAMOFF/ON is a full pipeline
rebuild; the vendor drivers cannot re-size a live pipe, so no attempt is made
to pretend otherwise.

## What this module does NOT do

- It does not touch the VPU decoder paths (`/dev/cvi_vc_dec*`). The NanoKVM
  stream service never decodes; the vendor decode nodes remain available.
- It does not remove the vendor char devices. The V4L2 graph is a front-end
  over the same modules; running both userspace MPI and this node at once is
  refused at STREAMON by the vendor drivers' own "resource exists" checks.
- It does not implement /dev/video for raw NV21 capture. The scaler subdev is
  where such a node would attach if wanted later.

## Why not OF-graph device tree bindings

The fully-upstream shape of this driver would declare each block as its own DT
node with `ports`/`endpoint` OF-graph links (`lontium,lt6911uxc` →
`sophgo,sg2002-isp` → `sophgo,sg2002-vpss` → `sophgo,sg2002-venc`) and let
v4l2-async assemble the graph. That shape requires each node's driver to *own
its hardware*: registers, clocks, interrupts. Here the hardware is owned by
the ported vendor drivers, probed from the DT nodes the kernel already
carries (`cvi-vi`, `cvi-vpss`, `cvi-cif`, `cvi_vc_drv` — see the
multimedia DTS patch); a second set of nodes claiming the same `reg` ranges
could not bind. Until the vendor cores are rewritten as native V4L2 drivers
— an effort mainline has not started (Sophgo's upstreaming tracker lists
Media as "Not Started") — the graph is wired internally with
IMMUTABLE|ENABLED links, which userspace observes and configures through
exactly the same media-ctl / subdev ioctls. The userspace contract is the
upstream one; only the interior plumbing is transitional.

## Dependencies on the vendor modules

New EXPORT_SYMBOL_GPL entries added where the ioctl backends already exist,
kept in one `*_ksyms.c` file per module so the delta against upstream osdrv
stays reviewable (vi_ksyms.c, vpss_ksyms.c, cvi_venc_ksyms.c). Beyond pure
linkage, four small behavior-preserving additions were needed:

- `vi_open_kernel()/vi_release_kernel()` and
  `vpss_open_kernel()/vpss_release_kernel()`: both drivers hang one-time
  init (clocks, sw init) and its undo off their char device's open count,
  and an in-kernel consumer has no file to open.
- `vi_sdk_set_vdev()` published from `vi_create_instance()`: the SDK
  backends dereference a static device pointer that only the ioctl path
  used to set.
- `vi_set_snr_info_kernel()`: the SET_SNR_INFO ioctl backend was inline in
  the ioctl switch.
- `vb_set_config_kernel()/vb_is_inited()`: the VB config path copies from
  userspace, and the init state was file-private.

CIF needs nothing: its whole ioctl surface was already registered in the
base callback table with kernel-pointer semantics (`base_exe_module_cb`
with callee `E_MODULE_CIF`).

## The bridge's hardware reset line

The stock firmware's DTB routes the LT6911's reset to PWR_GPIO1 (RTC-domain
GPIO bank "porte", pin 1, active low), declared as the cif node's
`snsr-reset` — our DTS patch mirrors it. A pulse reboots the bridge MCU from
its SPI flash (~1s) and toggles HPD to the managed host, so the `soph_v4l2`
`bridge_reset=` parameter decides when it fires:

- `recover` (default): only when the bridge stops answering i2c at
  STREAMON — pulse, wait out the firmware boot, retry once. Normal
  bring-up never touches the pin, so a viewer connecting never makes the
  host re-enumerate its monitor.
- `always`: stock semantics — pulse on every bring-up, in which case the
  TX-start/stop i2c writes are skipped entirely (a freshly booted bridge
  free-runs its transmitter, which is why stock never writes a bridge
  register).
- `never`: the pin is left alone.

## Runtime module order

Nothing autoloads. The full insertion order, dependencies first:

```text
videodev mc videobuf2-common videobuf2-v4l2 videobuf2-memops videobuf2-vmalloc
soph_sys soph_base soph_snsr_i2c soph_mipi_rx soph_vi soph_vpss
soph_vcodec soph_jpeg soph_vc_driver
soph_v4l2
```

(`modprobe soph_v4l2` resolves all of it from depmod; a loader that uses
raw `finit_module` — like nanokvm-app's pkg/video/cvi/modules.go — must add
the V4L2 core modules and soph_v4l2 to its explicit list.)

## Userspace quickstart

```sh
media-ctl -d /dev/media0 -p                     # topology
v4l2-ctl -d /dev/video0 --set-fmt-video width=1280,height=720,pixelformat=H264
v4l2-ctl -d /dev/video0 --set-parm 30
v4l2-ctl -d /dev/video0 --set-ctrl video_bitrate=4000000,video_gop_size=120
v4l2-ctl -d /dev/video0 --stream-mmap --stream-to=out.h264 --stream-count=300
```

Mode changes surface as `V4L2_EVENT_SOURCE_CHANGE`; the standard reaction is
STREAMOFF → `VIDIOC_QUERY_DV_TIMINGS` → S_FMT → STREAMON, and each
STREAMOFF/ON cycle is a full vendor pipeline rebuild.
