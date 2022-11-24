#include "lexmark_x2600.h"

SANE_Status
sane_init (SANE_Int __sane_unused__ *vc, SANE_Auth_Callback __sane_unused__ cb)
{
  DBG_INIT ();
  DBG (1, "SANE Lexmark_x2600 backend ");
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_devices (const SANE_Device __sane_unused__ ***dl, SANE_Bool __sane_unused__ local)
{
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_open (SANE_String_Const __sane_unused__ name, SANE_Handle __sane_unused__ *h)
{
  return SANE_STATUS_GOOD;
}

const SANE_Option_Descriptor *
sane_get_option_descriptor (SANE_Handle __sane_unused__ h, SANE_Int __sane_unused__ opt)
{
  return NULL;
}

SANE_Status
sane_control_option (SANE_Handle __sane_unused__ h, SANE_Int __sane_unused__ opt, SANE_Action __sane_unused__ act,
                     void __sane_unused__ *val, SANE_Word __sane_unused__ *info)
{
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_parameters (SANE_Handle __sane_unused__ h, SANE_Parameters __sane_unused__ *parms)
{
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_start (SANE_Handle __sane_unused__ h)
{
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_read (SANE_Handle __sane_unused__ h, SANE_Byte __sane_unused__ *buf, SANE_Int __sane_unused__ maxlen, SANE_Int __sane_unused__ *lenp)
{
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_set_io_mode (SANE_Handle __sane_unused__ h, SANE_Bool __sane_unused__ non_blocking)
{
  return SANE_STATUS_GOOD;
}

SANE_Status
sane_get_select_fd (SANE_Handle __sane_unused__ h, SANE_Int __sane_unused__ *fdp)
{
  return SANE_STATUS_GOOD;
}

void
sane_cancel (SANE_Handle __sane_unused__ h)
{

}

void
sane_close (SANE_Handle __sane_unused__ h)
{

}

void
sane_exit (void)
{

}
