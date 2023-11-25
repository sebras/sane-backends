#include "lexmark_x2600.h"

#define BUILD 1
#define LEXMARK_X2600_CONFIG_FILE "lexmark_x2600.conf"
#define MAX_OPTION_STRING_SIZE 255

static Lexmark_Device *first_device = 0;
static SANE_Int num_devices = 0;
static const SANE_Device **devlist = 0;

static SANE_Bool initialized = SANE_FALSE;

static SANE_Int dpi_list[] = {
  5, 75, 100, 200, 300, 600
};

static SANE_String_Const mode_list[] = {
  SANE_VALUE_SCAN_MODE_COLOR,
  SANE_VALUE_SCAN_MODE_GRAY,
  NULL
};

static SANE_Range x_range = {
  0,				/* minimum */
  5078,				/* maximum */
  1				/* quantization */
};

static SANE_Range y_range = {
  0,				/* minimum */
  7015,				/* maximum */
  1				/* quantization */
};

static SANE_Byte command1_block[] = {
  0xA5, 0x00, 0x19, 0x10, 0x01, 0x83, 0xAA, 0xBB,
  0xCC, 0xDD, 0x02, 0x00, 0x1B, 0x53, 0x03, 0x00,
  0x00, 0x00, 0x80, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,
  0xAA, 0xBB, 0xCC, 0xDD};
static SANE_Int command1_block_size = sizeof(command1_block);

static SANE_Byte command2_block[] = {
  0xA5, 0x00, 0x19, 0x10, 0x01, 0x83, 0xAA, 0xBB,
  0xCC, 0xDD, 0x02, 0x00, 0x1B, 0x53, 0x04, 0x00,
  0x00, 0x00, 0x80, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,
  0xAA, 0xBB, 0xCC, 0xDD};
static SANE_Int command2_block_size = sizeof(command2_block);

static SANE_Byte command_with_params_block[] = {
  0xA5, 0x00, 0x31, 0x10, 0x01, 0x83, 0xAA, 0xBB,
  0xCC, 0xDD, 0x02, 0x00, 0x1B, 0x53, 0x05, 0x00,
  0x18, 0x00, 0x80, 0x00, 0xFF, 0x00, 0x00, 0x02,
  0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0xBB, 0xCC, 0xDD,
  0xAA, 0xBB, 0xCC, 0xDD};
static SANE_Int command_with_params_block_size = sizeof(command_with_params_block);

static SANE_Byte command_cancel1_block[] = {
  0xa5, 0x00, 0x19, 0x10, 0x01, 0x83, 0xaa, 0xbb,
  0xcc, 0xdd, 0x02, 0x00, 0x1b, 0x53, 0x0f, 0x00,
  0x00, 0x00, 0x00, 0x00, 0xaa, 0xbb, 0xcc, 0xdd,
  0xaa, 0xbb, 0xcc, 0xdd};
static SANE_Byte command_cancel2_block[] = {
  0xa5, 0x00, 0x19, 0x10, 0x01, 0x83, 0xaa, 0xbb,
  0xcc, 0xdd, 0x02, 0x00, 0x1b, 0x53, 0x06, 0x00,
  0x00, 0x00, 0x80, 0x00, 0xaa, 0xbb, 0xcc, 0xdd,
  0xaa, 0xbb, 0xcc, 0xdd};
static SANE_Int command_cancel_size = sizeof(command_cancel1_block);

static SANE_Byte unknnown_d_data_packet[] = {
  0x1b, 0x53, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00};
static SANE_Int unknnown_d_data_packet_size = sizeof(unknnown_d_data_packet);

static SANE_Byte empty_line_data_packet[] = {
  0x1b, 0x53, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x00};
static SANE_Int empty_line_data_packet_size = sizeof(empty_line_data_packet);

static SANE_Byte last_data_packet[] = {
  0x1b, 0x53, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x01};
static SANE_Int last_data_packet_size = sizeof(last_data_packet);

static SANE_Byte cancel_packet[] = {
  0x1b, 0x53, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x03};
static SANE_Int cancel_packet_size = sizeof(cancel_packet);

static SANE_Byte linebegin_data_packet[] = {
  0x1b, 0x53, 0x02, 0x00};
static SANE_Int linebegin_data_packet_size = sizeof(linebegin_data_packet);

static SANE_Byte unknnown_a_data_packet[] = {
  0x1b, 0x53, 0x04, 0x00, 0x00, 0x00, 0x80, 0x00};
static SANE_Int unknnown_a_data_packet_size = sizeof(unknnown_a_data_packet);

static SANE_Byte unknnown_c_data_packet[] = {
  0x1b, 0x53, 0x05, 0x00, 0x00, 0x00};
static SANE_Int unknnown_c_data_packet_size = sizeof(unknnown_c_data_packet);

static SANE_Byte unknnown_b_data_packet[] = {
  0xa5, 0x00, 0x06, 0x10, 0x01, 0xaa, 0xbb, 0xcc,
  0xdd};
static SANE_Int unknnown_b_data_packet_size = sizeof(unknnown_b_data_packet);

static SANE_Int  line_header_length = 9;


//static SANE_Byte empty_data_packet[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void
clean_and_copy_data(const SANE_Byte * source, SANE_Int source_size,
                    SANE_Byte * destination, SANE_Int * destination_length,
                    SANE_Int mode, SANE_Int max_length, SANE_Handle dev)
{

  // if source doesnt start with 1b 53 02, then it is a continuation packet
  if (memcmp(empty_line_data_packet, source, empty_line_data_packet_size) == 0){
    return;
  }
  if (memcmp(unknnown_a_data_packet, source, unknnown_a_data_packet_size) == 0){
    return;
  }
  if (memcmp(unknnown_b_data_packet, source, unknnown_b_data_packet_size) == 0){
    return;
  }
  if (memcmp(unknnown_c_data_packet, source, unknnown_c_data_packet_size) == 0){
    return;
  }
  if (memcmp(unknnown_d_data_packet, source, unknnown_d_data_packet_size) == 0){
    return;
  }

  // SANE_Int k = 0;
  // SANE_Int bytes_written = 0;
  // BW    1b 53 02 00 21 00 00 00 00  |   32 |   21 ->    33 (segmentlng=   32)
  // BW    1b 53 02 00 41 00 00 00 00  |   64 |   41 ->    65 (segmentlng=   64)
  // COLOR 1b 53 02 00 c1 00 00 00 00  |   64 |   c1 ->   193 (segmentlng=  192)
  // COLOR 1b 53 02 00 01 06 00 00 00  |  512 |  601 ->  1537 (segmentlng= 1536)
  // COLOR 1b 53 02 00 99 3a 00 00 00  | 5000 | 3a99 -> 15001 (segmentlng=15000)
  // COLOR 1b 53 02 00 f7 0f 00        | 1362 | 0ff7 ->  4087 <- limit where sane_read can a read a line at e time, more that 1362 and then the rest
  //                                                             of the line will be available in the next sane_read call
  // COLOR 1b 53 02 00 fa 0f 00        |      | 0ffa ->  4090 <- in that case the line doesnt fit, clean_and_copy_data will be called again with the rest of the data


  // edge case segment doesn(t feet in the packet size
  /* if(segment_length > source_size - 9) */
  /*   segment_length = source_size - 9; */

  DBG (10, "source = %02hhx %02hhx %02hhx %02hhx | %02hhx %02hhx %02hhx %02hhx \n",
       source[0], source[1], source[2], source[3], source[4], source[5], source[6], source[7]);
  DBG (10, "source = %02hhx %02hhx %02hhx %02hhx | %02hhx %02hhx %02hhx %02hhx \n",
       source[8], source[9], source[10], source[11], source[12], source[13], source[14], source[15]);
  int debug_offset = 4092;
  if(source_size > debug_offset){
    DBG (10, "source = %02hhx %02hhx %02hhx %02hhx | %02hhx %02hhx %02hhx %02hhx \n",
         source[source_size-16-debug_offset],
         source[source_size-15-debug_offset],
         source[source_size-14-debug_offset],
         source[source_size-13-debug_offset],
         source[source_size-12-debug_offset],
         source[source_size-11-debug_offset],
         source[source_size-10-debug_offset],
         source[source_size-9]-debug_offset);
    DBG (10, "source = %02hhx %02hhx %02hhx %02hhx | %02hhx %02hhx %02hhx %02hhx \n",
         source[source_size-8-debug_offset],
         source[source_size-7-debug_offset],
         source[source_size-6-debug_offset],
         source[source_size-5-debug_offset],
         source[source_size-4-debug_offset],
         source[source_size-3-debug_offset],
         source[source_size-2-debug_offset],
         source[source_size-1-debug_offset]);
  }

  DBG (10, "\n");

  // the scanner sends series of 8 lines function param source
  // every lines has prefix see linebegin_data_packet
  // the source parameter as a limited length :function param source_size
  // so the serie og 8 lines can be splited
  // in such case, in the next call of this function, source contain the end of the
  // broken segment.
  // Here is the way data is read:
  // 1 - check that source begin with a linebegin_data_packet signature
  //     if this is the case the source[4] & source[5] contains how much data
  //     can be read before onother header is reach (linebegin_data_packet)

  Lexmark_Device * ldev = (Lexmark_Device * ) dev;
  SANE_Int i = 0;
  SANE_Int bytes_read = 0;
  SANE_Byte tmp = 0;
  SANE_Int source_read_cursor = 0;
  SANE_Int block_pixel_data_length = 0;
  SANE_Byte* alloc_result;
  SANE_Int size_to_realloc = 0;
  
  // does source start with linebegin_data_packet?
  if (memcmp(linebegin_data_packet, source, linebegin_data_packet_size) == 0){
    // extract the number of bytes we can read befor new header is reached
    // store it in the device in case of continuation packet
    ldev->read_buffer->linesize = (source[4] + ((source[5] << 8) & 0xFF00)) - 1;
    ldev->read_buffer->last_line_bytes_read = ldev->read_buffer->linesize;
    DBG (20, "this is the begining of a line linesize=%ld\n",
         ldev->read_buffer->linesize);
  } else {
    DBG (20, "this is not a new line packet, continue to fill the read buffer\n");
    //return;
  }

  // loop over source buffer
  while(i < source_size){
    // last line was full
    if(ldev->read_buffer->last_line_bytes_read == ldev->read_buffer->linesize){
      // if next block fit in the source
      if(i + line_header_length + ldev->read_buffer->linesize <= source_size){
        ldev->read_buffer->image_line_no += 1;
        source_read_cursor = i + line_header_length;
        block_pixel_data_length = ldev->read_buffer->linesize;
        ldev->read_buffer->last_line_bytes_read = block_pixel_data_length;
        size_to_realloc = ldev->read_buffer->image_line_no *
          ldev->read_buffer->linesize * sizeof(SANE_Byte);
        bytes_read = block_pixel_data_length + line_header_length;
      }
      // next block cannot be read fully because source_size is too small
      // (USB packet fragmentation)
      else{
        ldev->read_buffer->image_line_no += 1;
        source_read_cursor = i + line_header_length;
        block_pixel_data_length = source_size - i - line_header_length;
        ldev->read_buffer->last_line_bytes_read = block_pixel_data_length;
        size_to_realloc = ((ldev->read_buffer->image_line_no-1) *
          ldev->read_buffer->linesize + block_pixel_data_length) * sizeof(SANE_Byte);
        bytes_read = block_pixel_data_length + line_header_length;
      }
    }
    // last line was not full lets extract what is left
    // this is du to USB packet fragmentation
    else{
      // the last line was not full so no increment
      ldev->read_buffer->image_line_no += 0;
      source_read_cursor = i;
      block_pixel_data_length = ldev->read_buffer->linesize -
        ldev->read_buffer->last_line_bytes_read;
      // we completed the last line with missing bytes so new the line is full
      ldev->read_buffer->last_line_bytes_read = ldev->read_buffer->linesize;
      size_to_realloc = ldev->read_buffer->image_line_no *
        ldev->read_buffer->linesize * sizeof(SANE_Byte);
      bytes_read = block_pixel_data_length;
    }  

    DBG (20, "size_to_realloc=%d i=%d image_line_no=%d\n",
         size_to_realloc, i, ldev->read_buffer->image_line_no);
    // do realoc memory space for our buffer
    SANE_Byte* alloc_result = realloc(ldev->read_buffer->data, size_to_realloc);
    if(alloc_result == NULL){
      // TODO allocation was not possible
      DBG (20, "REALLOC failed\n");
    }
    // point data to our new memary space
    ldev->read_buffer->data = alloc_result;
    // reposition writeptr and readptr to the correct memory adress
    // to do that use write_byte_counter and read_byte_counter
    ldev->read_buffer->writeptr =
      ldev->read_buffer->data + ldev->read_buffer->write_byte_counter;
    ldev->read_buffer->readptr =
      ldev->read_buffer->data + ldev->read_buffer->read_byte_counter;
    // copy new data
    memcpy(
           ldev->read_buffer->writeptr,
           source + source_read_cursor,
           block_pixel_data_length
    );
    
    // store how long is the buffer
    ldev->read_buffer->write_byte_counter += block_pixel_data_length;

    i += bytes_read;
  }

  
  // read our buffer to fill the destination buffer
  // mulitple call so read may has been already started
  // length already read is stored in ldev->read_buffer->read_byte_counter
  DBG (20, "##### source read done now sending to destination\n");

  // we will copy image line by image line
  // this avoid error on color channels swapping
  SANE_Int available_bytes_to_read =
    ldev->read_buffer->write_byte_counter - ldev->read_buffer->read_byte_counter;
  SANE_Int offset = 0;
  //*destination_length = 0;
  i = 0;
  while(available_bytes_to_read >= ldev->read_buffer->linesize){
    DBG (20, "    i=%d destination_length=%d\n", i, *destination_length);
    offset = i*ldev->read_buffer->linesize;

    SANE_Byte * color_swarp_ptr = ldev->read_buffer->readptr + offset;
    // we have to invert color channels
    if (mode == SANE_FRAME_RGB){
      for(SANE_Int j=0; j < ldev->read_buffer->linesize;j += 3)
        {
          // DBG (20, "  swapping RGB <- BGR j=%d\n", j);
          tmp = *(color_swarp_ptr + j);
          *(color_swarp_ptr + j) = *(color_swarp_ptr + j + 2);
          *(color_swarp_ptr + j + 2) = tmp;
        }
    }

    
    memcpy (
      destination + offset,
      ldev->read_buffer->readptr + offset,
      ldev->read_buffer->linesize
    );
    ldev->read_buffer->read_byte_counter += ldev->read_buffer->linesize;
    //ldev->read_buffer->readptr += bytes_to_read;
    
    available_bytes_to_read =
      ldev->read_buffer->write_byte_counter - ldev->read_buffer->read_byte_counter;
    i++;
  }

  *destination_length = ldev->read_buffer->linesize * i;  
}

SANE_Status
usb_write_then_read (Lexmark_Device * dev, SANE_Byte * cmd, size_t cmd_size)
{
  size_t buf_size = 256;
  SANE_Byte buf[buf_size];
  SANE_Status status;

  DBG (10, "usb_write_then_read: %d\n", dev->devnum);
  sanei_usb_set_endpoint(dev->devnum, USB_DIR_OUT|USB_ENDPOINT_TYPE_BULK, 0x02);
  status = sanei_usb_write_bulk (dev->devnum, cmd, &cmd_size);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "USB WRITE IO Error in usb_write_then_read, launch fail: %d\n",
           status);
      return status;
    }

  status = sanei_usb_read_bulk (dev->devnum, buf, &buf_size);
  if (status != SANE_STATUS_GOOD && status != SANE_STATUS_EOF)
    {
      DBG (1, "USB READ IO Error in usb_write_then_read, fail devnum=%d\n",
           dev->devnum);
      return status;
    }
  return SANE_STATUS_GOOD;
}

void
build_packet(Lexmark_Device * dev, SANE_Byte packet_id, SANE_Byte * buffer){
  memcpy(buffer, command_with_params_block, command_with_params_block_size);
  // protocole related... "ID?"
  buffer[14] = packet_id;

  // mode
  if (memcmp(dev->val[OPT_MODE].s, "Color", 5) == 0 )
    buffer[20] = 0x03;
  else
    buffer[20] = 0x02;

  // pixel width (swap lower byte -> higher byte)
  buffer[24] = dev->val[OPT_BR_X].w & 0xFF;
  buffer[25] = (dev->val[OPT_BR_X].w >> 8) & 0xFF;

  // pixel height (swap lower byte -> higher byte)
  buffer[28] = dev->val[OPT_BR_Y].w & 0xFF;
  buffer[29] = (dev->val[OPT_BR_Y].w >> 8) & 0xFF;

  // dpi x (swap lower byte -> higher byte)
  buffer[40] = dev->val[OPT_RESOLUTION].w & 0xFF;
  buffer[41] = (dev->val[OPT_RESOLUTION].w >> 8) & 0xFF;

  // dpi y (swap lower byte -> higher byte)
  buffer[42] = dev->val[OPT_RESOLUTION].w & 0xFF;
  buffer[43] = (dev->val[OPT_RESOLUTION].w >> 8) & 0xFF;
}

SANE_Status
init_options (Lexmark_Device * dev)
{

  SANE_Option_Descriptor *od;

  DBG (2, "init_options: dev = %p\n", (void *) dev);

  /* number of options */
  od = &(dev->opt[OPT_NUM_OPTS]);
  od->name = SANE_NAME_NUM_OPTIONS;
  od->title = SANE_TITLE_NUM_OPTIONS;
  od->desc = SANE_DESC_NUM_OPTIONS;
  od->type = SANE_TYPE_INT;
  od->unit = SANE_UNIT_NONE;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT;
  od->constraint_type = SANE_CONSTRAINT_NONE;
  od->constraint.range = 0;
  dev->val[OPT_NUM_OPTS].w = NUM_OPTIONS;

  /* mode - sets the scan mode: Color / Gray */
  od = &(dev->opt[OPT_MODE]);
  od->name = SANE_NAME_SCAN_MODE;
  od->title = SANE_TITLE_SCAN_MODE;
  od->desc = SANE_DESC_SCAN_MODE;;
  od->type = SANE_TYPE_STRING;
  od->unit = SANE_UNIT_NONE;
  od->size = MAX_OPTION_STRING_SIZE;
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_STRING_LIST;
  od->constraint.string_list = mode_list;
  dev->val[OPT_MODE].s = malloc (od->size);
  if (!dev->val[OPT_MODE].s)
    return SANE_STATUS_NO_MEM;
  strcpy (dev->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_COLOR);

  /* resolution */
  od = &(dev->opt[OPT_RESOLUTION]);
  od->name = SANE_NAME_SCAN_RESOLUTION;
  od->title = SANE_TITLE_SCAN_RESOLUTION;
  od->desc = SANE_DESC_SCAN_RESOLUTION;
  od->type = SANE_TYPE_INT;
  od->unit = SANE_UNIT_DPI;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->constraint_type = SANE_CONSTRAINT_WORD_LIST;
  od->constraint.word_list = dpi_list;
  dev->val[OPT_RESOLUTION].w = 200;

  /* preview mode */
  od = &(dev->opt[OPT_PREVIEW]);
  od->name = SANE_NAME_PREVIEW;
  od->title = SANE_TITLE_PREVIEW;
  od->desc = SANE_DESC_PREVIEW;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_INACTIVE;
  od->type = SANE_TYPE_BOOL;
  od->constraint_type = SANE_CONSTRAINT_NONE;
  dev->val[OPT_PREVIEW].w = SANE_FALSE;

  /* "Geometry" group: */
  od = &(dev->opt[OPT_GEOMETRY_GROUP]);
  od->name = "";
  od->title = SANE_I18N ("Geometry");
  od->desc = "";
  od->type = SANE_TYPE_GROUP;
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->size = 0;
  od->constraint_type = SANE_CONSTRAINT_NONE;

  /* top-left x */
  od = &(dev->opt[OPT_TL_X]);
  od->name = SANE_NAME_SCAN_TL_X;
  od->title = SANE_TITLE_SCAN_TL_X;
  od->desc = SANE_DESC_SCAN_TL_X;
  od->type = SANE_TYPE_INT;
  od->cap = SANE_CAP_INACTIVE;
  od->size = sizeof (SANE_Word);
  od->unit = SANE_UNIT_PIXEL;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &x_range;
  dev->val[OPT_TL_X].w = 0;

  /* top-left y */
  od = &(dev->opt[OPT_TL_Y]);
  od->name = SANE_NAME_SCAN_TL_Y;
  od->title = SANE_TITLE_SCAN_TL_Y;
  od->desc = SANE_DESC_SCAN_TL_Y;
  od->type = SANE_TYPE_INT;
  od->cap = SANE_CAP_INACTIVE;
  od->size = sizeof (SANE_Word);
  od->unit = SANE_UNIT_PIXEL;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &y_range;
  dev->val[OPT_TL_Y].w = 0;

  /* bottom-right x */
  od = &(dev->opt[OPT_BR_X]);
  od->name = SANE_NAME_SCAN_BR_X;
  od->title = SANE_TITLE_SCAN_BR_X;
  od->desc = SANE_DESC_SCAN_BR_X;
  od->type = SANE_TYPE_INT;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->unit = SANE_UNIT_PIXEL;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &x_range;
  dev->val[OPT_BR_X].w = 1699;

  /* bottom-right y */
  od = &(dev->opt[OPT_BR_Y]);
  od->name = SANE_NAME_SCAN_BR_Y;
  od->title = SANE_TITLE_SCAN_BR_Y;
  od->desc = SANE_DESC_SCAN_BR_Y;
  od->type = SANE_TYPE_INT;
  od->size = sizeof (SANE_Word);
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
  od->unit = SANE_UNIT_PIXEL;
  od->constraint_type = SANE_CONSTRAINT_RANGE;
  od->constraint.range = &y_range;
  dev->val[OPT_BR_Y].w = 2337;

  return SANE_STATUS_GOOD;
}

/* callback function for sanei_usb_attach_matching_devices
*/
static SANE_Status
attach_one (SANE_String_Const devname)
{
  Lexmark_Device *lexmark_device;

  DBG (2, "attachLexmark: devname=%s\n", devname);

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      /* already attached devices */
      if (strcmp (lexmark_device->sane.name, devname) == 0)
      {
        lexmark_device->missing = SANE_FALSE;
    return SANE_STATUS_GOOD;
      }
    }

  lexmark_device = (Lexmark_Device *) malloc (sizeof (Lexmark_Device));
  if (lexmark_device == NULL)
    return SANE_STATUS_NO_MEM;

  lexmark_device->sane.name = strdup (devname);
  lexmark_device->sane.vendor = "Lexmark";
  lexmark_device->sane.model = "X2600 series";
  lexmark_device->sane.type = "flat bed";

  /* Make the pointer to the read buffer null here */
  lexmark_device->read_buffer = malloc (sizeof (Read_Buffer));
  lexmark_device->read_buffer->data = NULL;
  lexmark_device->read_buffer->size = 0;
  lexmark_device->read_buffer->last_line_bytes_read = 0;
  lexmark_device->read_buffer->image_line_no = 0;
  lexmark_device->read_buffer->write_byte_counter = 0;
  lexmark_device->read_buffer->read_byte_counter = 0;
  /* mark device as present */
  lexmark_device->missing = SANE_FALSE;
  lexmark_device->device_cancelled = SANE_FALSE;
  /* insert it a the start of the chained list */
  lexmark_device->next = first_device;
  first_device = lexmark_device;
  num_devices++;

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_init (SANE_Int *version_code, SANE_Auth_Callback authorize)
{
  FILE *fp;
  SANE_Char config_line[PATH_MAX];
  const char *lp;

  DBG_INIT ();
  DBG (2, "sane_init: version_code %s 0, authorize %s 0\n",
       version_code == 0 ? "=" : "!=", authorize == 0 ? "=" : "!=");
  DBG (1, "sane_init: SANE lexmark_x2600 backend version %d.%d.%d from %s\n",
       SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD, PACKAGE_STRING);

  if (version_code)
    *version_code = SANE_VERSION_CODE (SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, BUILD);

  sanei_usb_init ();

  fp = sanei_config_open (LEXMARK_X2600_CONFIG_FILE);
  if (!fp)
    {
      DBG (2, "No config no prob...(%s)\n", LEXMARK_X2600_CONFIG_FILE);
      return SANE_STATUS_GOOD;
    }
  while (sanei_config_read (config_line, sizeof (config_line), fp))
    {
      if (config_line[0] == '#')
    continue;		/* ignore line comments */

      lp = sanei_config_skip_whitespace (config_line);
      /* skip empty lines */
      if (*lp == 0)
    continue;

      DBG (4, "attach_matching_devices(%s)\n", config_line);
      sanei_usb_attach_matching_devices (config_line, attach_one);
    }

  DBG (4, "finished reading configure file\n");
  fclose (fp);
  initialized = SANE_TRUE;
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_devices (const SANE_Device ***device_list, SANE_Bool local_only)
{
  SANE_Int index;
  Lexmark_Device *lexmark_device;

  DBG (2, "sane_get_devices: device_list=%p, local_only=%d\n",
       (void *) device_list, local_only);

  sanei_usb_scan_devices ();

  if (devlist)
    free (devlist);

  devlist = malloc ((num_devices + 1) * sizeof (devlist[0]));
  if (!devlist)
    return (SANE_STATUS_NO_MEM);

  index = 0;
  lexmark_device = first_device;
  while (lexmark_device != NULL)
    {
      DBG (2, "sane_get_devices:   lexmark_device->missing:%d\n",
           lexmark_device->missing);
      if (lexmark_device->missing == SANE_FALSE)
    {

      devlist[index] = &(lexmark_device->sane);
      index++;
    }
      lexmark_device = lexmark_device->next;
    }
  devlist[index] = 0;

  *device_list = devlist;

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open (SANE_String_Const devicename, SANE_Handle * handle)
{
  Lexmark_Device *lexmark_device;
  SANE_Status status;

  DBG (2, "sane_open: devicename=\"%s\", handle=%p\n", devicename,
       (void *) handle);

  /* walk the linked list of scanner device until there is a match
   * with the device name */
  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      DBG (10, "sane_open: devname from list: %s\n",
       lexmark_device->sane.name);
      if (strcmp (devicename, "") == 0
      || strcmp (devicename, "lexmark") == 0
      || strcmp (devicename, lexmark_device->sane.name) == 0)
    break;
    }

  *handle = lexmark_device;

  status = init_options (lexmark_device);
  if (status != SANE_STATUS_GOOD)
    return status;

  DBG(2, "sane_open: device `%s' opening devnum: '%d'\n",
      lexmark_device->sane.name, lexmark_device->devnum);
  status = sanei_usb_open (lexmark_device->sane.name, &(lexmark_device->devnum));
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "sane_open: couldn't open device `%s': %s\n",
           lexmark_device->sane.name,
       sane_strstatus (status));
      return status;
    }
  else
    {
      DBG (2, "sane_open: device `%s' successfully opened devnum: '%d'\n",
           lexmark_device->sane.name, lexmark_device->devnum);
    }

  return status;
}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  Lexmark_Device *lexmark_device;

  DBG (2, "sane_get_option_descriptor: handle=%p, option = %d\n",
       (void *) handle, option);

  /* Check for valid option number */
  if ((option < 0) || (option >= NUM_OPTIONS))
    return NULL;

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
    break;
    }

  if (!lexmark_device)
    return NULL;

  if (lexmark_device->opt[option].name)
    {
      DBG (2, "sane_get_option_descriptor: name=%s\n",
       lexmark_device->opt[option].name);
    }

  return &(lexmark_device->opt[option]);
}

SANE_Status
sane_control_option (SANE_Handle handle, SANE_Int option, SANE_Action action,
                     void * value, SANE_Word * info)
{
  Lexmark_Device *lexmark_device;
  SANE_Status status;
  SANE_Word w;

  DBG (2, "sane_control_option: handle=%p, opt=%d, act=%d, val=%p, info=%p\n",
       (void *) handle, option, action, (void *) value, (void *) info);

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
    break;
    }


  if (value == NULL)
    return SANE_STATUS_INVAL;

  switch (action)
    {
    case SANE_ACTION_SET_VALUE:
      if (!SANE_OPTION_IS_SETTABLE (lexmark_device->opt[option].cap))
        {
          return SANE_STATUS_INVAL;
        }
      /* Make sure boolean values are only TRUE or FALSE */
      if (lexmark_device->opt[option].type == SANE_TYPE_BOOL)
    {
      if (!
          ((*(SANE_Bool *) value == SANE_FALSE)
           || (*(SANE_Bool *) value == SANE_TRUE)))
        return SANE_STATUS_INVAL;
    }

      /* Check range constraints */
      if (lexmark_device->opt[option].constraint_type ==
      SANE_CONSTRAINT_RANGE)
    {
      status =
        sanei_constrain_value (&(lexmark_device->opt[option]), value,
                   info);
      if (status != SANE_STATUS_GOOD)
        {
          DBG (2, "SANE_CONTROL_OPTION: Bad value for range\n");
          return SANE_STATUS_INVAL;
        }
    }
              switch (option)
    {
    case OPT_NUM_OPTS:
    case OPT_RESOLUTION:
    case OPT_TL_X:
    case OPT_TL_Y:
    case OPT_BR_X:
    case OPT_BR_Y:
      DBG (2, "Option value set to %d (%s)\n", *(SANE_Word *) value,
           lexmark_device->opt[option].name);
      lexmark_device->val[option].w = *(SANE_Word *) value;
      if (lexmark_device->val[OPT_TL_X].w >
          lexmark_device->val[OPT_BR_X].w)
        {
          w = lexmark_device->val[OPT_TL_X].w;
          lexmark_device->val[OPT_TL_X].w =
        lexmark_device->val[OPT_BR_X].w;
          lexmark_device->val[OPT_BR_X].w = w;
          if (info)
        *info |= SANE_INFO_RELOAD_PARAMS;
        }
      if (lexmark_device->val[OPT_TL_Y].w >
          lexmark_device->val[OPT_BR_Y].w)
        {
          w = lexmark_device->val[OPT_TL_Y].w;
          lexmark_device->val[OPT_TL_Y].w =
        lexmark_device->val[OPT_BR_Y].w;
          lexmark_device->val[OPT_BR_Y].w = w;
          if (info)
        *info |= SANE_INFO_RELOAD_PARAMS;
        }
      break;
    case OPT_MODE:
      strcpy (lexmark_device->val[option].s, value);
      if (info)
        *info |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS;
      return SANE_STATUS_GOOD;
    }

      if (info != NULL)
    *info |= SANE_INFO_RELOAD_PARAMS;

      break;
    case SANE_ACTION_GET_VALUE:

      switch (option)
    {
    case OPT_NUM_OPTS:
    case OPT_RESOLUTION:
    case OPT_PREVIEW:
    case OPT_TL_X:
    case OPT_TL_Y:
    case OPT_BR_X:
    case OPT_BR_Y:
      *(SANE_Word *) value = lexmark_device->val[option].w;
      DBG (2, "Option value = %d (%s)\n", *(SANE_Word *) value,
           lexmark_device->opt[option].name);
      break;
    case OPT_MODE:
      strcpy (value, lexmark_device->val[option].s);
      break;
    }
      break;

    default:
      return SANE_STATUS_INVAL;
    }

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_parameters (SANE_Handle handle, SANE_Parameters * params)
{
  Lexmark_Device *lexmark_device;
  SANE_Parameters *device_params;
  SANE_Int width_px;

  DBG (2, "sane_get_parameters: handle=%p, params=%p\n", (void *) handle,
       (void *) params);

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
    break;
    }

  if (!lexmark_device)
    return SANE_STATUS_INVAL;

  // res = lexmark_device->val[OPT_RESOLUTION].w;
  device_params = &(lexmark_device->params);

  width_px =
    lexmark_device->val[OPT_BR_X].w - lexmark_device->val[OPT_TL_X].w;

  /* 24 bit colour = 8 bits/channel for each of the RGB channels */
  device_params->pixels_per_line = width_px;
  device_params->format = SANE_FRAME_RGB; // SANE_FRAME_GRAY
  device_params->depth = 8;
  device_params->bytes_per_line =
    (SANE_Int) (3 * device_params->pixels_per_line);

  if (strcmp (lexmark_device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_COLOR)
      != 0)
    {
      device_params->format = SANE_FRAME_GRAY;
      device_params->bytes_per_line =
        (SANE_Int) (device_params->pixels_per_line);
    }

  /* geometry in pixels */
  device_params->last_frame = SANE_TRUE;
  device_params->lines = -1;//lexmark_device->val[OPT_BR_Y].w;

  DBG (2, "sane_get_parameters: device_params->pixels_per_line=%d\n",
       device_params->pixels_per_line);
  DBG (2, "sane_get_parameters: device_params->bytes_per_line=%d\n",
       device_params->bytes_per_line);
  DBG (2, "sane_get_parameters: device_params->format=%d\n",
       device_params->format);
  DBG (2, "    SANE_FRAME_GRAY: %d\n",
       SANE_FRAME_GRAY);
  DBG (2, "    SANE_FRAME_RGB: %d\n",
       SANE_FRAME_RGB);

  if (params != 0)
    {
      params->format = device_params->format;
      params->last_frame = device_params->last_frame;
      params->lines = device_params->lines;
      params->depth = device_params->depth;
      params->pixels_per_line = device_params->pixels_per_line;
      params->bytes_per_line = device_params->bytes_per_line;
    }
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_start (SANE_Handle handle)
{
  Lexmark_Device * lexmark_device;
  SANE_Status status;
  SANE_Byte * cmd = (SANE_Byte *) malloc
    (command_with_params_block_size * sizeof (SANE_Byte));;

  DBG (2, "sane_start: handle=%p\n", (void *) handle);

  if (!initialized)
    return SANE_STATUS_INVAL;

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
    break;
    }

  //launch scan commands
  status = usb_write_then_read(lexmark_device, command1_block,
                               command1_block_size);
  if (status != SANE_STATUS_GOOD)
    return status;

  status = usb_write_then_read(lexmark_device, command2_block,
                               command2_block_size);
  if (status != SANE_STATUS_GOOD)
    return status;

  build_packet(lexmark_device, 0x05, cmd);
  status = usb_write_then_read(lexmark_device, cmd,
                               command_with_params_block_size);
  if (status != SANE_STATUS_GOOD)
    return status;

  build_packet(lexmark_device, 0x01, cmd);;
  status = usb_write_then_read(lexmark_device, cmd,
                               command_with_params_block_size);
  if (status != SANE_STATUS_GOOD)
    return status;

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_read (SANE_Handle handle, SANE_Byte * data,
       SANE_Int max_length, SANE_Int * length)
{
  Lexmark_Device * lexmark_device;
  SANE_Status status;
  size_t size = max_length;
  SANE_Byte buf[size];

  DBG (1, "sane_read: handle=%p, data=%p, max_length=%d\n",
       (void *) handle, (void *) data, max_length);

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
    break;
    }

  if (lexmark_device->device_cancelled == SANE_TRUE) {
      usb_write_then_read(lexmark_device, command_cancel1_block,
                          command_cancel_size);
      usb_write_then_read(lexmark_device, command_cancel2_block,
                          command_cancel_size);
      usb_write_then_read(lexmark_device, command_cancel1_block,
                          command_cancel_size);
      usb_write_then_read(lexmark_device, command_cancel2_block,
                          command_cancel_size);
      // to empty buffers
      status = sanei_usb_read_bulk (lexmark_device->devnum, buf, &size);
      DBG (10, "USB READ \n");
      status = sanei_usb_read_bulk (lexmark_device->devnum, buf, &size);
      DBG (10, "USB READ \n");
      status = sanei_usb_read_bulk (lexmark_device->devnum, buf, &size);
      DBG (10, "USB READ \n");
      //status = sanei_usb_read_bulk (lexmark_device->devnum, buf, &size);
      //DBG (10, "USB READ \n");
      return SANE_STATUS_CANCELLED;//SANE_STATUS_GOOD; //;
  }

  status = sanei_usb_read_bulk (lexmark_device->devnum, buf, &size);
  if (status != SANE_STATUS_GOOD && status != SANE_STATUS_EOF)
    {
      DBG (1, "USB READ Error in usb_write_then_read, cannot read devnum=%d\n",
           lexmark_device->devnum);
      return status;
    }

  // is last data packet ?
  if (memcmp(last_data_packet, buf, last_data_packet_size) == 0)
    {
      length = 0;
      return SANE_STATUS_EOF;
    }

  // cancel packet received?
  if (memcmp(cancel_packet, buf, cancel_packet_size) == 0)
    {
      length = 0;
      return SANE_STATUS_CANCELLED;
    }

  clean_and_copy_data(buf, size, data, length, lexmark_device->params.format,
                      max_length, handle);

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
  DBG (2, "sane_set_io_mode: handle = %p, non_blocking = %d\n",
       (void *) handle, non_blocking);

  if (non_blocking)
    return SANE_STATUS_UNSUPPORTED;

  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_select_fd (SANE_Handle handle, SANE_Int * fd)
{
  DBG (2, "sane_get_select_fd: handle = %p, fd %s 0\n", (void *) handle,
       fd ? "!=" : "=");

  return SANE_STATUS_UNSUPPORTED;
}

void
sane_cancel (SANE_Handle handle)
{
  Lexmark_Device * lexmark_device;

  DBG (2, "sane_cancel: handle = %p\n", (void *) handle);

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
    break;
    }

  lexmark_device->device_cancelled = SANE_TRUE;
}

void
sane_close (SANE_Handle handle)
{
  Lexmark_Device * lexmark_device;

  DBG (2, "sane_close: handle=%p\n", (void *) handle);

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
    break;
    }

  sanei_usb_close (lexmark_device->devnum);
}

void
sane_exit (void)
{
  Lexmark_Device *lexmark_device, *next_lexmark_device;

  DBG (2, "sane_exit\n");

  if (!initialized)
    return;

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = next_lexmark_device)
    {
      next_lexmark_device = lexmark_device->next;
      free (lexmark_device);
    }

  if (devlist)
    free (devlist);

  sanei_usb_exit();
  initialized = SANE_FALSE;

}
