#pragma once

#ifndef _PDEVICE_MAP
#define _PDEVICE_MAP
#include "pdevice_monitor.h"
void do_notify_device_callbacks_in(void * param);
void do_notify_device_callbacks_out(void * param);
void add_device (pdevice_types type, int isextended, char *filesystem_path, char *vendor, char *product, char *device_id);
void remove_device (pdevice_types type, char *filesystem_path);
#endif //_PDEVICE_MAP