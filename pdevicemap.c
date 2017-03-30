#include "pdevicemap.h"
#include "plibs.h"
#include "pdevice_monitor.h"

#include <search.h>
#include <string.h>

extern device_event_callback *callbacks;

extern int clbsize = 10;
extern int clbnum = 0;

static void *stree_root = 0;
static int dev_num = 0;

static int key_compar(const void *l, const void *r)
{
  const pdevice_info *lm = l;
  const pdevice_info *lr = r;
  return strcmp(lm->filesystem_path, lr->filesystem_path);
}

static int ext_compar(const void *l, const void *r)
{
  const pdevice_extended_info *lm = l;
  const pdevice_extended_info *lr = r;
  if (lm->isextended && lr->isextended)
    return strcmp(lm->filesystem_path, lr->filesystem_path)<<27 +  strcmp(lm->device_id, lr->device_id)<<18 + strcmp(lm->vendor, lr->vendor)<<9 + strcmp(lm->product, lr->product);
  return strcmp(lm->filesystem_path, lr->filesystem_path);
}

static void do_notify_device_callbacks(void * param, device_event event) {
  int i = 0; 
  while (i < clbnum) {
    device_event_callback c = callbacks[i];
    c(param, event);
    i++;
  }
};

pdevice_extended_info* construct_deviceininfo( pdevice_types type,  device_event event, int isextended, char *filesystem_path, 
                                               char *vendor, char *product, char *device_id);
void destruct_deviceininfo(pdevice_extended_info* device);

void do_notify_device_callbacks_in(void * param) {
  do_notify_device_callbacks(param, Dev_Event_arrival);
}
void do_notify_device_callbacks_out(void * param) {
  do_notify_device_callbacks(param, Dev_Event_removed);
  destruct_deviceininfo((pdevice_extended_info*)param);
}

pdevice_extended_info* construct_deviceininfo( pdevice_types type,  device_event event, int isextended, char *filesystem_path, 
                                               char *vendor, char *product, char *device_id){
  pdevice_extended_info* ret;
  if (isextended){
    ret = (pdevice_extended_info*)psync_malloc(sizeof(pdevice_extended_info));
    ret->vendor = psync_strdup(vendor);
    ret->product = psync_strdup(product);
    ret->device_id = psync_strdup(device_id);
  }else {
    ret = (pdevice_extended_info*)psync_malloc(sizeof(pdevice_info));
  }
  ret->type = type;
  ret->isextended = isextended;
  ret->filesystem_path = psync_strdup(filesystem_path);
  return ret;
}

void destruct_deviceininfo(pdevice_extended_info* device) {
  psync_free (device->filesystem_path);
  if (device->isextended) {
    psync_free (device->vendor);
    psync_free (device->product);
    psync_free (device->device_id);
    psync_free (device);
  } else psync_free ((pdevice_info*) device);
}

void add_device (pdevice_types type,  device_event event, int isextended, char *filesystem_path, char *vendor, char *product, char *device_id)
{
  psync_sql_res *q;
  pdevice_extended_info* data = construct_deviceininfo(type, event, isextended, filesystem_path, vendor,  product, device_id);
  
  pdevice_extended_info * key = tfind(data, &stree_root, key_compar);
  if (key) {
    if (ext_compar(key, data)) {
      pnotify_device_callbacks(key, Dev_Event_removed);
      tdelete(key, &stree_root, key_compar);
      if (key->isextended && key->device_id) {
        q=psync_sql_prep_statement("DELETE FROM devices WHERE id = ? ");
        psync_sql_bind_string(q, 1, key->device_id);
        psync_sql_run_free(q);
      }
      psync_free(key);
    } else {
      psync_free (data);
      return;
    }
  }
  tsearch(data, &stree_root, key_compar);
  pnotify_device_callbacks(data, Dev_Event_arrival);
}

void remove_device (pdevice_types type,  device_event event, int isextended, char *filesystem_path)
{
  pdevice_extended_info* data = construct_deviceininfo(type, event, 0, filesystem_path, "",  "", "");
  
  pdevice_extended_info * key = tfind(data, &stree_root, key_compar);
  if (key) {
    pnotify_device_callbacks(key, Dev_Event_removed);
    tdelete(key, &stree_root, key_compar);
    psync_free(key);
  }
  psync_free(data);
}

