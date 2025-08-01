/*
 * epsonds-ops.c - Epson ESC/I-2 driver, support routines.
 *
 * Copyright (C) 2015 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * This file is part of the SANE package.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 */

#define DEBUG_DECLARE_ONLY

#include "sane/config.h"

#include <unistd.h>		/* sleep */
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#include "epsonds.h"
#include "epsonds-io.h"
#include "epsonds-ops.h"
#include "epsonds-cmd.h"

extern struct mode_param mode_params[];

/* Define the different scan sources */
#define STRING_FLATBED SANE_I18N("Flatbed")
#define STRING_ADFFRONT SANE_I18N("ADF Front")
#define STRING_ADFDUPLEX SANE_I18N("ADF Duplex")

extern SANE_String_Const source_list[];

void
eds_dev_init(epsonds_device *dev)
{
	dev->res_list = malloc(sizeof(SANE_Word));
	dev->res_list[0] = 0;

	dev->depth_list = malloc(sizeof(SANE_Word));
	dev->depth_list[0] = 0;
}

SANE_Status
eds_dev_post_init(struct epsonds_device *dev)
{
	SANE_String_Const *source_list_add = source_list;

	DBG(10, "%s\n", __func__);

	if (dev->has_fb)
		*source_list_add++ = STRING_FLATBED;

	if (dev->has_adf)
		*source_list_add++ = STRING_ADFFRONT;

	if (dev->adf_is_duplex)
		*source_list_add++ = STRING_ADFDUPLEX;

	if (source_list[0] == 0
		|| (dev->res_list[0] == 0 && dev->dpi_range.min == 0)
		|| dev->depth_list[0] == 0) {

		DBG(1, "something is wrong in the discovery process, aborting.\n");
		DBG(1, "sources: %ld, res: %d, depths: %d.\n",
			source_list_add - source_list, dev->res_list[0], dev->depth_list[0]);

		return SANE_STATUS_INVAL;
	}

	return SANE_STATUS_GOOD;
}

SANE_Bool
eds_is_model(epsonds_device *dev, const char *model)
{
        if (dev->model == NULL)
                return SANE_FALSE;

        if (strncmp(dev->model, model, strlen(model)) == 0)
                return SANE_TRUE;

        return SANE_FALSE;
}

SANE_Status
eds_add_resolution(epsonds_device *dev, int r)
{
	DBG(10, "%s: add (dpi): %d\n", __func__, r);

	/* first element is the list size */
	dev->res_list[0]++;
	dev->res_list = realloc(dev->res_list,
						(dev->res_list[0] + 1) *
						sizeof(SANE_Word));
	if (dev->res_list == NULL)
		return SANE_STATUS_NO_MEM;

	dev->res_list[dev->res_list[0]] = r;

	return SANE_STATUS_GOOD;
}

SANE_Status
eds_set_resolution_range(epsonds_device *dev, int min, int max)
{
	DBG(10, "%s: set min/max (dpi): %d/%d\n", __func__, min, max);

	dev->dpi_range.min = min;
	dev->dpi_range.max = max;
	dev->dpi_range.quant = 1;

	return SANE_STATUS_GOOD;
}

void
eds_set_fbf_area(epsonds_device *dev, int x, int y, int unit)
{
	if (x == 0 || y == 0)
		return;

	dev->fbf_x_range.min = 0;
	dev->fbf_x_range.max = SANE_FIX(x * MM_PER_INCH / unit);
	dev->fbf_x_range.quant = 0;

	dev->fbf_y_range.min = 0;
	dev->fbf_y_range.max = SANE_FIX(y * MM_PER_INCH / unit);
	dev->fbf_y_range.quant = 0;

	DBG(5, "%s: %f,%f %f,%f %d [mm]\n",
	    __func__,
	    SANE_UNFIX(dev->fbf_x_range.min),
	    SANE_UNFIX(dev->fbf_y_range.min),
	    SANE_UNFIX(dev->fbf_x_range.max),
	    SANE_UNFIX(dev->fbf_y_range.max), unit);
}

void
eds_set_adf_area(struct epsonds_device *dev, int x, int y, int unit)
{
	dev->adf_x_range.min = 0;
	dev->adf_x_range.max = SANE_FIX(x * MM_PER_INCH / unit);
	dev->adf_x_range.quant = 0;

	dev->adf_y_range.min = 0;
	dev->adf_y_range.max = SANE_FIX(y * MM_PER_INCH / unit);
	dev->adf_y_range.quant = 0;

	DBG(5, "%s: %f,%f %f,%f %d [mm]\n",
	    __func__,
	    SANE_UNFIX(dev->adf_x_range.min),
	    SANE_UNFIX(dev->adf_y_range.min),
	    SANE_UNFIX(dev->adf_x_range.max),
	    SANE_UNFIX(dev->adf_y_range.max), unit);
}

void
eds_set_tpu_area(struct epsonds_device *dev, int x, int y, int unit)
{
	dev->tpu_x_range.min = 0;
	dev->tpu_x_range.max = SANE_FIX(x * MM_PER_INCH / unit);
	dev->tpu_x_range.quant = 0;

	dev->tpu_y_range.min = 0;
	dev->tpu_y_range.max = SANE_FIX(y * MM_PER_INCH / unit);
	dev->tpu_y_range.quant = 0;

	DBG(5, "%s: %f,%f %f,%f %d [mm]\n",
	    __func__,
	    SANE_UNFIX(dev->tpu_x_range.min),
	    SANE_UNFIX(dev->tpu_y_range.min),
	    SANE_UNFIX(dev->tpu_x_range.max),
	    SANE_UNFIX(dev->tpu_y_range.max), unit);
}

SANE_Status
eds_add_depth(epsonds_device *dev, SANE_Word depth)
{
	DBG(5, "%s: add (bpp): %d\n", __func__, depth);

	/* > 8bpp not implemented yet */
	if (depth > 8) {
		DBG(1, " not supported");
		return SANE_STATUS_GOOD;
	}

	if (depth > dev->max_depth)
		dev->max_depth = depth;

	/* first element is the list size */
	dev->depth_list[0]++;
	dev->depth_list = realloc(dev->depth_list,
						(dev->depth_list[0] + 1) *
						sizeof(SANE_Word));

	if (dev->depth_list == NULL)
		return SANE_STATUS_NO_MEM;

	dev->depth_list[dev->depth_list[0]] = depth;

	return SANE_STATUS_GOOD;
}

SANE_Status
eds_init_parameters(epsonds_scanner *s)
{
	int dpi, bytes_per_pixel;

	memset(&s->params, 0, sizeof(SANE_Parameters));

	/* setup depth according to our mode table */
	if (mode_params[s->val[OPT_MODE].w].depth == 1)
		s->params.depth = 1;
	else
		s->params.depth = s->val[OPT_DEPTH].w;

	dpi = s->val[OPT_RESOLUTION].w;

	if (SANE_UNFIX(s->val[OPT_BR_Y].w) == 0 ||
		SANE_UNFIX(s->val[OPT_BR_X].w) == 0)
		return SANE_STATUS_INVAL;

	s->left = ((SANE_UNFIX(s->val[OPT_TL_X].w) / MM_PER_INCH) *
		s->val[OPT_RESOLUTION].w) + 0.5;

	s->top = ((SANE_UNFIX(s->val[OPT_TL_Y].w) / MM_PER_INCH) *
		s->val[OPT_RESOLUTION].w) + 0.5;

	s->params.pixels_per_line =
		((SANE_UNFIX(s->val[OPT_BR_X].w -
			   s->val[OPT_TL_X].w) / MM_PER_INCH) * dpi) + 0.5;
	s->params.lines =
		((SANE_UNFIX(s->val[OPT_BR_Y].w -
			   s->val[OPT_TL_Y].w) / MM_PER_INCH) * dpi) + 0.5;

	DBG(5, "%s: tlx %f tly %f brx %f bry %f [mm]\n",
	    __func__,
	    SANE_UNFIX(s->val[OPT_TL_X].w), SANE_UNFIX(s->val[OPT_TL_Y].w),
	    SANE_UNFIX(s->val[OPT_BR_X].w), SANE_UNFIX(s->val[OPT_BR_Y].w));

	DBG(5, "%s: tlx %d tly %d brx %d bry %d [dots @ %d dpi]\n",
		__func__, s->left, s->top,
		s->params.pixels_per_line, s->params.lines, dpi);

	/* center aligned? */
	if (s->hw->alignment == 1) {

		SANE_Int offset = ((SANE_UNFIX(s->hw->x_range->max) / MM_PER_INCH) * dpi) + 0.5;

		s->left += ((offset - s->params.pixels_per_line) / 2);

		DBG(5, "%s: centered to tlx %d tly %d brx %d bry %d [dots @ %d dpi]\n",
			__func__, s->left, s->top,
			s->params.pixels_per_line, s->params.lines, dpi);
	}

	/* right aligned? */
	if (s->hw->alignment == 2) {

		SANE_Int offset = ((SANE_UNFIX(s->hw->x_range->max) / MM_PER_INCH) * dpi) + 0.5;

		s->left += ((offset - s->params.pixels_per_line) );

		DBG(5, "%s: right-aligned to tlx %d tly %d brx %d bry %d [dots @ %d dpi]\n",
			__func__, s->left, s->top,
			s->params.pixels_per_line, s->params.lines, dpi);
	}
	/*
	 * Calculate bytes_per_pixel and bytes_per_line for
	 * any color depths.
	 *
	 * The default color depth is stored in mode_params.depth:
	 */

	/* this works because it can only be set to 1, 8 or 16 */
	bytes_per_pixel = s->params.depth / 8;
	if (s->params.depth % 8) {	/* just in case ... */
		bytes_per_pixel++;
	}

	/* pixels_per_line is rounded to the next 8bit boundary */
	s->params.pixels_per_line = s->params.pixels_per_line & ~7;

	s->params.last_frame = SANE_TRUE;

	switch (s->val[OPT_MODE].w) {
	case MODE_BINARY:
	case MODE_GRAY:
		s->params.format = SANE_FRAME_GRAY;
		s->params.bytes_per_line =
			s->params.pixels_per_line * s->params.depth / 8;
		break;
	case MODE_COLOR:
		s->params.format = SANE_FRAME_RGB;
		s->params.bytes_per_line =
			3 * s->params.pixels_per_line * bytes_per_pixel;
		break;
	}

	if (s->params.bytes_per_line == 0) {
		DBG(1, "bytes_per_line is ZERO\n");
		return SANE_STATUS_INVAL;
	}

	/*
	 * If (s->top + s->params.lines) is larger than the max scan area, reset
	 * the number of scan lines:
	 * XXX: precalculate the maximum scanning area elsewhere (use dev max_y)
	 */

	if (SANE_UNFIX(s->val[OPT_BR_Y].w) / MM_PER_INCH * dpi <
	    (s->params.lines + s->top)) {
		s->params.lines =
			((int) SANE_UNFIX(s->val[OPT_BR_Y].w) / MM_PER_INCH *
			 dpi + 0.5) - s->top;
	}

	if (s->params.lines <= 0) {
		DBG(1, "wrong number of lines: %d\n", s->params.lines);
		return SANE_STATUS_INVAL;
	}

	return SANE_STATUS_GOOD;
}
#define min(A,B) (((A)<(B)) ? (A) : (B))

void
eds_copy_image_from_ring(epsonds_scanner *s, SANE_Byte *data, SANE_Int max_length,
                   SANE_Int *length)
{
	int lines, available;
	int hw_line_size = (s->params.bytes_per_line + s->dummy);

	available = eds_ring_avail(s->current);
	if (max_length > available)
		max_length = available;

	lines = min(max_length / s->params.bytes_per_line, available / hw_line_size);

	DBG(18, "copying %d lines (%d, %d, %d)\n", lines, s->params.bytes_per_line, s->dummy, s->params.depth);

	/* need more data? */
	if (lines == 0) {
		*length = 0;
		return;
	}

	*length = (lines * s->params.bytes_per_line);

	/* we need to copy one line at time, skipping
	 * dummy bytes at the end of each line
	 */

	/* lineart */
	if (s->params.depth == 1) {

		while (lines--) {

			int i;
			SANE_Byte *p;

			eds_ring_read(s->current, s->line_buffer, s->params.bytes_per_line);
			eds_ring_skip(s->current, s->dummy);

			p = s->line_buffer;

			for (i = 0; i < s->params.bytes_per_line; i++) {
				*data++ = ~*p++;
			}
		}

	} else { /* gray and color */

		while (lines--) {

			eds_ring_read(s->current, data, s->params.bytes_per_line);
			eds_ring_skip(s->current, s->dummy);

			data += s->params.bytes_per_line;

		}
	}
}

SANE_Status eds_ring_init(ring_buffer *ring, SANE_Int size)
{
	ring->ring = realloc(ring->ring, size);
	if (!ring->ring) {
		return SANE_STATUS_NO_MEM;
	}

	ring->size = size;
	ring->fill = 0;
	ring->end = ring->ring + size;
	ring->wp = ring->rp = ring->ring;

	return SANE_STATUS_GOOD;
}

SANE_Status eds_ring_write(ring_buffer *ring, SANE_Byte *buf, SANE_Int size)
{
	SANE_Int tail;

	if (size > (ring->size - ring->fill)) {
		DBG(1, "ring buffer full, requested: %d, available: %d\n", size, ring->size - ring->fill);
		return SANE_STATUS_NO_MEM;
	}

	tail = ring->end - ring->wp;
	if (size < tail) {

		memcpy(ring->wp, buf, size);

		ring->wp += size;
		ring->fill += size;

	} else {

		memcpy(ring->wp, buf, tail);
		size -= tail;

		ring->wp = ring->ring;
		memcpy(ring->wp, buf + tail, size);

		ring->wp += size;
		ring->fill += (tail + size);
	}

	return SANE_STATUS_GOOD;
}

SANE_Int eds_ring_read(ring_buffer *ring, SANE_Byte *buf, SANE_Int size)
{
	SANE_Int tail;

	DBG(18, "reading from ring, %d bytes available\n", (int)ring->fill);

	/* limit read to available */
	if (size > ring->fill) {
		DBG(1, "not enough data in the ring, shouldn't happen\n");
		size = ring->fill;
	}

	tail = ring->end - ring->rp;
	if (size < tail) {

		memcpy(buf, ring->rp, size);

		ring->rp += size;
		ring->fill -= size;

		return size;

	} else {

		memcpy(buf, ring->rp, tail);
		size -= tail;

		ring->rp = ring->ring;
		memcpy(buf + tail, ring->rp, size);

		ring->rp += size;
		ring->fill -= (size + tail);

		return size + tail;
	}
}

SANE_Int eds_ring_skip(ring_buffer *ring, SANE_Int size)
{
	SANE_Int tail;
	/* limit skip to available */
	if (size > ring->fill)
		size = ring->fill;

	tail = ring->end - ring->rp;
	if (size < tail) {
		ring->rp += size;
	} else {

		ring->rp = ring->ring + (size - tail);
	}

	ring->fill -= size;

	return size;
}

SANE_Int eds_ring_avail(ring_buffer *ring)
{
	return ring->fill;
}

void eds_ring_flush(ring_buffer *ring)
{
	eds_ring_skip(ring, ring->fill);
}

void eds_ring_destory(ring_buffer *ring)
{
	if (ring->ring)
	{
		free(ring->ring);
		ring->ring = NULL;
	}
}
