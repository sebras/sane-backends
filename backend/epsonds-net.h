#ifndef _EPSONDS_NET_H_
#define _EPSONDS_NET_H_

#include <sys/types.h>
#include "../include/sane/sane.h"

typedef void (*Device_Found_CallBack) (const char* name, const char* ip);

extern ssize_t epsonds_net_read(struct epsonds_scanner *s, unsigned char *buf, size_t buf_size,
				SANE_Status *status);
extern size_t epsonds_net_write(struct epsonds_scanner *s, unsigned int cmd, const unsigned char *buf,
				size_t buf_size, size_t reply_len,
				SANE_Status *status);
extern SANE_Status epsonds_net_lock(struct epsonds_scanner *s);
extern SANE_Status epsonds_net_unlock(struct epsonds_scanner *s);
extern SANE_Status epsonds_net_request_read(epsonds_scanner *s, size_t len);

#if WITH_AVAHI
extern SANE_Status epsonds_searchDevices(Device_Found_CallBack deviceFoundCallBack);
#endif

#endif
