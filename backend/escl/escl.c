/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Touboul Nathane
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

#include "escl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h>

#include "../include/sane/saneopts.h"
#include "../include/sane/sanei.h"
#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"


#ifndef SANE_NAME_SHARPEN
# define SANE_NAME_SHARPEN "sharpen"
# define SANE_TITLE_SHARPEN SANE_I18N("Sharpen")
# define SANE_DESC_SHARPEN SANE_I18N("Set sharpen value.")
#endif

#ifndef SANE_NAME_THRESHOLD
# define SANE_NAME_THRESHOLD "threshold"
#endif
#ifndef SANE_TITLE_THRESHOLD
# define SANE_TITLE_THRESHOLD SANE_I18N("Threshold")
#endif
#ifndef SANE_DESC_THRESHOLD
# define SANE_DESC_THRESHOLD \
    SANE_I18N("Set threshold for line-art scans.")
#endif

#define min(A,B) (((A)<(B)) ? (A) : (B))
#define max(A,B) (((A)>(B)) ? (A) : (B))
#define IS_ACTIVE(OPTION) (((handler->opt[OPTION].cap) & SANE_CAP_INACTIVE) == 0)
#define INPUT_BUFFER_SIZE 4096

static const SANE_Device **devlist = NULL;
static ESCL_Device *list_devices_primary = NULL;
static int num_devices = 0;


typedef struct Handled {
    struct Handled *next;
    ESCL_Device *device;
    char *result;
    ESCL_ScanParam param;
    SANE_Option_Descriptor opt[NUM_OPTIONS];
    Option_Value val[NUM_OPTIONS];
    capabilities_t *scanner;
    SANE_Range x_range1;
    SANE_Range x_range2;
    SANE_Range y_range1;
    SANE_Range y_range2;
    SANE_Range brightness_range;
    SANE_Range contrast_range;
    SANE_Range sharpen_range;
    SANE_Range thresold_range;
    SANE_Bool cancel;
    SANE_Bool write_scan_data;
    SANE_Bool decompress_scan_data;
    SANE_Bool end_read;
    SANE_Parameters ps;
} escl_sane_t;

static ESCL_Device *
escl_free_device(ESCL_Device *current)
{
    if (!current) return NULL;
    free((void*)current->ip_address);
    free((void*)current->model_name);
    free((void*)current->type);
    free((void*)current->is);
    free((void*)current->uuid);
    free((void*)current->unix_socket);
    curl_slist_free_all(current->hack);
    free(current);
    return NULL;
}


static int
escl_tls_protocol_supported(char *url)
{
   CURLcode res = CURLE_UNSUPPORTED_PROTOCOL;
   CURL *curl = curl_easy_init();
   if(curl) {
      curl_easy_setopt(curl, CURLOPT_URL, url);

      curl_easy_setopt(curl, CURLOPT_USE_SSL, (long)CURLUSESSL_TRY);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
      curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
      curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
      /* Perform the request */
      res = curl_easy_perform(curl);
      curl_easy_cleanup(curl);
   }
   return res;
}

static int
escl_is_tls(char * url, char *type)
{
    if(!strcmp(type, "_uscans._tcp") ||
       !strcmp(type, "https"))
      {
                if (escl_tls_protocol_supported(url) == CURLE_OK)
                {
                        DBG(10, "curl tls compatible\n");
                        return 1;
                }
      }
    return 0;
}

void
escl_free_handler(escl_sane_t *handler)
{
    if (handler == NULL)
        return;

    escl_free_device(handler->device);
    free(handler);
}

SANE_Status escl_parse_name(SANE_String_Const name, ESCL_Device *device);

static SANE_Status
escl_check_and_add_device(ESCL_Device *current)
{
    if(!current) {
      DBG (10, "ESCL_Device *current us null.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (!current->ip_address) {
      DBG (10, "Ip Address allocation failure.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (current->port_nb == 0) {
      DBG (10, "No port defined.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (!current->model_name) {
      DBG (10, "Modele Name allocation failure.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (!current->type) {
      DBG (10, "Scanner Type allocation failure.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (!current->is) {
      DBG (10, "Scanner Is allocation failure.\n");
      return (SANE_STATUS_NO_MEM);
    }
    ++num_devices;
    current->next = list_devices_primary;
    list_devices_primary = current;
    return (SANE_STATUS_GOOD);
}

/**
 * \fn static SANE_Status escl_add_in_list(ESCL_Device *current)
 * \brief Function that adds all the element needed to my list :
 *        the port number, the model name, the ip address, and the type of url (http/https).
 *        Moreover, this function counts the number of devices found.
 *
 * \return SANE_STATUS_GOOD if everything is OK.
 */
static SANE_Status
escl_add_in_list(ESCL_Device *current)
{
    if(!current) {
      DBG (10, "ESCL_Device *current us null.\n");
      return (SANE_STATUS_NO_MEM);
    }

    if (SANE_STATUS_GOOD ==
        escl_check_and_add_device(current)) {
        list_devices_primary = current;
        return (SANE_STATUS_GOOD);
    }
    current = escl_free_device(current);
    return (SANE_STATUS_NO_MEM);
}

/**
 * \fn SANE_Status escl_device_add(int port_nb, const char *model_name, char *ip_address, char *type)
 * \brief Function that browses my list ('for' loop) and returns the "escl_add_in_list" function to
 *        adds all the element needed to my list :
 *        the port number, the model name, the ip address and the type of the url (http / https).
 *
 * \return escl_add_in_list(current)
 */
SANE_Status
escl_device_add(int port_nb,
                const char *model_name,
                char *ip_address,
                const char *is,
                const char *uuid,
                char *type)
{
    char tmp[PATH_MAX] = { 0 };
    char *model = NULL;
    char url_port[512] = { 0 };
    int tls_version = 0;
    ESCL_Device *current = NULL;
    DBG (10, "escl_device_add\n");
    snprintf(url_port, sizeof(url_port), "https://%s:%d", ip_address, port_nb);
    tls_version = escl_is_tls(url_port, type);

    for (current = list_devices_primary; current; current = current->next) {
	if ((strcmp(current->ip_address, ip_address) == 0) ||
            (uuid && current->uuid && !strcmp(current->uuid, uuid)))
           {
	      if (strcmp(current->type, type))
                {
                  if(!strcmp(type, "_uscans._tcp") ||
                     !strcmp(type, "https"))
                    {
                       free (current->type);
                       current->type = strdup(type);
                       if (strcmp(current->ip_address, ip_address)) {
                           free (current->ip_address);
                           current->ip_address = strdup(ip_address);
                       }
                       current->port_nb = port_nb;
                       current->https = SANE_TRUE;
                       current->tls = tls_version;
                    }
	          return (SANE_STATUS_GOOD);
                }
              else if (current->port_nb == port_nb)
	        return (SANE_STATUS_GOOD);
           }
    }
    current = (ESCL_Device*)calloc(1, sizeof(*current));
    if (current == NULL) {
       DBG (10, "New device allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    current->port_nb = port_nb;

    if (strcmp(type, "_uscan._tcp") != 0 && strcmp(type, "http") != 0) {
        snprintf(tmp, sizeof(tmp), "%s SSL", model_name);
        current->https = SANE_TRUE;
    } else {
        current->https = SANE_FALSE;
    }
    current->tls = tls_version;
    model = (char*)(tmp[0] != 0 ? tmp : model_name);
    current->model_name = strdup(model);
    current->ip_address = strdup(ip_address);
    memset(tmp, 0, PATH_MAX);
    snprintf(tmp, sizeof(tmp), "%s scanner", (is ? is : "flatbed or ADF"));
    current->is = strdup(tmp);
    current->type = strdup(type);
    if (uuid)
       current->uuid = strdup(uuid);
    return escl_add_in_list(current);
}

/**
 * \fn static inline size_t max_string_size(const SANE_String_Const strings[])
 * \brief Function that browses the string ('for' loop) and counts the number of character in the string.
 *        --> this allows to know the maximum size of the string.
 *
 * \return max_size + 1 (the size max)
 */
static inline size_t
max_string_size(const SANE_String_Const strings[])
{
    size_t max_size = 0;
    int i = 0;

    for (i = 0; strings[i]; ++i) {
	size_t size = strlen (strings[i]);
	if (size > max_size)
	    max_size = size;
    }
    return (max_size + 1);
}

static char *
get_vendor(char *search)
{
	if(strcasestr(search, "Epson"))
		return strdup("Epson");
	else if(strcasestr(search, "Fujitsu"))
		return strdup("Fujitsu");
	else if(strcasestr(search, "HP"))
		return strdup("HP");
	else if(strcasestr(search, "Canon"))
		return strdup("Canon");
	else if(strcasestr(search, "Lexmark"))
		return strdup("Lexmark");
	else if(strcasestr(search, "Samsung"))
		return strdup("Samsung");
	else if(strcasestr(search, "Xerox"))
		return strdup("Xerox");
	else if(strcasestr(search, "OKI"))
		return strdup("OKI");
	else if(strcasestr(search, "Hewlett Packard"))
		return strdup("Hewlett Packard");
	else if(strcasestr(search, "IBM"))
		return strdup("IBM");
	else if(strcasestr(search, "Mustek"))
		return strdup("Mustek");
	else if(strcasestr(search, "Ricoh"))
		return strdup("Ricoh");
	else if(strcasestr(search, "Sharp"))
		return strdup("Sharp");
	else if(strcasestr(search, "UMAX"))
		return strdup("UMAX");
	else if(strcasestr(search, "PINT"))
		return strdup("PINT");
	else if(strcasestr(search, "Brother"))
		return strdup("Brother");
	return NULL;
}

/**
 * \fn static SANE_Device *convertFromESCLDev(ESCL_Device *cdev)
 * \brief Function that checks if the url of the received scanner is secured or not (http / https).
 *        --> if the url is not secured, our own url will be composed like "http://'ip':'port'".
 *        --> else, our own url will be composed like "https://'ip':'port'".
 *        AND, it's in this function that we gather all the information of the url (that were in our list) :
 *        the model_name, the port, the ip, and the type of url.
 *        SO, leaving this function, we have in memory the complete url.
 *
 * \return sdev (structure that contains the elements of the url)
 */
static SANE_Device *
convertFromESCLDev(ESCL_Device *cdev)
{
    char *tmp;
    int len, lv = 0;
    char unix_path[PATH_MAX+7] = { 0 };
    SANE_Device *sdev = (SANE_Device*) calloc(1, sizeof(SANE_Device));
    if (!sdev) {
       DBG (10, "Sane_Device allocation failure.\n");
       return NULL;
    }

    if (cdev->unix_socket && strlen(cdev->unix_socket)) {
        snprintf(unix_path, sizeof(unix_path), "unix:%s:", cdev->unix_socket);
    }
    len = snprintf(NULL, 0, "%shttp%s://%s:%d",
             unix_path, cdev->https ? "s" : "", cdev->ip_address, cdev->port_nb);
    len++;
    tmp = (char *)malloc(len);
    if (!tmp) {
        DBG (10, "Name allocation failure.\n");
        goto freedev;
    }
    snprintf(tmp, len, "%shttp%s://%s:%d",
             unix_path, cdev->https ? "s" : "", cdev->ip_address, cdev->port_nb);
    sdev->name = tmp;

    DBG( 10, "Escl add device : %s\n", tmp);
    sdev->vendor = get_vendor(cdev->model_name);

    if (!sdev->vendor)
       sdev->vendor = strdup("ESCL");
    else
       lv = strlen(sdev->vendor) + 1;
    if (!sdev->vendor) {
       DBG (10, "Vendor allocation failure.\n");
       goto freemodel;
    }
    sdev->model = strdup(lv + cdev->model_name);
    if (!sdev->model) {
       DBG (10, "Model allocation failure.\n");
       goto freename;
    }
    sdev->type = strdup(cdev->is);
    if (!sdev->type) {
       DBG (10, "Scanner Type allocation failure.\n");
       goto freevendor;
    }
    return (sdev);
freevendor:
    free((void*)sdev->vendor);
freemodel:
    free((void*)sdev->model);
freename:
    free((void*)sdev->name);
freedev:
    free((void*)sdev);
    return NULL;
}

/**
 * \fn SANE_Status sane_init(SANE_Int *version_code, SANE_Auth_Callback authorize)
 * \brief Function that's called before any other SANE function ; it's the first SANE function called.
 *        --> this function checks the SANE config. and can check the authentication of the user if
 *        'authorize' value is more than SANE_TRUE.
 *        In this case, it will be necessary to define an authentication method.
 *
 * \return SANE_STATUS_GOOD (everything is OK)
 */
SANE_Status
sane_init(SANE_Int *version_code, SANE_Auth_Callback __sane_unused__ authorize)
{
    DBG_INIT();
    DBG (10, "escl sane_init\n");
    SANE_Status status = SANE_STATUS_GOOD;
    curl_global_init(CURL_GLOBAL_ALL);
    if (version_code != NULL)
	*version_code = SANE_VERSION_CODE(1, 0, 0);
    if (status != SANE_STATUS_GOOD)
	return (status);
    return (SANE_STATUS_GOOD);
}

/**
 * \fn void sane_exit(void)
 * \brief Function that must be called to terminate use of a backend.
 *        This function will first close all device handles that still might be open.
 *        --> by freeing all the elements of my list.
 *        After this function, no function other than 'sane_init' may be called.
 */
void
sane_exit(void)
{
    DBG (10, "escl sane_exit\n");
    ESCL_Device *next = NULL;

    while (list_devices_primary != NULL) {
	next = list_devices_primary->next;
	free(list_devices_primary);
	list_devices_primary = next;
    }
    if (devlist)
	free (devlist);
    list_devices_primary = NULL;
    devlist = NULL;
    curl_global_cleanup();
}

/**
 * \fn static SANE_Status attach_one_config(SANEI_Config *config, const char *line)
 * \brief Function that implements a configuration file to the user :
 *        if the user can't detect some devices, he will be able to force their detection with this config' file to use them.
 *        Thus, this function parses the config' file to use the device of the user with the information below :
 *        the type of protocol (http/https), the ip, the port number, and the model name.
 *
 * \return escl_add_in_list(escl_device) if the parsing worked, SANE_STATUS_GOOD otherwise.
 */
static SANE_Status
attach_one_config(SANEI_Config __sane_unused__ *config, const char *line,
		  void __sane_unused__ *data)
{
    int port = 0;
    SANE_Status status;
    static ESCL_Device *escl_device = NULL;
    if (*line == '#') return SANE_STATUS_GOOD;
    if (!strncmp(line, "pdfblacklist", 12)) return SANE_STATUS_GOOD;
    if (strncmp(line, "device", 6) == 0) {
        char *name_str = NULL;
        char *opt_model = NULL;
        char *opt_hack = NULL;

        line = sanei_config_get_string(line + 6, &name_str);
        DBG (10, "New Escl_Device URL [%s].\n", (name_str ? name_str : "VIDE"));
        if (!name_str || !*name_str) {
            DBG(10, "Escl_Device URL missing.\n");
            return SANE_STATUS_INVAL;
        }
        if (*line) {
            line = sanei_config_get_string(line, &opt_model);
            DBG (10, "New Escl_Device model [%s].\n", opt_model);
        }
        if (*line) {
            line = sanei_config_get_string(line, &opt_hack);
            DBG (10, "New Escl_Device hack [%s].\n", opt_hack);
        }

        escl_free_device(escl_device);
        escl_device = (ESCL_Device*)calloc(1, sizeof(ESCL_Device));
        if (!escl_device) {
           DBG (10, "New Escl_Device allocation failure.\n");
           free(name_str);
           return (SANE_STATUS_NO_MEM);
        }
        status = escl_parse_name(name_str, escl_device);
        free(name_str);
        if (status != SANE_STATUS_GOOD) {
            escl_free_device(escl_device);
            escl_device = NULL;
            return status;
        }
        escl_device->model_name = opt_model ? opt_model : strdup("Unknown model");
        escl_device->is = strdup("flatbed or ADF scanner");
        escl_device->uuid = NULL;
    }

    if (strncmp(line, "[device]", 8) == 0) {
	escl_device = escl_free_device(escl_device);
	escl_device = (ESCL_Device*)calloc(1, sizeof(ESCL_Device));
	if (!escl_device) {
	   DBG (10, "New Escl_Device allocation failure.");
	   return (SANE_STATUS_NO_MEM);
	}
    }
    else if (strncmp(line, "ip", 2) == 0) {
	const char *ip_space = sanei_config_skip_whitespace(line + 2);
	DBG (10, "New Escl_Device IP [%s].", (ip_space ? ip_space : "VIDE"));
	if (escl_device != NULL && ip_space != NULL) {
	    DBG (10, "New Escl_Device IP Affected.");
	    escl_device->ip_address = strdup(ip_space);
	}
    }
    else if (sscanf(line, "port %i", &port) == 1 && port != 0) {
	DBG (10, "New Escl_Device PORT [%d].", port);
	if (escl_device != NULL) {
	    DBG (10, "New Escl_Device PORT Affected.");
	    escl_device->port_nb = port;
	}
    }
    else if (strncmp(line, "model", 5) == 0) {
	const char *model_space = sanei_config_skip_whitespace(line + 5);
	DBG (10, "New Escl_Device MODEL [%s].", (model_space ? model_space : "VIDE"));
	if (escl_device != NULL && model_space != NULL) {
	    DBG (10, "New Escl_Device MODEL Affected.");
	    escl_device->model_name = strdup(model_space);
	}
    }
    else if (strncmp(line, "type", 4) == 0) {
	const char *type_space = sanei_config_skip_whitespace(line + 4);
	DBG (10, "New Escl_Device TYPE [%s].", (type_space ? type_space : "VIDE"));
	if (escl_device != NULL && type_space != NULL) {
	    DBG (10, "New Escl_Device TYPE Affected.");
	    escl_device->type = strdup(type_space);
	}
    }
    escl_device->is = strdup("flatbed or ADF scanner");
    escl_device->uuid = NULL;
    char url_port[512] = { 0 };
    snprintf(url_port, sizeof(url_port), "https://%s:%d", escl_device->ip_address, escl_device->port_nb);
    escl_device->tls = escl_is_tls(url_port, escl_device->type);
    status = escl_check_and_add_device(escl_device);
    if (status == SANE_STATUS_GOOD)
       escl_device = NULL;
    return status;
}

/**
 * \fn SANE_Status sane_get_devices(const SANE_Device ***device_list, SANE_Bool local_only)
 * \brief Function that searches for connected devices and places them in our 'device_list'. ('for' loop)
 *        If the attribute 'local_only' is worth SANE_FALSE, we only returns the connected devices locally.
 *
 * \return SANE_STATUS_GOOD if devlist != NULL ; SANE_STATUS_NO_MEM otherwise.
 */
SANE_Status
sane_get_devices(const SANE_Device ***device_list, SANE_Bool local_only)
{
    if (local_only)             /* eSCL is a network-only protocol */
	return (device_list ? SANE_STATUS_GOOD : SANE_STATUS_INVAL);

    DBG (10, "escl sane_get_devices\n");
    ESCL_Device *dev = NULL;
    static const SANE_Device **devlist = 0;
    SANE_Status status;
    SANE_Status status2;

    if (device_list == NULL)
	return (SANE_STATUS_INVAL);
    status2 = sanei_configure_attach(ESCL_CONFIG_FILE, NULL,
				    attach_one_config, NULL);
    escl_devices(&status);
    if (status != SANE_STATUS_GOOD && status2 != SANE_STATUS_GOOD)
    {
       if (status2 != SANE_STATUS_GOOD)
               return (status2);
       if (status != SANE_STATUS_GOOD)
               return (status);
    }
    if (devlist)
	free(devlist);
    devlist = (const SANE_Device **) calloc (num_devices + 1, sizeof (devlist[0]));
    if (devlist == NULL)
	return (SANE_STATUS_NO_MEM);
    int i = 0;
    for (dev = list_devices_primary; i < num_devices; dev = dev->next) {
	SANE_Device *s_dev = convertFromESCLDev(dev);
	devlist[i] = s_dev;
	i++;
    }
    devlist[i] = 0;
    *device_list = devlist;
    return (devlist) ? SANE_STATUS_GOOD : SANE_STATUS_NO_MEM;
}

/* Returns the length of the longest string, including the terminating
 * character. */
static size_t
_source_size_max (SANE_String_Const * sources)
{
  size_t size = 0;

  while(*sources)
   {
      size_t t = strlen (*sources) + 1;
      if (t > size)
          size = t;
      sources++;
   }
  return size;
}

static int
_get_resolution(escl_sane_t *handler, int resol)
{
    int x = 1;
    int n = handler->scanner->caps[handler->scanner->source].SupportedResolutions[0] + 1;
    int old = -1;
    for (; x < n; x++) {
      DBG(10, "SEARCH RESOLUTION [ %d | %d]\n", resol, (int)handler->scanner->caps[handler->scanner->source].SupportedResolutions[x]);
      if (resol == handler->scanner->caps[handler->scanner->source].SupportedResolutions[x])
         return resol;
      else if (resol < handler->scanner->caps[handler->scanner->source].SupportedResolutions[x])
      {
          if (old == -1)
             return handler->scanner->caps[handler->scanner->source].SupportedResolutions[1];
          else
             return old;
      }
      else
          old = handler->scanner->caps[handler->scanner->source].SupportedResolutions[x];
    }
    return old;
}


/**
 * \fn static SANE_Status init_options(SANE_String_Const name, escl_sane_t *s)
 * \brief Function thzt initializes all the needed options of the received scanner
 *        (the resolution / the color / the margins) thanks to the information received with
 *        the 'escl_capabilities' function, called just before.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD)
 */
static SANE_Status
init_options_small(SANE_String_Const name_source, escl_sane_t *s)
{
    int found = 0;
    DBG (10, "escl init_options\n");

    SANE_Status status = SANE_STATUS_GOOD;
    if (!s->scanner) return SANE_STATUS_INVAL;
    if (name_source) {
	   int source = s->scanner->source;
	   if (!strcmp(name_source, SANE_I18N ("ADF Duplex")))
	       s->scanner->source = ADFDUPLEX;
	   else if (!strncmp(name_source, "A", 1) ||
	            !strcmp(name_source, SANE_I18N ("ADF")))
	       s->scanner->source = ADFSIMPLEX;
	   else
	       s->scanner->source = PLATEN;
	   if (source == s->scanner->source) return status;
           s->scanner->caps[s->scanner->source].default_color =
                strdup(s->scanner->caps[source].default_color);
           s->scanner->caps[s->scanner->source].default_resolution =
                _get_resolution(s, s->scanner->caps[source].default_resolution);
    }
    if (s->scanner->caps[s->scanner->source].ColorModes == NULL) {
        if (s->scanner->caps[PLATEN].ColorModes)
            s->scanner->source = PLATEN;
        else if (s->scanner->caps[ADFSIMPLEX].ColorModes)
            s->scanner->source = ADFSIMPLEX;
        else if (s->scanner->caps[ADFDUPLEX].ColorModes)
            s->scanner->source = ADFDUPLEX;
        else
            return SANE_STATUS_INVAL;
    }
    if (s->scanner->source == PLATEN) {
        DBG (10, "SOURCE PLATEN.\n");
    }
    else if (s->scanner->source == ADFDUPLEX) {
        DBG (10, "SOURCE ADFDUPLEX.\n");
    }
    else if (s->scanner->source == ADFSIMPLEX) {
        DBG (10, "SOURCE ADFSIMPLEX.\n");
    }
    s->x_range1.min = 0;
    s->x_range1.max =
	    PIXEL_TO_MM((s->scanner->caps[s->scanner->source].MaxWidth -
		         s->scanner->caps[s->scanner->source].MinWidth),
			300.0);
    s->x_range1.quant = 0;
    s->x_range2.min = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MinWidth, 300.0);
    s->x_range2.max = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MaxWidth, 300.0);
    s->x_range2.quant = 0;
    s->y_range1.min = 0;
    s->y_range1.max =
	    PIXEL_TO_MM((s->scanner->caps[s->scanner->source].MaxHeight -
	                 s->scanner->caps[s->scanner->source].MinHeight),
			300.0);
    s->y_range1.quant = 0;
    s->y_range2.min = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MinHeight, 300.0);
    s->y_range2.max = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MaxHeight, 300.0);
    s->y_range2.quant = 0;

    s->opt[OPT_MODE].constraint.string_list = s->scanner->caps[s->scanner->source].ColorModes;
    if (s->val[OPT_MODE].s)
        free(s->val[OPT_MODE].s);
    s->val[OPT_MODE].s = NULL;

    if (s->scanner->caps[s->scanner->source].default_color) {
        int x = 0;
        if (!strcmp(s->scanner->caps[s->scanner->source].default_color, "Grayscale8"))
           s->val[OPT_MODE].s = (char *)strdup(SANE_VALUE_SCAN_MODE_GRAY);
        else if (!strcmp(s->scanner->caps[s->scanner->source].default_color, "BlackAndWhite1"))
           s->val[OPT_MODE].s = (char *)strdup(SANE_VALUE_SCAN_MODE_LINEART);
        else
           s->val[OPT_MODE].s = (char *)strdup(SANE_VALUE_SCAN_MODE_COLOR);
        for (x = 0; s->scanner->caps[s->scanner->source].ColorModes[x]; x++) {
            if (s->scanner->caps[s->scanner->source].ColorModes[x] &&
              !strcasecmp(s->scanner->caps[s->scanner->source].ColorModes[x], s->val[OPT_MODE].s)) {
              found = 1;
              break;
            }
        }
    }
    if (!s->scanner->caps[s->scanner->source].default_color || found == 0) {
        if (s->scanner->caps[s->scanner->source].default_color)
           free(s->scanner->caps[s->scanner->source].default_color);
        s->val[OPT_MODE].s = strdup(s->scanner->caps[s->scanner->source].ColorModes[0]);
        if (!strcasecmp(s->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY))
            s->scanner->caps[s->scanner->source].default_color = strdup("Grayscale8");
        else if (!strcasecmp(s->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_LINEART))
            s->scanner->caps[s->scanner->source].default_color = strdup("BlackAndWhite1");
        else
            s->scanner->caps[s->scanner->source].default_color = strdup("RGB24");
    }
    if (!s->val[OPT_MODE].s) {
       DBG (10, "Color Mode Default allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    if (!s->scanner->caps[s->scanner->source].default_color) {
       DBG (10, "Color Mode Default allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    s->val[OPT_RESOLUTION].w = s->scanner->caps[s->scanner->source].default_resolution;
    s->opt[OPT_TL_X].constraint.range = &s->x_range1;
    s->opt[OPT_TL_Y].constraint.range = &s->y_range1;
    s->opt[OPT_BR_X].constraint.range = &s->x_range2;
    s->opt[OPT_BR_Y].constraint.range = &s->y_range2;

    if (s->val[OPT_SCAN_SOURCE].s)
      free (s->val[OPT_SCAN_SOURCE].s);
    s->val[OPT_SCAN_SOURCE].s = strdup (s->scanner->Sources[s->scanner->source]);

    return (SANE_STATUS_GOOD);
}

/**
 * \fn static SANE_Status init_options(SANE_String_Const name, escl_sane_t *s)
 * \brief Function thzt initializes all the needed options of the received scanner
 *        (the resolution / the color / the margins) thanks to the information received with
 *        the 'escl_capabilities' function, called just before.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD)
 */
static SANE_Status
init_options(SANE_String_Const name_source, escl_sane_t *s)
{
    DBG (10, "escl init_options\n");

    SANE_Status status = SANE_STATUS_GOOD;
    int i = 0;
    if (!s->scanner) return SANE_STATUS_INVAL;
    if (name_source) {
	   int source = s->scanner->source;
	   DBG (10, "escl init_options name [%s]\n", name_source);
	   if (!strcmp(name_source, SANE_I18N ("ADF Duplex")))
	       s->scanner->source = ADFDUPLEX;
	   else if (!strncmp(name_source, "A", 1) ||
	            !strcmp(name_source, SANE_I18N ("ADF")))
	       s->scanner->source = ADFSIMPLEX;
	   else
	       s->scanner->source = PLATEN;
	   if (source == s->scanner->source) return status;
    }
    if (s->scanner->caps[s->scanner->source].ColorModes == NULL) {
        if (s->scanner->caps[PLATEN].ColorModes)
            s->scanner->source = PLATEN;
        else if (s->scanner->caps[ADFSIMPLEX].ColorModes)
            s->scanner->source = ADFSIMPLEX;
        else if (s->scanner->caps[ADFDUPLEX].ColorModes)
            s->scanner->source = ADFDUPLEX;
        else
            return SANE_STATUS_INVAL;
    }
    if (s->scanner->source == PLATEN) {
        DBG (10, "SOURCE PLATEN.\n");
    }
    else if (s->scanner->source == ADFDUPLEX) {
        DBG (10, "SOURCE ADFDUPLEX.\n");
    }
    else if (s->scanner->source == ADFSIMPLEX) {
        DBG (10, "SOURCE ADFSIMPLEX.\n");
    }
    memset (s->opt, 0, sizeof (s->opt));
    memset (s->val, 0, sizeof (s->val));
    for (i = 0; i < NUM_OPTIONS; ++i) {
	   s->opt[i].size = sizeof (SANE_Word);
	   s->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    }
    s->x_range1.min = 0;
    s->x_range1.max =
	    PIXEL_TO_MM((s->scanner->caps[s->scanner->source].MaxWidth -
		         s->scanner->caps[s->scanner->source].MinWidth),
			300.0);
    s->x_range1.quant = 0;
    s->x_range2.min = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MinWidth, 300.0);
    s->x_range2.max = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MaxWidth, 300.0);
    s->x_range2.quant = 0;
    s->y_range1.min = 0;
    s->y_range1.max =
	    PIXEL_TO_MM((s->scanner->caps[s->scanner->source].MaxHeight -
	                 s->scanner->caps[s->scanner->source].MinHeight),
			300.0);
    s->y_range1.quant = 0;
    s->y_range2.min = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MinHeight, 300.0);
    s->y_range2.max = PIXEL_TO_MM(s->scanner->caps[s->scanner->source].MaxHeight, 300.0);
    s->y_range2.quant = 0;
    s->opt[OPT_NUM_OPTS].title = SANE_TITLE_NUM_OPTIONS;
    s->opt[OPT_NUM_OPTS].desc = SANE_DESC_NUM_OPTIONS;
    s->opt[OPT_NUM_OPTS].type = SANE_TYPE_INT;
    s->opt[OPT_NUM_OPTS].cap = SANE_CAP_SOFT_DETECT;
    s->val[OPT_NUM_OPTS].w = NUM_OPTIONS;

    s->opt[OPT_MODE_GROUP].title = SANE_TITLE_SCAN_MODE;
    s->opt[OPT_MODE_GROUP].desc = "";
    s->opt[OPT_MODE_GROUP].type = SANE_TYPE_GROUP;
    s->opt[OPT_MODE_GROUP].cap = 0;
    s->opt[OPT_MODE_GROUP].size = 0;
    s->opt[OPT_MODE_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

    s->opt[OPT_MODE].name = SANE_NAME_SCAN_MODE;
    s->opt[OPT_MODE].title = SANE_TITLE_SCAN_MODE;
    s->opt[OPT_MODE].desc = SANE_DESC_SCAN_MODE;
    s->opt[OPT_MODE].type = SANE_TYPE_STRING;
    s->opt[OPT_MODE].unit = SANE_UNIT_NONE;
    s->opt[OPT_MODE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    s->opt[OPT_MODE].constraint.string_list = s->scanner->caps[s->scanner->source].ColorModes;
    if (s->scanner->caps[s->scanner->source].default_color) {
        if (!strcasecmp(s->scanner->caps[s->scanner->source].default_color, "Grayscale8"))
           s->val[OPT_MODE].s = (char *)strdup(SANE_VALUE_SCAN_MODE_GRAY);
        else if (!strcasecmp(s->scanner->caps[s->scanner->source].default_color, "BlackAndWhite1"))
           s->val[OPT_MODE].s = (char *)strdup(SANE_VALUE_SCAN_MODE_LINEART);
        else
           s->val[OPT_MODE].s = (char *)strdup(SANE_VALUE_SCAN_MODE_COLOR);
    }
    else {
        s->val[OPT_MODE].s = (char *)strdup(s->scanner->caps[s->scanner->source].ColorModes[0]);
        if (!strcasecmp(s->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY)) {
           s->scanner->caps[s->scanner->source].default_color = strdup("Grayscale8");
        }
        else if (!strcasecmp(s->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_LINEART)) {
           s->scanner->caps[s->scanner->source].default_color =
                strdup("BlackAndWhite1");
        }
        else {
           s->scanner->caps[s->scanner->source].default_color =
               strdup("RGB24");
       }
    }
    if (!s->val[OPT_MODE].s) {
       DBG (10, "Color Mode Default allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    DBG (10, "++ Color Mode Default allocation [%s].\n", s->scanner->caps[s->scanner->source].default_color);
    s->opt[OPT_MODE].size = max_string_size(s->scanner->caps[s->scanner->source].ColorModes);
    if (!s->scanner->caps[s->scanner->source].default_color) {
       DBG (10, "Color Mode Default allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    DBG (10, "Color Mode Default allocation (%s).\n", s->scanner->caps[s->scanner->source].default_color);

    s->opt[OPT_RESOLUTION].name = SANE_NAME_SCAN_RESOLUTION;
    s->opt[OPT_RESOLUTION].title = SANE_TITLE_SCAN_RESOLUTION;
    s->opt[OPT_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
    s->opt[OPT_RESOLUTION].type = SANE_TYPE_INT;
    s->opt[OPT_RESOLUTION].unit = SANE_UNIT_DPI;
    s->opt[OPT_RESOLUTION].constraint_type = SANE_CONSTRAINT_WORD_LIST;
    s->opt[OPT_RESOLUTION].constraint.word_list = s->scanner->caps[s->scanner->source].SupportedResolutions;
    s->val[OPT_RESOLUTION].w = s->scanner->caps[s->scanner->source].SupportedResolutions[1];
    s->scanner->caps[s->scanner->source].default_resolution = s->scanner->caps[s->scanner->source].SupportedResolutions[1];

    s->opt[OPT_PREVIEW].name = SANE_NAME_PREVIEW;
    s->opt[OPT_PREVIEW].title = SANE_TITLE_PREVIEW;
    s->opt[OPT_PREVIEW].desc = SANE_DESC_PREVIEW;
    s->opt[OPT_PREVIEW].cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
    s->opt[OPT_PREVIEW].type = SANE_TYPE_BOOL;
    s->val[OPT_PREVIEW].w = SANE_FALSE;

    s->opt[OPT_GRAY_PREVIEW].name = SANE_NAME_GRAY_PREVIEW;
    s->opt[OPT_GRAY_PREVIEW].title = SANE_TITLE_GRAY_PREVIEW;
    s->opt[OPT_GRAY_PREVIEW].desc = SANE_DESC_GRAY_PREVIEW;
    s->opt[OPT_GRAY_PREVIEW].type = SANE_TYPE_BOOL;
    s->val[OPT_GRAY_PREVIEW].w = SANE_FALSE;

    s->opt[OPT_GEOMETRY_GROUP].title = SANE_TITLE_GEOMETRY;
    s->opt[OPT_GEOMETRY_GROUP].desc = SANE_DESC_GEOMETRY;
    s->opt[OPT_GEOMETRY_GROUP].type = SANE_TYPE_GROUP;
    s->opt[OPT_GEOMETRY_GROUP].cap = SANE_CAP_ADVANCED;
    s->opt[OPT_GEOMETRY_GROUP].size = 0;
    s->opt[OPT_GEOMETRY_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

    s->opt[OPT_TL_X].name = SANE_NAME_SCAN_TL_X;
    s->opt[OPT_TL_X].title = SANE_TITLE_SCAN_TL_X;
    s->opt[OPT_TL_X].desc = SANE_DESC_SCAN_TL_X;
    s->opt[OPT_TL_X].type = SANE_TYPE_FIXED;
    s->opt[OPT_TL_X].size = sizeof(SANE_Fixed);
    s->opt[OPT_TL_X].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_TL_X].unit = SANE_UNIT_MM;
    s->opt[OPT_TL_X].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_TL_X].constraint.range = &s->x_range1;
    s->val[OPT_TL_X].w = s->x_range1.min;

    s->opt[OPT_TL_Y].name = SANE_NAME_SCAN_TL_Y;
    s->opt[OPT_TL_Y].title = SANE_TITLE_SCAN_TL_Y;
    s->opt[OPT_TL_Y].desc = SANE_DESC_SCAN_TL_Y;
    s->opt[OPT_TL_Y].type = SANE_TYPE_FIXED;
    s->opt[OPT_TL_Y].size = sizeof(SANE_Fixed);
    s->opt[OPT_TL_Y].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_TL_Y].unit = SANE_UNIT_MM;
    s->opt[OPT_TL_Y].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_TL_Y].constraint.range = &s->y_range1;
    s->val[OPT_TL_Y].w = s->y_range1.min;

    s->opt[OPT_BR_X].name = SANE_NAME_SCAN_BR_X;
    s->opt[OPT_BR_X].title = SANE_TITLE_SCAN_BR_X;
    s->opt[OPT_BR_X].desc = SANE_DESC_SCAN_BR_X;
    s->opt[OPT_BR_X].type = SANE_TYPE_FIXED;
    s->opt[OPT_BR_X].size = sizeof(SANE_Fixed);
    s->opt[OPT_BR_X].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_BR_X].unit = SANE_UNIT_MM;
    s->opt[OPT_BR_X].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_BR_X].constraint.range = &s->x_range2;
    s->val[OPT_BR_X].w = s->x_range2.max;

    s->opt[OPT_BR_Y].name = SANE_NAME_SCAN_BR_Y;
    s->opt[OPT_BR_Y].title = SANE_TITLE_SCAN_BR_Y;
    s->opt[OPT_BR_Y].desc = SANE_DESC_SCAN_BR_Y;
    s->opt[OPT_BR_Y].type = SANE_TYPE_FIXED;
    s->opt[OPT_BR_Y].size = sizeof(SANE_Fixed);
    s->opt[OPT_BR_Y].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_BR_Y].unit = SANE_UNIT_MM;
    s->opt[OPT_BR_Y].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_BR_Y].constraint.range = &s->y_range2;
    s->val[OPT_BR_Y].w = s->y_range2.max;

	/* OPT_SCAN_SOURCE */
    s->opt[OPT_SCAN_SOURCE].name = SANE_NAME_SCAN_SOURCE;
    s->opt[OPT_SCAN_SOURCE].title = SANE_TITLE_SCAN_SOURCE;
    s->opt[OPT_SCAN_SOURCE].desc = SANE_DESC_SCAN_SOURCE;
    s->opt[OPT_SCAN_SOURCE].type = SANE_TYPE_STRING;
    s->opt[OPT_SCAN_SOURCE].size = _source_size_max(s->scanner->Sources);
    s->opt[OPT_SCAN_SOURCE].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_SCAN_SOURCE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    s->opt[OPT_SCAN_SOURCE].constraint.string_list = s->scanner->Sources;
    if (s->val[OPT_SCAN_SOURCE].s)
       free (s->val[OPT_SCAN_SOURCE].s);
    s->val[OPT_SCAN_SOURCE].s = strdup (s->scanner->Sources[s->scanner->source]);

    /* "Enhancement" group: */
    s->opt[OPT_ENHANCEMENT_GROUP].title = SANE_I18N ("Enhancement");
    s->opt[OPT_ENHANCEMENT_GROUP].desc = "";    /* not valid for a group */
    s->opt[OPT_ENHANCEMENT_GROUP].type = SANE_TYPE_GROUP;
    s->opt[OPT_ENHANCEMENT_GROUP].cap = SANE_CAP_ADVANCED;
    s->opt[OPT_ENHANCEMENT_GROUP].size = 0;
    s->opt[OPT_ENHANCEMENT_GROUP].constraint_type = SANE_CONSTRAINT_NONE;


    s->opt[OPT_BRIGHTNESS].name = SANE_NAME_BRIGHTNESS;
    s->opt[OPT_BRIGHTNESS].title = SANE_TITLE_BRIGHTNESS;
    s->opt[OPT_BRIGHTNESS].desc = SANE_DESC_BRIGHTNESS;
    s->opt[OPT_BRIGHTNESS].type = SANE_TYPE_INT;
    s->opt[OPT_BRIGHTNESS].unit = SANE_UNIT_NONE;
    s->opt[OPT_BRIGHTNESS].constraint_type = SANE_CONSTRAINT_RANGE;
    if (s->scanner->brightness) {
       s->opt[OPT_BRIGHTNESS].constraint.range = &s->brightness_range;
       s->val[OPT_BRIGHTNESS].w = s->scanner->brightness->value;
       s->brightness_range.quant=1;
       s->brightness_range.min=s->scanner->brightness->min;
       s->brightness_range.max=s->scanner->brightness->max;
    }
    else{
      SANE_Range range = { 0, 255, 0 };
      s->opt[OPT_BRIGHTNESS].constraint.range = &range;
      s->val[OPT_BRIGHTNESS].w = 0;
      s->opt[OPT_BRIGHTNESS].cap |= SANE_CAP_INACTIVE;
    }
    s->opt[OPT_CONTRAST].name = SANE_NAME_CONTRAST;
    s->opt[OPT_CONTRAST].title = SANE_TITLE_CONTRAST;
    s->opt[OPT_CONTRAST].desc = SANE_DESC_CONTRAST;
    s->opt[OPT_CONTRAST].type = SANE_TYPE_INT;
    s->opt[OPT_CONTRAST].unit = SANE_UNIT_NONE;
    s->opt[OPT_CONTRAST].constraint_type = SANE_CONSTRAINT_RANGE;
    if (s->scanner->contrast) {
       s->opt[OPT_CONTRAST].constraint.range = &s->contrast_range;
       s->val[OPT_CONTRAST].w = s->scanner->contrast->value;
       s->contrast_range.quant=1;
       s->contrast_range.min=s->scanner->contrast->min;
       s->contrast_range.max=s->scanner->contrast->max;
    }
    else{
      SANE_Range range = { 0, 255, 0 };
      s->opt[OPT_CONTRAST].constraint.range = &range;
      s->val[OPT_CONTRAST].w = 0;
      s->opt[OPT_CONTRAST].cap |= SANE_CAP_INACTIVE;
    }
    s->opt[OPT_SHARPEN].name = SANE_NAME_SHARPEN;
    s->opt[OPT_SHARPEN].title = SANE_TITLE_SHARPEN;
    s->opt[OPT_SHARPEN].desc = SANE_DESC_SHARPEN;
    s->opt[OPT_SHARPEN].type = SANE_TYPE_INT;
    s->opt[OPT_SHARPEN].unit = SANE_UNIT_NONE;
    s->opt[OPT_SHARPEN].constraint_type = SANE_CONSTRAINT_RANGE;
    if (s->scanner->sharpen) {
       s->opt[OPT_SHARPEN].constraint.range = &s->sharpen_range;
       s->val[OPT_SHARPEN].w = s->scanner->sharpen->value;
       s->sharpen_range.quant=1;
       s->sharpen_range.min=s->scanner->sharpen->min;
       s->sharpen_range.max=s->scanner->sharpen->max;
    }
    else{
      SANE_Range range = { 0, 255, 0 };
      s->opt[OPT_SHARPEN].constraint.range = &range;
      s->val[OPT_SHARPEN].w = 0;
      s->opt[OPT_SHARPEN].cap |= SANE_CAP_INACTIVE;
    }
    /*threshold*/
    s->opt[OPT_THRESHOLD].name = SANE_NAME_THRESHOLD;
    s->opt[OPT_THRESHOLD].title = SANE_TITLE_THRESHOLD;
    s->opt[OPT_THRESHOLD].desc = SANE_DESC_THRESHOLD;
    s->opt[OPT_THRESHOLD].type = SANE_TYPE_INT;
    s->opt[OPT_THRESHOLD].unit = SANE_UNIT_NONE;
    s->opt[OPT_THRESHOLD].constraint_type = SANE_CONSTRAINT_RANGE;
    if (s->scanner->threshold) {
      s->opt[OPT_THRESHOLD].constraint.range = &s->thresold_range;
      s->val[OPT_THRESHOLD].w = s->scanner->threshold->value;
      s->thresold_range.quant=1;
      s->thresold_range.min= s->scanner->threshold->min;
      s->thresold_range.max=s->scanner->threshold->max;
    }
    else{
      SANE_Range range = { 0, 255, 0 };
      s->opt[OPT_THRESHOLD].constraint.range = &range;
      s->val[OPT_THRESHOLD].w = 0;
      s->opt[OPT_THRESHOLD].cap |= SANE_CAP_INACTIVE;
    }
    if (!strcasecmp(s->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_LINEART)) {
       if (s->scanner->threshold)
       	  s->opt[OPT_THRESHOLD].cap  &= ~SANE_CAP_INACTIVE;
       if (s->scanner->brightness)
       	  s->opt[OPT_BRIGHTNESS].cap |= SANE_CAP_INACTIVE;
       if (s->scanner->contrast)
       	  s->opt[OPT_CONTRAST].cap |= SANE_CAP_INACTIVE;
       if (s->scanner->sharpen)
          s->opt[OPT_SHARPEN].cap |= SANE_CAP_INACTIVE;
    }
    else {
       if (s->scanner->threshold)
       	  s->opt[OPT_THRESHOLD].cap  |= SANE_CAP_INACTIVE;
       if (s->scanner->brightness)
          s->opt[OPT_BRIGHTNESS].cap &= ~SANE_CAP_INACTIVE;
       if (s->scanner->contrast)
          s->opt[OPT_CONTRAST].cap   &= ~SANE_CAP_INACTIVE;
       if (s->scanner->sharpen)
          s->opt[OPT_SHARPEN].cap   &= ~SANE_CAP_INACTIVE;
    }
    return (status);
}

SANE_Status
escl_parse_name(SANE_String_Const name, ESCL_Device *device)
{
    SANE_String_Const host = NULL;
    SANE_String_Const port_str = NULL;
    DBG(10, "escl_parse_name\n");
    if (name == NULL || device == NULL) {
        return SANE_STATUS_INVAL;
    }

    if (strncmp(name, "unix:", 5) == 0) {
        SANE_String_Const socket = name + 5;
        name = strchr(socket, ':');
        if (name == NULL)
            return SANE_STATUS_INVAL;
        device->unix_socket = strndup(socket, name - socket);
        name++;
    }

    if (strncmp(name, "https://", 8) == 0) {
        device->https = SANE_TRUE;
        device->type = strdup("https");
        host = name + 8;
    } else if (strncmp(name, "http://", 7) == 0) {
        device->https = SANE_FALSE;
        device->type = strdup("http");
        host = name + 7;
    } else {
        DBG(10, "Unknown URL scheme in %s", name);
        return SANE_STATUS_INVAL;
    }

    port_str = strchr(host, ':');
    if (port_str == NULL) {
        DBG(10, "Port missing from URL: %s", name);
        return SANE_STATUS_INVAL;
    }
    port_str++;
    device->port_nb = atoi(port_str);
    if (device->port_nb < 1 || device->port_nb > 65535) {
        DBG(10, "Invalid port number in URL: %s", name);
        return SANE_STATUS_INVAL;
    }

    device->ip_address = strndup(host, port_str - host - 1);
    return SANE_STATUS_GOOD;
}

static void
_get_hack(SANE_String_Const name, ESCL_Device *device)
{
  FILE *fp;
  SANE_Char line[PATH_MAX];
  DBG (3, "_get_hack: start\n");
  if (device->model_name &&
      (strcasestr(device->model_name, "LaserJet FlowMFP M578") ||
       strcasestr(device->model_name, "LaserJet MFP M630"))) {
       device->hack = curl_slist_append(NULL, "Host: localhost");
       DBG (3, "_get_hack: finish\n");
       return;
  }

  /* open configuration file */
  fp = sanei_config_open (ESCL_CONFIG_FILE);
  if (!fp)
    {
      DBG(4, "_get_hack: couldn't access %s\n", ESCL_CONFIG_FILE);
      DBG (3, "_get_hack: exit\n");
    }

  /* loop reading the configuration file, all line beginning by "option " are
   * parsed for value to store in configuration structure, other line are
   * used are device to try to attach
   */
  while (sanei_config_read (line, PATH_MAX, fp))
    {
       if (strstr(line, name)) {
          DBG (3, "_get_hack: idevice found\n");
	  if (strstr(line, "hack=localhost")) {
              DBG (3, "_get_hack: device found\n");
	      device->hack = curl_slist_append(NULL, "Host: localhost");
	  }
	  goto finish_hack;
       }
    }
finish_hack:
  DBG (3, "_get_hack: finish\n");
  fclose(fp);
}

static char*
_get_blacklist_pdf(void)
{
  FILE *fp;
  char *blacklist = NULL;
  SANE_Char line[PATH_MAX];

  /* open configuration file */
  fp = sanei_config_open (ESCL_CONFIG_FILE);
  if (!fp)
    {
      DBG(4, "_get_blacklit: couldn't access %s\n", ESCL_CONFIG_FILE);
      DBG (3, "_get_blacklist: exit\n");
    }

  /* loop reading the configuration file, all line beginning by "option " are
   * parsed for value to store in configuration structure, other line are
   * used are device to try to attach
   */
  while (sanei_config_read (line, PATH_MAX, fp))
    {
       if (!strncmp(line, "pdfblacklist", 12)) {
          blacklist = strdup(line);
	  goto finish_;
       }
    }
finish_:
  DBG (3, "_get_blacklist_pdf: finish\n");
  fclose(fp);
  return blacklist;
}


/**
 * \fn SANE_Status sane_open(SANE_String_Const name, SANE_Handle *h)
 * \brief Function that establishes a connection with the device named by 'name',
 *        and returns a 'handler' using 'SANE_Handle *h', representing it.
 *        Thus, it's this function that calls the 'escl_status' function firstly,
 *        then the 'escl_capabilities' function, and, after, the 'init_options' function.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_open(SANE_String_Const name, SANE_Handle *h)
{
    char *blacklist = NULL;
    DBG (10, "escl sane_open\n");
    SANE_Status status;
    escl_sane_t *handler = NULL;

    if (name == NULL)
        return (SANE_STATUS_INVAL);

    ESCL_Device *device = calloc(1, sizeof(ESCL_Device));
    if (device == NULL) {
        DBG (10, "Handle device allocation failure.\n");
        return SANE_STATUS_NO_MEM;
    }
    status = escl_parse_name(name, device);
    if (status != SANE_STATUS_GOOD) {
        escl_free_device(device);
        return status;
    }

    handler = (escl_sane_t *)calloc(1, sizeof(escl_sane_t));
    if (handler == NULL) {
        escl_free_device(device);
        return (SANE_STATUS_NO_MEM);
    }
    handler->device = device;  // Handler owns device now.
    blacklist = _get_blacklist_pdf();
    handler->scanner = escl_capabilities(device, blacklist, &status);
    if (status != SANE_STATUS_GOOD) {
        escl_free_handler(handler);
        return (status);
    }
    _get_hack(name, device);

    status = init_options(NULL, handler);
    if (status != SANE_STATUS_GOOD) {
        escl_free_handler(handler);
        return (status);
    }
    handler->ps.depth = 8;
    handler->ps.last_frame = SANE_TRUE;
    handler->ps.format = SANE_FRAME_RGB;
    handler->ps.pixels_per_line = MM_TO_PIXEL(handler->val[OPT_BR_X].w, 300.0);
    handler->ps.lines = MM_TO_PIXEL(handler->val[OPT_BR_Y].w, 300.0);
    handler->ps.bytes_per_line = handler->ps.pixels_per_line * 3;
    status = sane_get_parameters(handler, 0);
    if (status != SANE_STATUS_GOOD) {
        escl_free_handler(handler);
        return (status);
    }
    handler->cancel = SANE_FALSE;
    handler->write_scan_data = SANE_FALSE;
    handler->decompress_scan_data = SANE_FALSE;
    handler->end_read = SANE_FALSE;
    *h = handler;
    return (status);
}

/**
 * \fn void sane_cancel(SANE_Handle h)
 * \brief Function that's used to, immediately or as quickly as possible, cancel the currently
 *        pending operation of the device represented by 'SANE_Handle h'.
 *        This functions calls the 'escl_scanner' functions, that resets the scan operations.
 */
void
sane_cancel(SANE_Handle h)
{
    DBG (10, "escl sane_cancel\n");
    escl_sane_t *handler = h;
    if (handler->scanner->tmp)
    {
      fclose(handler->scanner->tmp);
      handler->scanner->tmp = NULL;
    }
    handler->scanner->work = SANE_FALSE;
    handler->cancel = SANE_TRUE;
    escl_scanner(handler->device, handler->scanner->scanJob, handler->result, SANE_TRUE);
    free(handler->result);
    handler->result = NULL;
    free(handler->scanner->scanJob);
    handler->scanner->scanJob = NULL;
}

/**
 * \fn void sane_close(SANE_Handle h)
 * \brief Function that closes the communication with the device represented by 'SANE_Handle h'.
 *        This function must release the resources that were allocated to the opening of 'h'.
 */
void
sane_close(SANE_Handle h)
{
    DBG (10, "escl sane_close\n");
    if (h != NULL) {
        escl_free_handler(h);
        h = NULL;
    }
}

/**
 * \fn const SANE_Option_Descriptor *sane_get_option_descriptor(SANE_Handle h, SANE_Int n)
 * \brief Function that retrieves a descriptor from the n number option of the scanner
 *        represented by 'h'.
 *        The descriptor remains valid until the machine is closed.
 *
 * \return s->opt + n
 */
const SANE_Option_Descriptor *
sane_get_option_descriptor(SANE_Handle h, SANE_Int n)
{
    DBG (10, "escl sane_get_option_descriptor\n");
    escl_sane_t *s = h;

    if ((unsigned) n >= NUM_OPTIONS || n < 0)
	return (0);
    return (&s->opt[n]);
}

/**
 * \fn SANE_Status sane_control_option(SANE_Handle h, SANE_Int n, SANE_Action a, void *v, SANE_Int *i)
 * \brief Function that defines the actions to perform for the 'n' option of the machine,
 *        represented by 'h', if the action is 'a'.
 *        There are 3 types of possible actions :
 *        --> SANE_ACTION_GET_VALUE: 'v' must be used to provide the value of the option.
 *        --> SANE_ACTION_SET_VALUE: The option must take the 'v' value.
 *        --> SANE_ACTION_SET_AUTO: The backend or machine must affect the option with an appropriate value.
 *        Moreover, the parameter 'i' is used to provide additional information about the state of
 *        'n' option if SANE_ACTION_SET_VALUE has been performed.
 *
 * \return SANE_STATUS_GOOD if everything is OK, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL
 */
SANE_Status
sane_control_option(SANE_Handle h, SANE_Int n, SANE_Action a, void *v, SANE_Int *i)
{
    DBG (10, "escl sane_control_option\n");
    escl_sane_t *handler = h;

    if (i)
	*i = 0;
    if (n >= NUM_OPTIONS || n < 0)
	return (SANE_STATUS_INVAL);
    if (a == SANE_ACTION_GET_VALUE) {
	switch (n) {
	case OPT_TL_X:
	case OPT_TL_Y:
	case OPT_BR_X:
	case OPT_BR_Y:
	case OPT_NUM_OPTS:
	case OPT_PREVIEW:
	case OPT_GRAY_PREVIEW:
	case OPT_RESOLUTION:
        case OPT_BRIGHTNESS:
        case OPT_CONTRAST:
        case OPT_SHARPEN:
	    *(SANE_Word *) v = handler->val[n].w;
	    break;
	case OPT_SCAN_SOURCE:
	case OPT_MODE:
	    strcpy (v, handler->val[n].s);
	    break;
	case OPT_MODE_GROUP:
	default:
	    break;
	}
	return (SANE_STATUS_GOOD);
    }
    if (a == SANE_ACTION_SET_VALUE) {
	switch (n) {
	case OPT_TL_X:
	case OPT_TL_Y:
	case OPT_BR_X:
	case OPT_BR_Y:
	case OPT_NUM_OPTS:
	case OPT_PREVIEW:
	case OPT_GRAY_PREVIEW:
        case OPT_BRIGHTNESS:
        case OPT_CONTRAST:
        case OPT_SHARPEN:
	    handler->val[n].w = *(SANE_Word *) v;
	    if (i)
		*i |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS | SANE_INFO_INEXACT;
	    break;
	case OPT_SCAN_SOURCE:
	    DBG(10, "SET OPT_SCAN_SOURCE(%s)\n", (SANE_String_Const)v);
	    init_options_small((SANE_String_Const)v, handler);
	    if (i)
		*i |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS | SANE_INFO_INEXACT;
	    break;
	case OPT_MODE:
	    if (handler->val[n].s)
		free (handler->val[n].s);
	    handler->val[n].s = strdup (v);
	    if (!handler->val[n].s) {
	      DBG (10, "OPT_MODE allocation failure.\n");
	      return (SANE_STATUS_NO_MEM);
	    }
	    DBG(10, "SET OPT_MODE(%s)\n", (SANE_String_Const)v);

            if (!strcasecmp(handler->val[n].s, SANE_VALUE_SCAN_MODE_GRAY)) {
              handler->scanner->caps[handler->scanner->source].default_color = strdup("Grayscale8");
	    DBG(10, "SET OPT_MODE(Grayscale8)\n");
            }
            else if (!strcasecmp(handler->val[n].s, SANE_VALUE_SCAN_MODE_LINEART)) {
              handler->scanner->caps[handler->scanner->source].default_color =
                 strdup("BlackAndWhite1");
	    DBG(10, "SET OPT_MODE(BlackAndWhite1)\n");
            }
            else {
              handler->scanner->caps[handler->scanner->source].default_color =
                 strdup("RGB24");
	         DBG(10, "SET OPT_MODE(RGB24)\n");
            }
            DBG (10, "Color Mode allocation (%s).\n", handler->scanner->caps[handler->scanner->source].default_color);
	    if (i)
		*i |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS | SANE_INFO_INEXACT;
            if (handler->scanner->brightness)
                handler->opt[OPT_BRIGHTNESS].cap |= SANE_CAP_INACTIVE;
            if (handler->scanner->contrast)
                handler->opt[OPT_CONTRAST].cap   |= SANE_CAP_INACTIVE;
            if (handler->scanner->threshold)
                handler->opt[OPT_THRESHOLD].cap  |= SANE_CAP_INACTIVE;
            if (handler->scanner->sharpen)
                handler->opt[OPT_SHARPEN].cap  |= SANE_CAP_INACTIVE;
            if (!strcasecmp(handler->val[n].s, SANE_VALUE_SCAN_MODE_LINEART)) {
               if (handler->scanner->threshold)
                  handler->opt[OPT_THRESHOLD].cap  &= ~SANE_CAP_INACTIVE;
            }
            else {
               if (handler->scanner->brightness)
                  handler->opt[OPT_BRIGHTNESS].cap &= ~SANE_CAP_INACTIVE;
               if (handler->scanner->contrast)
                  handler->opt[OPT_CONTRAST].cap   &= ~SANE_CAP_INACTIVE;
               if (handler->scanner->sharpen)
                  handler->opt[OPT_SHARPEN].cap   &= ~SANE_CAP_INACTIVE;
            }
	    break;
	case OPT_RESOLUTION:
            handler->val[n].w = _get_resolution(handler, (int)(*(SANE_Word *) v));
	    handler->scanner->caps[handler->scanner->source].default_resolution = handler->val[n].w;
	    if (i)
		*i |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS | SANE_INFO_INEXACT;
	    break;
	default:
	    break;
	}
    }
    return (SANE_STATUS_GOOD);
}

static SANE_Bool
_go_next_page(SANE_Status status,
              SANE_Status job)
{
   // Thank's Alexander Pevzner (pzz@apevzner.com)
   SANE_Status st = SANE_STATUS_NO_DOCS;
   switch (status) {
      case SANE_STATUS_GOOD:
      case SANE_STATUS_UNSUPPORTED:
      case SANE_STATUS_DEVICE_BUSY: {
         DBG(10, "eSCL : Test next page\n");
         if (job != SANE_STATUS_GOOD) {
            DBG(10, "eSCL : Go next page\n");
            st = SANE_STATUS_GOOD;
         }
         break;
      }
      default:
         DBG(10, "eSCL : No next page\n");
   }
   return st;
}

/**
 * \fn SANE_Status sane_start(SANE_Handle h)
 * \brief Function that initiates acquisition of an image from the device represented by handle 'h'.
 *        This function calls the "escl_newjob" function and the "escl_scan" function.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_start(SANE_Handle h)
{
    DBG (10, "escl sane_start\n");
    SANE_Status status = SANE_STATUS_GOOD;
    escl_sane_t *handler = h;
    int w = 0;
    int he = 0;
    int bps = 0;

    if (handler->device == NULL) {
        DBG(10, "Missing handler device.\n");
        return (SANE_STATUS_INVAL);
    }
    handler->cancel = SANE_FALSE;
    handler->write_scan_data = SANE_FALSE;
    handler->decompress_scan_data = SANE_FALSE;
    handler->end_read = SANE_FALSE;
    if (handler->scanner->work == SANE_FALSE) {
       escl_reset_all_jobs(handler->device);
       SANE_Status st = escl_status(handler->device,
                                    handler->scanner->source,
                                    NULL,
                                    NULL);
       if (st != SANE_STATUS_GOOD)
          return st;
       if (handler->val[OPT_PREVIEW].w == SANE_TRUE)
       {
          int i = 0, val = 9999;

          if(handler->scanner->caps[handler->scanner->source].default_color)
             free(handler->scanner->caps[handler->scanner->source].default_color);

          if (handler->val[OPT_GRAY_PREVIEW].w == SANE_TRUE ||
	      !strcasecmp(handler->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY))
	     handler->scanner->caps[handler->scanner->source].default_color =
	          strdup("Grayscale8");
          else
	     handler->scanner->caps[handler->scanner->source].default_color =
	          strdup("RGB24");
          if (!handler->scanner->caps[handler->scanner->source].default_color) {
	     DBG (10, "Default Color allocation failure.\n");
	     return (SANE_STATUS_NO_MEM);
	  }
          for (i = 1; i < handler->scanner->caps[handler->scanner->source].SupportedResolutionsSize; i++)
          {
	     if (val > handler->scanner->caps[handler->scanner->source].SupportedResolutions[i])
	         val = handler->scanner->caps[handler->scanner->source].SupportedResolutions[i];
          }
          handler->scanner->caps[handler->scanner->source].default_resolution = val;
       }
       else
       {
          handler->scanner->caps[handler->scanner->source].default_resolution =
	     handler->val[OPT_RESOLUTION].w;
          if (!handler->scanner->caps[handler->scanner->source].default_color) {
             if (!strcasecmp(handler->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY))
	        handler->scanner->caps[handler->scanner->source].default_color = strdup("Grayscale8");
             else if (!strcasecmp(handler->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_LINEART))
	        handler->scanner->caps[handler->scanner->source].default_color =
	            strdup("BlackAndWhite1");
             else
	        handler->scanner->caps[handler->scanner->source].default_color =
	            strdup("RGB24");
          }
       }
       DBG (10, "Before newjob Color Mode allocation (%s).\n", handler->scanner->caps[handler->scanner->source].default_color);
       handler->scanner->caps[handler->scanner->source].height =
            MM_TO_PIXEL(handler->val[OPT_BR_Y].w, 300.0);
       handler->scanner->caps[handler->scanner->source].width =
            MM_TO_PIXEL(handler->val[OPT_BR_X].w, 300.0);;
       if (handler->x_range1.min == handler->val[OPT_TL_X].w)
           handler->scanner->caps[handler->scanner->source].pos_x = 0;
       else
           handler->scanner->caps[handler->scanner->source].pos_x =
               MM_TO_PIXEL((handler->val[OPT_TL_X].w - handler->x_range1.min),
               300.0);
       if (handler->y_range1.min == handler->val[OPT_TL_X].w)
           handler->scanner->caps[handler->scanner->source].pos_y = 0;
       else
           handler->scanner->caps[handler->scanner->source].pos_y =
               MM_TO_PIXEL((handler->val[OPT_TL_Y].w - handler->y_range1.min),
               300.0);
       DBG(10, "Calculate Size Image [%dx%d|%dx%d]\n",
	        handler->scanner->caps[handler->scanner->source].pos_x,
	        handler->scanner->caps[handler->scanner->source].pos_y,
	        handler->scanner->caps[handler->scanner->source].width,
	        handler->scanner->caps[handler->scanner->source].height);
       if (!handler->scanner->caps[handler->scanner->source].default_color) {
          DBG (10, "Default Color allocation failure.\n");
          return (SANE_STATUS_NO_MEM);
       }

       if (handler->scanner->threshold) {
          DBG(10, "Have Thresold\n");
          if (IS_ACTIVE(OPT_THRESHOLD)) {
            DBG(10, "Use Thresold [%d]\n", handler->val[OPT_THRESHOLD].w);
            handler->scanner->val_threshold = handler->val[OPT_THRESHOLD].w;
            handler->scanner->use_threshold = 1;
         }
         else  {
            DBG(10, "Not use Thresold\n");
            handler->scanner->use_threshold = 0;
         }
       }
       else
          DBG(10, "Don't have Thresold\n");

       if (handler->scanner->sharpen) {
          DBG(10, "Have Sharpen\n");
           if (IS_ACTIVE(OPT_SHARPEN)) {
             DBG(10, "Use Sharpen [%d]\n", handler->val[OPT_SHARPEN].w);
             handler->scanner->val_sharpen = handler->val[OPT_SHARPEN].w;
             handler->scanner->use_sharpen = 1;
          }
         else  {
            DBG(10, "Not use Sharpen\n");
            handler->scanner->use_sharpen = 0;
         }
       }
       else
          DBG(10, "Don't have Sharpen\n");

       if (handler->scanner->contrast) {
          DBG(10, "Have Contrast\n");
          if (IS_ACTIVE(OPT_CONTRAST)) {
             DBG(10, "Use Contrast [%d]\n", handler->val[OPT_CONTRAST].w);
             handler->scanner->val_contrast = handler->val[OPT_CONTRAST].w;
             handler->scanner->use_contrast = 1;
          }
          else  {
             DBG(10, "Not use Contrast\n");
             handler->scanner->use_contrast = 0;
          }
       }
       else
          DBG(10, "Don't have Contrast\n");

       if (handler->scanner->brightness) {
          DBG(10, "Have Brightness\n");
          if (IS_ACTIVE(OPT_BRIGHTNESS)) {
             DBG(10, "Use Brightness [%d]\n", handler->val[OPT_BRIGHTNESS].w);
             handler->scanner->val_brightness = handler->val[OPT_BRIGHTNESS].w;
             handler->scanner->use_brightness = 1;
          }
          else  {
             DBG(10, "Not use Brightness\n");
             handler->scanner->use_brightness = 0;
          }
       }
       else
          DBG(10, "Don't have Brightness\n");

       handler->result = escl_newjob(handler->scanner, handler->device, &status);
       if (status != SANE_STATUS_GOOD)
          return (status);
    }
    else
    {
       SANE_Status job = SANE_STATUS_UNSUPPORTED;
       SANE_Status st = escl_status(handler->device,
                                       handler->scanner->source,
                                       handler->result,
                                       &job);
       DBG(10, "eSCL : command returned status %s\n", sane_strstatus(st));
       if (_go_next_page(st, job) != SANE_STATUS_GOOD)
       {
         handler->scanner->work = SANE_FALSE;
         return SANE_STATUS_NO_DOCS;
       }
    }
    status = escl_scan(handler->scanner, handler->device, handler->scanner->scanJob, handler->result);
    if (status != SANE_STATUS_GOOD)
       return (status);
    if (!strcmp(handler->scanner->caps[handler->scanner->source].default_format, "image/jpeg"))
    {
       status = get_JPEG_data(handler->scanner, &w, &he, &bps);
    }
    else if (!strcmp(handler->scanner->caps[handler->scanner->source].default_format, "image/png"))
    {
       status = get_PNG_data(handler->scanner, &w, &he, &bps);
    }
    else if (!strcmp(handler->scanner->caps[handler->scanner->source].default_format, "image/tiff"))
    {
       status = get_TIFF_data(handler->scanner, &w, &he, &bps);
    }
    else if (!strcmp(handler->scanner->caps[handler->scanner->source].default_format, "application/pdf"))
    {
       status = get_PDF_data(handler->scanner, &w, &he, &bps);
    }
    else {
       DBG(10, "Unknown image format\n");
       return SANE_STATUS_INVAL;
    }

    DBG(10, "2-Size Image (%ld)[%dx%d|%dx%d]\n", handler->scanner->img_size, 0, 0, w, he);

    if (status != SANE_STATUS_GOOD)
       return (status);
    handler->ps.depth = 8;
    handler->ps.pixels_per_line = w;
    handler->ps.lines = he;
    handler->ps.bytes_per_line = w * bps;
    handler->ps.last_frame = SANE_TRUE;
    handler->ps.format = SANE_FRAME_RGB;
    handler->scanner->work = SANE_FALSE;
//    DBG(10, "NEXT Frame [%s]\n", (handler->ps.last_frame ? "Non" : "Oui"));
    DBG(10, "Real Size Image [%dx%d|%dx%d]\n", 0, 0, w, he);
    return (status);
}

/**
 * \fn SANE_Status sane_get_parameters(SANE_Handle h, SANE_Parameters *p)
 * \brief Function that retrieves the device parameters represented by 'h' and stores them in 'p'.
 *        This function is normally used after "sane_start".
 *        It's in this function that we choose to assign the default color. (Color or Monochrome)
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_get_parameters(SANE_Handle h, SANE_Parameters *p)
{
    DBG (10, "escl sane_get_parameters\n");
    SANE_Status status = SANE_STATUS_GOOD;
    escl_sane_t *handler = h;

    if (status != SANE_STATUS_GOOD)
        return (status);
    if (p != NULL) {
        p->depth = 8;
        p->last_frame = handler->ps.last_frame;
        p->format = SANE_FRAME_RGB;
        p->pixels_per_line = handler->ps.pixels_per_line;
        p->lines = handler->ps.lines;
        p->bytes_per_line = handler->ps.bytes_per_line;
    }
    return (status);
}


/**
 * \fn SANE_Status sane_read(SANE_Handle h, SANE_Byte *buf, SANE_Int maxlen, SANE_Int *len)
 * \brief Function that's used to read image data from the device represented by handle 'h'.
 *        The argument 'buf' is a pointer to a memory area that is at least 'maxlen' bytes long.
 *        The number of bytes returned is stored in '*len'.
 *        --> When the call succeeds, the number of bytes returned can be anywhere in the range from 0 to 'maxlen' bytes.
 *
 * \return SANE_STATUS_GOOD (if everything is OK, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_read(SANE_Handle h, SANE_Byte *buf, SANE_Int maxlen, SANE_Int *len)
{
    DBG (10, "escl sane_read\n");
    escl_sane_t *handler = h;
    SANE_Status status = SANE_STATUS_GOOD;
    long readbyte;

    if (!handler | !buf | !len)
        return (SANE_STATUS_INVAL);

    if (handler->cancel)
        return (SANE_STATUS_CANCELLED);
    if (!handler->write_scan_data)
        handler->write_scan_data = SANE_TRUE;
    if (!handler->decompress_scan_data) {
        if (status != SANE_STATUS_GOOD)
            return (status);
        handler->decompress_scan_data = SANE_TRUE;
    }
    if (handler->scanner->img_data == NULL)
        return (SANE_STATUS_INVAL);
    if (!handler->end_read) {
        readbyte = min((handler->scanner->img_size - handler->scanner->img_read), maxlen);
        memcpy(buf, handler->scanner->img_data + handler->scanner->img_read, readbyte);
        handler->scanner->img_read = handler->scanner->img_read + readbyte;
        *len = readbyte;
        if (handler->scanner->img_read == handler->scanner->img_size)
            handler->end_read = SANE_TRUE;
        else if (handler->scanner->img_read > handler->scanner->img_size) {
            *len = 0;
            handler->end_read = SANE_TRUE;
            free(handler->scanner->img_data);
            handler->scanner->img_data = NULL;
            return (SANE_STATUS_INVAL);
        }
    }
    else {
        SANE_Status job = SANE_STATUS_UNSUPPORTED;
        *len = 0;
        free(handler->scanner->img_data);
        handler->scanner->img_data = NULL;
        if (handler->scanner->source != PLATEN) {
	      SANE_Bool next_page = SANE_FALSE;
          SANE_Status st = escl_status(handler->device,
                                       handler->scanner->source,
                                       handler->result,
                                       &job);
          DBG(10, "eSCL : command returned status %s\n", sane_strstatus(st));
          if (_go_next_page(st, job) == SANE_STATUS_GOOD)
	     next_page = SANE_TRUE;
          handler->scanner->work = SANE_TRUE;
          handler->ps.last_frame = !next_page;
        }
        return SANE_STATUS_EOF;
    }
    return (SANE_STATUS_GOOD);
}

SANE_Status
sane_get_select_fd(SANE_Handle __sane_unused__ h, SANE_Int __sane_unused__ *fd)
{
    return (SANE_STATUS_UNSUPPORTED);
}

SANE_Status
sane_set_io_mode(SANE_Handle __sane_unused__ handle, SANE_Bool __sane_unused__ non_blocking)
{
    return (SANE_STATUS_UNSUPPORTED);
}

/**
 * \fn void escl_curl_url(CURL *handle, const ESCL_Device *device, SANE_String_Const path)
 * \brief Uses the device info in 'device' and the path from 'path' to construct
 *        a full URL.  Sets this URL and any necessary connection options into
 *        'handle'.
 */
void
escl_curl_url(CURL *handle, const ESCL_Device *device, SANE_String_Const path)
{
    int url_len;
    char *url;

    url_len = snprintf(NULL, 0, "%s://%s:%d%s",
                       (device->https ? "https" : "http"), device->ip_address,
                       device->port_nb, path);
    url_len++;
    url = (char *)malloc(url_len);
    snprintf(url, url_len, "%s://%s:%d%s",
             (device->https ? "https" : "http"), device->ip_address,
             device->port_nb, path);

    DBG( 10, "escl_curl_url: URL: %s\n", url );
    curl_easy_setopt(handle, CURLOPT_URL, url);
    free(url);
    DBG( 10, "Before use hack\n");
    if (device->hack) {
        DBG( 10, "Use hack\n");
        curl_easy_setopt(handle, CURLOPT_HTTPHEADER, device->hack);
    }
    DBG( 10, "After use hack\n");
    if (device->https) {
        DBG( 10, "Ignoring safety certificates, use https\n");
        curl_easy_setopt(handle, CURLOPT_USE_SSL, (long)CURLUSESSL_TRY);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    if (device->unix_socket != NULL) {
        DBG( 10, "Using local socket %s\n", device->unix_socket );
        curl_easy_setopt(handle, CURLOPT_UNIX_SOCKET_PATH,
                         device->unix_socket);
    }
}
