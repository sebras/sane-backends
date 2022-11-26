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
  1,				/* minimum */
  5078,				/* maximum */
  2				/* quantization : 16 is required so we
				   never have an odd width */
};

static SANE_Range y_range = {
  1,				/* minimum */
  7015,				/* maximum */
  /* 7032, for X74 */
  2				/* quantization */
};

static SANE_Int command1_block_size = 28;
static SANE_Byte command1_block[] = {
  0xA5, 0x00, 0x19, 0x10, 0x01, 0x83, 0xAA, 0xBB,
  0xCC, 0xDD, 0x02, 0x00, 0x1B, 0x53, 0x03, 0x00,
  0x00, 0x00, 0x80, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,
  0xAA, 0xBB, 0xCC, 0xDD};

static SANE_Int command2_block_size = 28;
static SANE_Byte command2_block[] = {
  0xA5, 0x00, 0x19, 0x10, 0x01, 0x83, 0xAA, 0xBB,
  0xCC, 0xDD, 0x02, 0x00, 0x1B, 0x53, 0x04, 0x00,
  0x00, 0x00, 0x80, 0x00, 0xAA, 0xBB, 0xCC, 0xDD,
  0xAA, 0xBB, 0xCC, 0xDD};

static SANE_Int command_with_params_block_size = 52;
static SANE_Byte command_with_params_block[] = {
  0xA5, 0x00, 0x31, 0x10, 0x01, 0x83, 0xAA, 0xBB,
  0xCC, 0xDD, 0x02, 0x00, 0x1B, 0x53, 0x05, 0x00,
  0x18, 0x00, 0x80, 0x00, 0xFF, 0x00, 0x00, 0x02,
  0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0xBB, 0xCC, 0xDD,
  0xAA, 0xBB, 0xCC, 0xDD};


SANE_Status
usb_write_then_read (Lexmark_Device * dev, SANE_Byte * cmd, size_t cmd_size)
{
  size_t buf_size = 128;
  SANE_Byte buf[buf_size];
  SANE_Status status;

  DBG (2, "USB WRITE device:%d size:%d cmd:%p\n", dev->devnum, cmd_size, cmd[0]);

  sanei_usb_set_endpoint(dev->devnum, USB_DIR_OUT|USB_ENDPOINT_TYPE_BULK, 0x02);
  status = sanei_usb_write_bulk (dev->devnum, cmd, &cmd_size);
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "USB WRITE IO Error in usb_write_then_read, cannot launch scan status: %d\n", status);
      return status;
    }
  //sanei_usb_set_endpoint(dev->devnum, USB_DIR_IN|USB_ENDPOINT_TYPE_BULK , 0x01);
  status = sanei_usb_read_bulk (dev->devnum, buf, &buf_size);
  if (status != SANE_STATUS_GOOD && status != SANE_STATUS_EOF)
    {
      DBG (1, "USB READ IO Error in usb_write_then_read, cannot launch scan\n");
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
  buffer[20] = 0x02;
  // pixel width
  buffer[24] = 0xF0;
  buffer[25] = 0x00;
  // pixel height
  buffer[28] = 0xF0;
  buffer[29] = 0x00;
  // dpi x
  buffer[40] = 0xC8;
  buffer[41] = 0x00;
  // dpi y  
  buffer[42] = 0xC8;
  buffer[43] = 0x00;
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

  /* mode - sets the scan mode: Color, Gray, or Line Art */
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
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
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
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
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
  od->cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
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
  SANE_Int dn;
  SANE_Status status;

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

  /* status = sanei_usb_open (devname, &dn); */
  /* if (status != SANE_STATUS_GOOD) */
  /*   { */
  /*     DBG (1, "attachLexmark: couldn't open device `%s': %s\n", devname, */
  /*          sane_strstatus (status)); */
  /*     return status; */
  /*   } */
  /* else */
  /*   DBG (2, "attachLexmark: device `%s' successfully opened dn: %s\n", devname, dn); */

  lexmark_device->sane.name = strdup (devname);
  lexmark_device->sane.vendor = "Lexmark";
  lexmark_device->sane.model = "X2600 series";
  lexmark_device->sane.type = "flat bed";

  /* Make the pointer to the read buffer null here */
  lexmark_device->read_buffer = NULL;
  /* mark device as present */
  lexmark_device->missing = SANE_FALSE;

  //sanei_usb_close (lexmark_device->devnum);

  /* insert it a the start of the chained list */
  lexmark_device->next = first_device;
  first_device = lexmark_device;
  num_devices++;

  return status;
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

  if (devlist)
    free (devlist);

  devlist = malloc ((num_devices + 1) * sizeof (devlist[0]));
  if (!devlist)
    return (SANE_STATUS_NO_MEM);

  index = 0;
  lexmark_device = first_device;
  while (lexmark_device != NULL)
    {
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
      DBG (2, "sane_open: devname from list: %s\n",
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

  DBG(2, "sane_open: device `%s' opening devnum: '%d'\n", lexmark_device->sane.name, lexmark_device->devnum);
  status = sanei_usb_open (lexmark_device->sane.name, &(lexmark_device->devnum));
  if (status != SANE_STATUS_GOOD)
    {
      DBG (1, "sane_open: couldn't open device `%s': %s\n", lexmark_device->sane.name,
	   sane_strstatus (status));
      return status;
    }
  else
    {
      DBG (2, "sane_open: device `%s' successfully opened devnum: '%d'\n", lexmark_device->sane.name, lexmark_device->devnum);
    }

  return status;
}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle handle, SANE_Int option)
{
  Lexmark_Device *lexmark_device;

  DBG (2, "sane_get_option_descriptor: handle=%p, option = %d\n",
       (void *) handle, option);

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
  SANE_Int res, width_px;
  SANE_Int channels, bitsperchannel;

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

  res = lexmark_device->val[OPT_RESOLUTION].w;

  device_params = &(lexmark_device->params);

  /* 24 bit colour = 8 bits/channel for each of the RGB channels */
  channels = 3;
  bitsperchannel = 8;
  if (strcmp (lexmark_device->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_COLOR)
      != 0)
    channels = 1;

  /* geometry in pixels */
  width_px =
    lexmark_device->val[OPT_BR_X].w - lexmark_device->val[OPT_TL_X].w;
  DBG (7, "sane_get_parameters: tl=(%d,%d) br=(%d,%d)\n",
       lexmark_device->val[OPT_TL_X].w, lexmark_device->val[OPT_TL_Y].w,
       lexmark_device->val[OPT_BR_X].w, lexmark_device->val[OPT_BR_Y].w);

  DBG (7, "sane_get_parameters: res=(%d)\n", res);

  device_params->format = SANE_FRAME_RGB; // SANE_FRAME_GRAY
  if (channels == 1)
    device_params->format = SANE_FRAME_GRAY;

  device_params->last_frame = SANE_TRUE;
  device_params->lines = -1;
  device_params->depth = bitsperchannel;
  device_params->pixels_per_line = width_px;
  device_params->bytes_per_line =
	(SANE_Int) ((7 + device_params->pixels_per_line) / 8);

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
  SANE_Byte * cmd = (SANE_Byte *) malloc (command_with_params_block_size * sizeof (SANE_Byte));;
  
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
  status = usb_write_then_read(lexmark_device, command1_block, command1_block_size);
  if (status != SANE_STATUS_GOOD)
    return status;

  status = usb_write_then_read(lexmark_device, command2_block, command2_block_size);
  if (status != SANE_STATUS_GOOD)
    return status;

  build_packet(lexmark_device, 0x05, cmd);
  DBG (2, "sane_start: cmd=%p\n", cmd);
  status = usb_write_then_read(lexmark_device, cmd, command_with_params_block_size);
  if (status != SANE_STATUS_GOOD)
    return status;    
  build_packet(lexmark_device, 0x01, cmd);;
  status = usb_write_then_read(lexmark_device, cmd, command_with_params_block_size);
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
  
  DBG (2, "sane_read: handle=%p, data=%p, max_length = %d, length=%p\n",
       (void *) handle, (void *) data, max_length, (void *) length);

  for (lexmark_device = first_device; lexmark_device;
       lexmark_device = lexmark_device->next)
    {
      if (lexmark_device == handle)
	break;
    }

  size_t size = 256;
  SANE_Byte buf[size];

  //sanei_usb_set_endpoint(lexmark_device->devnum, USB_DIR_IN|USB_ENDPOINT_TYPE_BULK , 0x01);
  status = sanei_usb_read_bulk (lexmark_device->devnum, buf, &size);
  if (status != SANE_STATUS_GOOD && status != SANE_STATUS_EOF)
    {
      DBG (1, "USB READ IO Error in usb_write_then_read, cannot launch scan\n");
      return status;
    }
  
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_set_io_mode (SANE_Handle handle, SANE_Bool non_blocking)
{
  DBG (2, "sane_set_io_mode: handle = %p, non_blocking = %d\n",
       (void *) handle, non_blocking);

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
  DBG (2, "sane_cancel: handle = %p\n", (void *) handle);
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
