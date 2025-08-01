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

#define DEBUG_DECLARE_ONLY
#include "../include/sane/config.h"

#include "escl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libxml/parser.h>

struct idle
{
    char *memory;
    size_t size;
};

/**
 * \fn static size_t memory_callback_s(void *contents, size_t size, size_t nmemb, void *userp)
 * \brief Callback function that stocks in memory the content of the scanner status.
 *
 * \return realsize (size of the content needed -> the scanner status)
 */
static size_t
memory_callback_s(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct idle *mem = (struct idle *)userp;

    char *str = realloc(mem->memory, mem->size + realsize + 1);
    if (str == NULL) {
        DBG(10, "not enough memory (realloc returned NULL)\n");
        return (0);
    }
    mem->memory = str;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size = mem->size + realsize;
    mem->memory[mem->size] = 0;
    return (realsize);
}

/**
 * \fn static int find_nodes_s(xmlNode *node)
 * \brief Function that browses the xml file and parses it, to find the xml children node.
 *        --> to recover the scanner status.
 *
 * \return 0 if a xml child node is found, 1 otherwise
 */
static int
find_nodes_s(xmlNode *node)
{
    xmlNode *child = node->children;

    while (child) {
        if (child->type == XML_ELEMENT_NODE)
            return (0);
        child = child->next;
    }
    return (1);
}

static void
print_xml_job_status(xmlNode *node,
                     SANE_Status *job,
                     int *image)
{
    while (node) {
        if (node->type == XML_ELEMENT_NODE) {
            if (find_nodes_s(node)) {
                if (strcmp((const char *)node->name, "JobState") == 0) {
                    const char *state = (const char *)xmlNodeGetContent(node);
                    if (!strcmp(state, "Processing")) {
                        *job = SANE_STATUS_DEVICE_BUSY;
                        DBG(10, "jobId Processing SANE_STATUS_DEVICE_BUSY\n");
                    }
                    else if (!strcmp(state, "Completed")) {
                        *job = SANE_STATUS_GOOD;
                        DBG(10, "jobId Completed SANE_STATUS_GOOD\n");
                    }
                    else if (strcmp((const char *)node->name, "ImagesToTransfer") == 0) {
	                const char *state = (const char *)xmlNodeGetContent(node);
	                *image = atoi(state);
	            }
                }
            }
        }
        print_xml_job_status(node->children, job, image);
        node = node->next;
    }
}

static void
print_xml_platen_and_adf_status(xmlNode *node,
                                SANE_Status *platen,
                                SANE_Status *adf,
                                const char* jobId,
                                SANE_Status *job,
                                int *image)
{
    while (node) {
        if (node->type == XML_ELEMENT_NODE) {
            if (find_nodes_s(node)) {
                if (strcmp((const char *)node->name, "State") == 0) {
	            DBG(10, "State\t");
                    const char *state = (const char *)xmlNodeGetContent(node);
                    if (!strcmp(state, "Idle")) {
			DBG(10, "Idle SANE_STATUS_GOOD\n");
                        *platen = SANE_STATUS_GOOD;
                    } else if (!strcmp(state, "Processing")) {
			DBG(10, "Processing SANE_STATUS_DEVICE_BUSY\n");
                        *platen = SANE_STATUS_DEVICE_BUSY;
                    } else {
			DBG(10, "%s SANE_STATUS_UNSUPPORTED\n", state);
                        *platen = SANE_STATUS_UNSUPPORTED;
                    }
                }
                // Thank's Alexander Pevzner (pzz@apevzner.com)
                else if (adf && strcmp((const char *)node->name, "AdfState") == 0) {
                    const char *state = (const char *)xmlNodeGetContent(node);
                    if (!strcmp(state, "ScannerAdfLoaded")){
			DBG(10, "ScannerAdfLoaded SANE_STATUS_GOOD\n");
                        *adf = SANE_STATUS_GOOD;
                    } else if (!strcmp(state, "ScannerAdfJam")) {
                        DBG(10, "ScannerAdfJam SANE_STATUS_JAMMED\n");
                        *adf = SANE_STATUS_JAMMED;
                    } else if (!strcmp(state, "ScannerAdfDoorOpen")) {
                        DBG(10, "ScannerAdfDoorOpen SANE_STATUS_COVER_OPEN\n");
                        *adf = SANE_STATUS_COVER_OPEN;
                    } else if (!strcmp(state, "ScannerAdfProcessing")) {
                        /* Kyocera version */
                        DBG(10, "ScannerAdfProcessing SANE_STATUS_NO_DOC\n");
                        *adf = SANE_STATUS_NO_DOCS;
                    } else if (!strcmp(state, "ScannerAdfEmpty")) {
                        DBG(10, "ScannerAdfEmpty SANE_STATUS_NO_DOCS\n");
                        /* Cannon TR4500, EPSON XP-7100 */
                        *adf = SANE_STATUS_NO_DOCS;
                    } else {
                        DBG(10, "%s SANE_STATUS_NO_DOCS\n", state);
                        *adf = SANE_STATUS_UNSUPPORTED;
                    }
                }
                else if (jobId && job && strcmp((const char *)node->name, "JobUri") == 0) {
                    if (strstr((const char *)xmlNodeGetContent(node), jobId)) {
						print_xml_job_status(node, job, image);
					}
                }
            }
        }
        print_xml_platen_and_adf_status(node->children,
                                        platen,
                                        adf,
                                        jobId,
                                        job,
                                        image);
        node = node->next;
    }
}

/**
 * \fn SANE_Status escl_status(const ESCL_Device *device)
 * \brief Function that finally recovers the scanner status ('Idle', or not), using curl.
 *        This function is called in the 'sane_open' function and it's the equivalent of
 *        the following curl command : "curl http(s)://'ip':'port'/eSCL/ScannerStatus".
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
escl_status(const ESCL_Device *device,
            int source,
            const char* jobId,
            SANE_Status *job)
{
    SANE_Status status = SANE_STATUS_DEVICE_BUSY;
    SANE_Status platen= SANE_STATUS_DEVICE_BUSY;
    SANE_Status adf= SANE_STATUS_DEVICE_BUSY;
    CURL *curl_handle = NULL;
    struct idle *var = NULL;
    xmlDoc *data = NULL;
    xmlNode *node = NULL;
    const char *scanner_status = "/eSCL/ScannerStatus";
    int image = -1;
    int pass = 0;
reload:

    if (device == NULL)
        return (SANE_STATUS_NO_MEM);
    status = SANE_STATUS_DEVICE_BUSY;
    platen= SANE_STATUS_DEVICE_BUSY;
    adf= SANE_STATUS_DEVICE_BUSY;
    var = (struct idle*)calloc(1, sizeof(struct idle));
    if (var == NULL)
        return (SANE_STATUS_NO_MEM);
    var->memory = malloc(1);
    var->size = 0;
    curl_handle = curl_easy_init();

    escl_curl_url(curl_handle, device, scanner_status);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, memory_callback_s);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)var);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 3L);
    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        DBG( 10, "The scanner didn't respond: %s\n", curl_easy_strerror(res));
        status = SANE_STATUS_INVAL;
        goto clean_data;
    }
    DBG( 10, "eSCL : Status : %s.\n", var->memory);
    data = xmlReadMemory(var->memory, var->size, "file.xml", NULL, 0);
    if (data == NULL) {
        status = SANE_STATUS_NO_MEM;
        goto clean_data;
    }
    node = xmlDocGetRootElement(data);
    if (node == NULL) {
        status = SANE_STATUS_NO_MEM;
        goto clean;
    }
    /* Decode Job status */
    // Thank's Alexander Pevzner (pzz@apevzner.com)
    print_xml_platen_and_adf_status(node, &platen, &adf, jobId, job, &image);
    if (platen != SANE_STATUS_GOOD &&
        platen != SANE_STATUS_UNSUPPORTED) {
        status = platen;
    } else if (source == PLATEN) {
        status = platen;
    } else {
        status = adf;
    }
    DBG (10, "STATUS : %s\n", sane_strstatus(status));
clean:
    xmlFreeDoc(data);
clean_data:
    xmlCleanupParser();
    xmlMemoryDump();
    curl_easy_cleanup(curl_handle);
    free(var->memory);
    free(var);
    if (pass == 0 &&
        source != PLATEN &&
        image == 0 &&
        (status == SANE_STATUS_GOOD ||
         status == SANE_STATUS_UNSUPPORTED ||
         status == SANE_STATUS_DEVICE_BUSY)) {
       pass = 1;
       goto reload;
    }
    return (status);
}

static void
print_xml_job_finish(xmlNode *node,
                     SANE_Status *job)
{
    while (node) {
        if (node->type == XML_ELEMENT_NODE) {
            if (find_nodes_s(node)) {
                if (strcmp((const char *)node->name, "JobState") == 0) {
                    const char *state = (const char *)xmlNodeGetContent(node);
                    if (!strcmp(state, "Canceled")) {
                        *job = SANE_STATUS_GOOD;
                        DBG(10, "jobId Completed SANE_STATUS_GOOD\n");
                    }
                    else if (!strcmp(state, "Aborted")) {
                        *job = SANE_STATUS_GOOD;
                        DBG(10, "jobId Completed SANE_STATUS_GOOD\n");
                    }
                    else if (!strcmp(state, "Completed")) {
                        *job = SANE_STATUS_GOOD;
                        DBG(10, "jobId Completed SANE_STATUS_GOOD\n");
                    }
                }
            }
        }
        print_xml_job_finish(node->children, job);
        node = node->next;
    }
}

static void
print_xml_reset_all_jobs (xmlNode *node,
                          ESCL_Device *device)
{
    DBG(10, "print_xml_reset_all_jobs\n");
    SANE_Status status = SANE_STATUS_DEVICE_BUSY;
    while (node) {
        if (node->type == XML_ELEMENT_NODE) {
            if (find_nodes_s(node)) {
                if (strcmp((const char *)node->name, "JobUri") == 0) {
                    DBG(10, "print_xml_reset_all_jobs: %s\n", node->name);
		    if (device != NULL) {
			print_xml_job_finish (node, &status);
			if (status == SANE_STATUS_DEVICE_BUSY) {
			    char *jobUri = (char *)xmlNodeGetContent(node);
			    char *job = strrchr((const char *)jobUri, '/');
			    char *scanj = NULL;
			    if (job != NULL) {
			        if (strstr(jobUri,"ScanJobs"))
			           scanj = strdup("ScanJobs");
			        else
			           scanj = strdup("ScanJob");
                                DBG(10, "print_xml_reset_all_jobs: %s/%s\n", scanj, job);
                                escl_scanner(device, scanj, job, SANE_FALSE);
			        free(scanj);
			    }
                            DBG(10, "print_xml_reset_all_jobs: sleep to finish the job\n");
		        }
		    }
                }
            }
        }
        print_xml_reset_all_jobs (node->children,
                                  device);
        node = node->next;
    }
}

/**
 * \fn SANE_Status escl_reset_all_jobs (ESCL_Device *device, , char *scanJob)
 * \brief Function that forces the end of jobs, using curl.
 *          This function is called in the 'sane_start' function.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
escl_reset_all_jobs(ESCL_Device *device)
{
    CURL *curl_handle = NULL;
    xmlDoc *data = NULL;
    xmlNode *node = NULL;
    struct idle *var = NULL;
    const char *scanner_status = "/eSCL/ScannerStatus";
    SANE_Status status = SANE_STATUS_DEVICE_BUSY;

    DBG(10, "escl_reset_all_jobs\n");
    if (device == NULL)
        return (SANE_STATUS_NO_MEM);
    DBG(10, "1 - escl_reset_all_jobs\n");
    var = (struct idle*)calloc(1, sizeof(struct idle));
    if (var == NULL)
        return (SANE_STATUS_NO_MEM);
    DBG(10, "2 - escl_reset_all_jobs\n");
    var->memory = malloc(1);
    var->size = 0;
    curl_handle = curl_easy_init();

    escl_curl_url(curl_handle, device, scanner_status);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, memory_callback_s);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)var);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 3L);
    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        DBG( 10, "The scanner didn't respond: %s\n", curl_easy_strerror(res));
        status = SANE_STATUS_INVAL;
        goto clean_data1;
    }
    DBG(10, "3 - escl_reset_all_jobs\n");
    DBG( 10, "eSCL : Status : %s.\n", var->memory);
    data = xmlReadMemory(var->memory, var->size, "file.xml", NULL, 0);
    if (data == NULL) {
        status = SANE_STATUS_NO_MEM;
        goto clean_data1;
    }
    node = xmlDocGetRootElement(data);
    if (node == NULL) {
        status = SANE_STATUS_NO_MEM;
        goto clean1;
    }
    print_xml_reset_all_jobs (node, device);
    status = SANE_STATUS_GOOD;
clean1:
    xmlFreeDoc(data);
clean_data1:
    xmlCleanupParser();
    xmlMemoryDump();
    curl_easy_cleanup(curl_handle);
    free(var->memory);
    free(var);
    return status;
}
