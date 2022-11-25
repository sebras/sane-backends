#ifndef LEXMARK_X2600_H
#define LEXMARK_X2600_H

#include "../include/sane/config.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/time.h>

#include "../include/_stdint.h"
#include "../include/sane/sane.h"
#include "../include/sane/sanei.h"
#include "../include/sane/saneopts.h"
#include "../include/sane/sanei_config.h"
#include "../include/sane/sanei_usb.h"
#include "../include/sane/sanei_backend.h"


typedef struct Read_Buffer
{
  SANE_Int gray_offset;
  SANE_Int max_gray_offset;
  SANE_Int region;
  SANE_Int red_offset;
  SANE_Int green_offset;
  SANE_Int blue_offset;
  SANE_Int max_red_offset;
  SANE_Int max_green_offset;
  SANE_Int max_blue_offset;
  SANE_Byte *data;
  SANE_Byte *readptr;
  SANE_Byte *writeptr;
  SANE_Byte *max_writeptr;
  size_t size;
  size_t linesize;
  SANE_Bool empty;
  SANE_Int image_line_no;
  SANE_Int bit_counter;
  SANE_Int max_lineart_offset;
}
Read_Buffer;


typedef enum
{
  OPT_NUM_OPTS = 0,
  OPT_MODE,
  OPT_RESOLUTION,
  OPT_PREVIEW,

  OPT_GEOMETRY_GROUP,
  OPT_TL_X,			/* top-left x */
  OPT_TL_Y,			/* top-left y */
  OPT_BR_X,			/* bottom-right x */
  OPT_BR_Y,			/* bottom-right y */

  /* must come last: */
  NUM_OPTIONS
}
Lexmark_Options;

typedef struct Lexmark_Device
{
  struct Lexmark_Device *next;
  SANE_Bool missing;	/**< devices has been unplugged or swtiched off */

  SANE_Device sane;
  SANE_Option_Descriptor opt[NUM_OPTIONS];
  Option_Value val[NUM_OPTIONS];
  SANE_Parameters params;
  SANE_Int devnum;
  long data_size;
  SANE_Bool initialized;
  SANE_Bool eof;
  SANE_Int x_dpi;
  SANE_Int y_dpi;
  long data_ctr;
  SANE_Bool device_cancelled;
  SANE_Int cancel_ctr;
  SANE_Byte *transfer_buffer;
  size_t bytes_read;
  size_t bytes_remaining;
  size_t bytes_in_buffer;
  SANE_Byte *read_pointer;
  Read_Buffer *read_buffer;
}
Lexmark_Device;


#endif /* LEXMARK_X2600_H */
