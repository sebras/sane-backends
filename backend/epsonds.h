/*
 * epsonds.c - Epson ESC/I-2 driver.
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

#ifndef epsonds_h
#define epsonds_h

#undef BACKEND_NAME
#define BACKEND_NAME epsonds
#define DEBUG_NOT_STATIC

#define mode_params epsonds_mode_params
#define source_list epsonds_source_list

#include "sane/config.h"

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef NEED_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/ssl.h>
#endif

#include <string.h> /* for memset and memcpy */
#include <stdio.h>

#include "sane/sane.h"
#include "sane/sanei_backend.h"
#include "sane/sanei_debug.h"
#include "sane/sanei_usb.h"
#include "sane/sanei_jpeg.h"

#define EPSONDS_CONFIG_FILE "epsonds.conf"

#ifndef PATH_MAX
#define PATH_MAX (1024)
#endif

#ifndef XtNumber
#define XtNumber(x)  (sizeof(x) / sizeof(x[0]))
#define XtOffset(p_type, field)  ((size_t)&(((p_type)NULL)->field))
#define XtOffsetOf(s_type, field)  XtOffset(s_type*, field)
#endif

#define ACK	0x06
#define NAK	0x15
#define	FS	0x1C

#define FBF_STR SANE_I18N("Flatbed")
#define TPU_STR SANE_I18N("Transparency Unit")
#define ADF_STR SANE_I18N("Automatic Document Feeder")

#define STRING_FLATBED SANE_I18N("Flatbed")
#define STRING_ADFFRONT SANE_I18N("ADF Front")
#define STRING_ADFDUPLEX SANE_I18N("ADF Duplex")

enum {
	OPT_NUM_OPTS = 0,
	OPT_STANDARD_GROUP,
	OPT_SOURCE,
	OPT_MODE,
	OPT_DEPTH,
	OPT_RESOLUTION,
	OPT_GEOMETRY_GROUP,
	OPT_TL_X,
	OPT_TL_Y,
	OPT_BR_X,
	OPT_BR_Y,
	OPT_EQU_GROUP,
	OPT_EJECT,
	OPT_LOAD,
	OPT_ADF_SKEW,
	OPT_ADF_CRP,
	NUM_OPTIONS
};

typedef enum
{	/* hardware connection to the scanner */
	SANE_EPSONDS_NODEV,	/* default, no HW specified yet */
	SANE_EPSONDS_USB,	/* USB interface */
	SANE_EPSONDS_NET	/* network interface */
} epsonds_conn_type;

/* hardware description */

struct epsonds_device
{
	struct epsonds_device *next;

	epsonds_conn_type connection;

	char *name;
	char *model;

	unsigned int model_id;

	SANE_Device sane;
	SANE_Range *x_range;
	SANE_Range *y_range;
	SANE_Range dpi_range;
	SANE_Byte alignment;


	SANE_Int *res_list;		/* list of resolutions */
	SANE_Int *depth_list;
	SANE_Int max_depth;		/* max. color depth */

	SANE_Bool has_raw;		/* supports RAW format */

	SANE_Bool has_mono;  /*supprt M001*/

	SANE_Bool has_fb;		/* flatbed */
	SANE_Range fbf_x_range;	        /* x range */
	SANE_Range fbf_y_range;	        /* y range */
	SANE_Byte fbf_alignment;	/* left, center, right */
	SANE_Bool fbf_has_skew;		/* supports skew correction */

	SANE_Bool has_adf;		/* adf */
	SANE_Range adf_x_range;	        /* x range */
	SANE_Range adf_y_range;	        /* y range */
	SANE_Bool adf_is_duplex;	/* supports duplex mode */
	SANE_Bool adf_singlepass;	/* supports single pass duplex */
	SANE_Bool adf_has_skew;		/* supports skew correction */
	SANE_Bool adf_has_load;		/* supports load command */
	SANE_Bool adf_has_eject;	/* supports eject command */
	SANE_Byte adf_alignment;	/* left, center, right */
	SANE_Byte adf_has_dfd;		/* supports double feed detection */

	SANE_Byte adf_has_crp;		/* supports crp */

	SANE_Bool has_tpu;		/* tpu */
	SANE_Range tpu_x_range;	        /* transparency unit x range */
	SANE_Range tpu_y_range;	        /* transparency unit y range */

	SANE_Int lut_id;
	SANE_Bool has_hardware_lut;
};

typedef struct epsonds_device epsonds_device;

typedef struct ring_buffer
{
	SANE_Byte *ring, *wp, *rp, *end;
	SANE_Int fill, size;

} ring_buffer;
#ifdef HAVE_OPENSSL
typedef struct crypt_context {
    SSL_CTX *ssl;
    BIO *bio;
} crypt_context;
#endif

/* an instance of a scanner */

struct epsonds_scanner
{
	struct epsonds_scanner *next;
	struct epsonds_device *hw;

	int fd;

	SANE_Option_Descriptor opt[NUM_OPTIONS];
	Option_Value val[NUM_OPTIONS];
	SANE_Parameters params;

	size_t bsz;		/* transfer buffer size */
	SANE_Byte *buf, *line_buffer;
	ring_buffer *current, front, back;

	SANE_Bool eof, scanning, canceling, locked, backside, mode_jpeg;

	SANE_Int left, top, pages, dummy;

	SANE_Int width_front, height_front;
	SANE_Int width_back , height_back;
	SANE_Int width_temp, height_temp;

	/* jpeg stuff */

	djpeg_dest_ptr jdst;
	struct jpeg_decompress_struct jpeg_cinfo;
	struct jpeg_error_mgr jpeg_err;
	SANE_Bool jpeg_header_seen;

	/* network buffers */
	unsigned char *netbuf, *netptr;
	size_t netlen;

	SANE_Byte *frontJpegBuf, *backJpegBuf;
	SANE_Int   frontJpegBufLen, backJpegBufLen;
	SANE_Int   acquirePage;

	SANE_Int   isflatbedScan;
	SANE_Int   isDuplexScan;

	SANE_Int   needToConvertBW;

	SANE_Int   scanEnd;
#ifdef HAVE_OPENSSL
	crypt_context *cryptContext;
#endif

 };

typedef struct epsonds_scanner epsonds_scanner;

struct mode_param
{
	int color;
	int flags;
	int dropout_mask;
	int depth;
};

enum {
	MODE_BINARY, MODE_GRAY, MODE_COLOR
};

#endif
