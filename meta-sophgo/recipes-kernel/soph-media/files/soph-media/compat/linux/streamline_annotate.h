/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Stub for ARM Streamline's kernel annotation header.
 *
 * The vendor 5.10 tree carried include/linux/streamline_annotate.h from ARM's
 * gator/Streamline profiler. It is a profiling aid only -- the annotations mark
 * regions on a Streamline timeline and have no functional effect -- and it does
 * not exist in mainline.
 *
 * Only four macros are used across vi/, vcodec/ and jpeg/, all of them
 * ANNOTATE_* channel markers, so they compile away to nothing here. If anyone
 * ever wants Streamline traces on this board, drop ARM's real header in ahead
 * of this one on the include path.
 */

#ifndef __STREAMLINE_ANNOTATE_STUB_H__
#define __STREAMLINE_ANNOTATE_STUB_H__

#define ANNOTATE_DEFINE			do { } while (0)
#define ANNOTATE_SETUP			do { } while (0)
#define ANNOTATE_GREEN			0
#define ANNOTATE_NAME_CHANNEL(ch, group, str)	do { } while (0)
#define ANNOTATE_CHANNEL_COLOR(ch, color, str)	do { } while (0)
#define ANNOTATE_CHANNEL_END(ch)		do { } while (0)

#endif /* __STREAMLINE_ANNOTATE_STUB_H__ */
