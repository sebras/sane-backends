/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Thierry HUCHARD <thierry@ordissimo.com>

   This file is part of the SANE package.

   SANE is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3 of the License, or (at your
   option) any later version.

   SANE is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with sane; see the file COPYING.
   If not, see <https://www.gnu.org/licenses/>.

   This file implements a SANE backend for eSCL scanners.  */

#define DEBUG_DECLARE_ONLY
#include "../include/sane/config.h"

#include "escl.h"

#include "../include/sane/sanei.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#include <errno.h>

#if HAVE_POPPLER_GLIB
#include <poppler/glib/poppler.h>
#endif

#include <setjmp.h>


#if HAVE_POPPLER_GLIB

#define ESCL_PDF_USE_MAPPED_FILE POPPLER_CHECK_VERSION(0,82,0)

#if ! ESCL_PDF_USE_MAPPED_FILE
static unsigned char*
set_file_in_buffer(FILE *fp, int *size)
{
	char buffer[1024] = { 0 };
    unsigned char *data = (unsigned char *)calloc(1, sizeof(char));
    int nx = 0;

    while(!feof(fp))
    {
      int n = fread(buffer,sizeof(char),1024,fp);
      unsigned char *t = realloc(data, nx + n + 1);
      if (t == NULL) {
        DBG(10, "not enough memory (realloc returned NULL)");
        free(data);
        return NULL;
      }
      data = t;
      memcpy(&(data[nx]), buffer, n);
      nx = nx + n;
      data[nx] = 0;
    }
    *size = nx;
    return data;
}
#endif

static unsigned char *
cairo_surface_to_pixels (cairo_surface_t *surface, int bps)
{
  int cairo_width, cairo_height, cairo_rowstride;
  unsigned char *data, *dst, *cairo_data;
  unsigned int *src;
  int x, y;

  cairo_width = cairo_image_surface_get_width (surface);
  cairo_height = cairo_image_surface_get_height (surface);
  cairo_rowstride = cairo_image_surface_get_stride (surface);
  cairo_data = cairo_image_surface_get_data (surface);
  data = (unsigned char*)calloc(1, sizeof(unsigned char) * (cairo_height * cairo_width * bps));

  for (y = 0; y < cairo_height; y++)
    {
      src = (unsigned int *) (cairo_data + y * cairo_rowstride);
      dst = data + y * (cairo_width * bps);
      for (x = 0; x < cairo_width; x++)
        {
          dst[0] = (*src >> 16) & 0xff;
          dst[1] = (*src >> 8) & 0xff;
          dst[2] = (*src >> 0) & 0xff;
          dst += bps;
          src++;
        }
    }
    return data;
}

SANE_Status
get_PDF_data(capabilities_t *scanner, int *width, int *height, int *bps)
{
        cairo_surface_t *cairo_surface = NULL;
        cairo_t *cr;
    PopplerPage *page;
    PopplerDocument   *doc;
    double dw, dh;
    int w, h;
    unsigned char* surface = NULL;
    SANE_Status status = SANE_STATUS_GOOD;

#if ESCL_PDF_USE_MAPPED_FILE
    GMappedFile *file;
    GBytes *bytes;

    file = g_mapped_file_new_from_fd (fileno (scanner->tmp), 0, NULL);
    if (!file) {
                DBG(10, "Error : g_mapped_file_new_from_fd");
                status =  SANE_STATUS_INVAL;
                goto close_file;
        }

    bytes = g_mapped_file_get_bytes (file);
    if (!bytes) {
                DBG(10, "Error : g_mapped_file_get_bytes");
                status =  SANE_STATUS_INVAL;
                goto free_file;
        }

    doc = poppler_document_new_from_bytes (bytes, NULL, NULL);
    if (!doc) {
                DBG(10, "Error : poppler_document_new_from_bytes");
                status =  SANE_STATUS_INVAL;
                goto free_bytes;
        }
#else
    int size = 0;
    char *data = NULL;

    data = (char*)set_file_in_buffer(scanner->tmp, &size);
    if (!data) {
                DBG(10, "Error : set_file_in_buffer");
                status =  SANE_STATUS_INVAL;
                goto close_file;
        }

    doc = poppler_document_new_from_data (data, size, NULL, NULL);
    if (!doc) {
                DBG(10, "Error : poppler_document_new_from_data");
                status =  SANE_STATUS_INVAL;
                goto free_data;
        }
#endif

    page = poppler_document_get_page (doc, 0);
    if (!page) {
                DBG(10, "Error : poppler_document_get_page");
                status =  SANE_STATUS_INVAL;
                goto free_doc;
        }

    poppler_page_get_size (page, &dw, &dh);
    dw = (double)scanner->caps[scanner->source].default_resolution * dw / 72.0;
    dh = (double)scanner->caps[scanner->source].default_resolution * dh / 72.0;
    w = (int)ceil(dw);
    h = (int)ceil(dh);
    cairo_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, w, h);
    if (!cairo_surface) {
                DBG(10, "Error : cairo_image_surface_create");
                status =  SANE_STATUS_INVAL;
                goto free_page;
        }

    cr = cairo_create (cairo_surface);
    if (!cairo_surface) {
                DBG(10, "Error : cairo_create");
                status =  SANE_STATUS_INVAL;
                goto free_surface;
        }
    cairo_scale (cr, (double)scanner->caps[scanner->source].default_resolution / 72.0,
                     (double)scanner->caps[scanner->source].default_resolution / 72.0);
    cairo_save (cr);
    poppler_page_render (page, cr);
    cairo_restore (cr);

    cairo_set_operator (cr, CAIRO_OPERATOR_DEST_OVER);
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_paint (cr);

    int st = cairo_status(cr);
    if (st)
    {
        DBG(10, "%s", cairo_status_to_string (st));
                status =  SANE_STATUS_INVAL;
        goto destroy_cr;
    }

    *bps = 3;

    DBG(10, "Escl Pdf : Image Size [%dx%d]\n", w, h);

    surface = cairo_surface_to_pixels (cairo_surface, *bps);
    if (!surface)  {
        status = SANE_STATUS_NO_MEM;
        DBG(10, "Escl Pdf : Surface Memory allocation problem");
        goto destroy_cr;
    }

    // If necessary, trim the image.
    surface = escl_crop_surface(scanner, surface, w, h, *bps, width, height);
    if (!surface)  {
        DBG(10, "Escl Pdf Crop: Surface Memory allocation problem");
        status = SANE_STATUS_NO_MEM;
    }

destroy_cr:
    cairo_destroy (cr);
free_surface:
    cairo_surface_destroy (cairo_surface);
free_page:
    g_object_unref (page);
free_doc:
    g_object_unref (doc);
#if ESCL_PDF_USE_MAPPED_FILE
free_bytes:
    g_bytes_unref (bytes);
free_file:
    g_mapped_file_unref (file);
#else
free_data:
    free(data);
#endif
close_file:
    if (scanner->tmp)
        fclose(scanner->tmp);
    scanner->tmp = NULL;
    return status;
}
#else

SANE_Status
get_PDF_data(capabilities_t __sane_unused__ *scanner,
              int __sane_unused__ *width,
              int __sane_unused__ *height,
              int __sane_unused__ *bps)
{
	return (SANE_STATUS_INVAL);
}

#endif
