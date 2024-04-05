/* Copyright (c) 2013-2014 Anton Titov.
 * Copyright (c) 2013-2014 pCloud Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of pCloud Ltd nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL pCloud Ltd BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include "pcallbacks.h"
#include "pcompat.h"
#include "plibs.h"
#include "plist.h"
#include "pfolder.h"
#include "prunratelimit.h"

#define MAX_STATUS_STR_LEN 64
#define DONT_SHOW_TIME_IF_SEC_OVER (2*86400)
#define DONT_SHOW_TIME_IF_SPEED_BELOW (4*1024)

static pthread_mutex_t statusmutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t statuscond=PTHREAD_COND_INITIALIZER;
static int statuschanges=0;
static int statusthreadrunning=0;

static pthread_mutex_t eventmutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t eventcond=PTHREAD_COND_INITIALIZER;
static psync_list eventlist;
static int eventthreadrunning=0;
static pstatus_t status_old={ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

typedef struct {
  psync_list list;
  psync_eventdata_t data;
  psync_eventtype_t event;
  int freedata;
} event_list_t;

static char *cat_lstr(char *src, const char *app, size_t len){
  return (char *)memcpy(src, app, len)+len;
}

static char *cat_str(char *src, const char *app){
  return cat_lstr(src, app, strlen(app));
}

PSYNC_NOINLINE static char *cat_uint32l(char *src, uint32_t num){
  char str[16];
  uint32_t rem;
  int unsigned len;
  len=0;
  do{
    rem=num%10;
    str[sizeof(str)-++len]='0'+rem;
  } while (num/=10);
  memcpy(src, str+sizeof(str)-len, len);
  return src+len;
}

static char *cat_uint32(char *src, uint32_t num){
  if (num>=1000)
    return cat_uint32l(src, num);
  if (num<100){
    if (num<10)
      goto format10;
    else
      goto format100;
  }
  *src++='0'+num/100;
  num%=100;
format100:
  *src++='0'+num/10;
  num%=10;
format10:
  *src++='0'+num;
  return src;
}

#define cat_const(src, app) cat_lstr(src, app, sizeof(app)-1)

static char *fill_formatted_bytes(char *str, uint64_t bytes){
  const static char *sizes[]={"Bytes", "KB", "MB", "GB", "TB"};
  uint32_t rem, sz;
  rem=0;
  sz=0;
  while (bytes>=1024 && sz<ARRAY_SIZE(sizes)-1){
    rem=bytes%1024;
    bytes/=1024;
    sz++;
  }
  str=cat_uint32(str, (uint32_t)bytes);
  if (sz && bytes<100){
    rem=rem*1000/1024;
    assert(rem<=999);
    *str++='.';
    if (bytes<10){
      rem/=10;
      *str++='0'+rem/10;
      *str++='0'+rem%10;
    }
    else
      *str++='0'+rem/100;
  }
  return cat_str(str, sizes[sz]);
}

static char *fill_remaining(char *str, uint32_t files, uint64_t bytes){
  str=cat_const(str, "Remaining: ");
  str=cat_uint32(str, files);
  str=cat_const(str, " files, ");
  return fill_formatted_bytes(str, bytes);
}

static char *fill_formatted_time(char *str, uint64_t totalsec){
  uint32_t d, h, m, s;
  s=totalsec%60;
  totalsec/=60;
  m=totalsec%60;
  totalsec/=60;
  h=totalsec%24;
  d=totalsec/24;
  if (d){
    str=cat_uint32(str, d);
    *str++='d';
    *str++=' ';
    if (m>=30)
      h++;
    str=cat_uint32(str, h);
    *str++='h';
  }
  else if (h){
    str=cat_uint32(str, h);
    *str++='h';
    *str++=' ';
    if (s>=30)
      m++;
    str=cat_uint32(str, m);
    *str++='m';
  }
  else if (m){
    str=cat_uint32(str, m);
    *str++='m';
    *str++=' ';
    str=cat_uint32(str, s);
    *str++='s';
  }
  else {
    if (!s)
      s=1;
    str=cat_uint32(str, s);
    *str++='s';
  }
  return str;
}

static uint64_t sub_no_underf(uint64_t a, uint64_t b){
  if (likely(b<a))
    return a-b;
  else
    return 0;
}

static void status_fill_formatted_str(pstatus_t *status, char *downloadstr, char *uploadstr){
  char *up, *dw;
  uint64_t remsec;
  uint32_t speed;
  dw=downloadstr;
  up=uploadstr;

  if (status->filestodownload){
    speed=status->downloadspeed;
    if (status->status==PSTATUS_PAUSED || status->status==PSTATUS_STOPPED || status->localisfull || speed==0){
      if (status->status==PSTATUS_PAUSED)
        dw=cat_const(dw, "Paused. ");
      else if (status->status==PSTATUS_STOPPED)
        dw=cat_const(dw, "Stopped. ");
      else if (status->localisfull)
        dw=cat_const(dw, "Disk full. ");
      dw=fill_remaining(dw, status->filestodownload, sub_no_underf(status->bytestodownload, status->bytesdownloaded));
    }
    else{
      dw=fill_formatted_bytes(dw, speed);
      dw=cat_const(dw, "/sec, ");
      dw=fill_remaining(dw, status->filestodownload, sub_no_underf(status->bytestodownload, status->bytesdownloaded));
      remsec=(status->bytestodownload-status->bytesdownloaded)/speed;
      if (remsec<DONT_SHOW_TIME_IF_SEC_OVER || speed>=DONT_SHOW_TIME_IF_SPEED_BELOW){
        *dw++=' ';
        dw=fill_formatted_time(dw, remsec);
      }
    }
  }
  else
    dw=cat_const(dw, "Everything Downloaded");

  if (status->filestoupload){
    speed=status->uploadspeed;
    if (status->status==PSTATUS_PAUSED || status->status==PSTATUS_STOPPED || status->remoteisfull || speed==0){
      if (status->status==PSTATUS_PAUSED)
        up=cat_const(up, "Paused. ");
      else if (status->status==PSTATUS_STOPPED)
        up=cat_const(up, "Stopped. ");
      else if (status->remoteisfull)
        up=cat_const(up, "Account full. ");
      up=fill_remaining(up, status->filestoupload, sub_no_underf(status->bytestoupload, status->bytesuploaded));
    }
    else{
      up=fill_formatted_bytes(up, speed);
      up=cat_const(up, "/sec, ");
      up=fill_remaining(up, status->filestoupload, sub_no_underf(status->bytestoupload, status->bytesuploaded));
      remsec=(status->bytestoupload-status->bytesuploaded)/speed;
      if (remsec<DONT_SHOW_TIME_IF_SEC_OVER || speed>=DONT_SHOW_TIME_IF_SPEED_BELOW){
        *up++=' ';
        up=fill_formatted_time(up, remsec);
      }
    }
  }
  else
    up=cat_const(up, "Everything Uploaded");

  assert(dw<downloadstr+MAX_STATUS_STR_LEN);
  assert(up<uploadstr+MAX_STATUS_STR_LEN);
  *dw=0;
  *up=0;
  status->downloadstr=downloadstr;
  status->uploadstr=uploadstr;
}

void psync_callbacks_get_status(pstatus_t *status){
  static char downloadstr[MAX_STATUS_STR_LEN], uploadstr[MAX_STATUS_STR_LEN];
  memcpy(status, &psync_status, sizeof(pstatus_t));
  status_fill_formatted_str(status, downloadstr, uploadstr);
}

static void status_change_thread(void *ptr){
  char downloadstr[MAX_STATUS_STR_LEN], uploadstr[MAX_STATUS_STR_LEN];
  pstatus_change_callback_t callback=(pstatus_change_callback_t)ptr;
  while (1){
    // Maximum 2 updates/sec
    psync_milisleep(500);
    pthread_mutex_lock(&statusmutex);
    while (statuschanges<=0){
      statuschanges=-1;
      pthread_cond_wait(&statuscond, &statusmutex);
    }
    statuschanges=0;
    if (((status_old.filestodownload > 0 ) && (psync_status.filestodownload == 0)) ||
        ((psync_status.filestodownload > 0 ) && (status_old.filestodownload == 0)) ||
        ((status_old.filestoupload > 0 ) && (psync_status.filestoupload == 0)) ||
        ((psync_status.filestoupload > 0 ) && (status_old.filestoupload == 0)) ||
        ((psync_status.localisfull != status_old.localisfull)) ||
        ((psync_status.remoteisfull != status_old.remoteisfull)) ||
        ((psync_status.status != status_old.status) && (
          (psync_status.status == PSTATUS_STOPPED) ||
          (psync_status.status == PSTATUS_PAUSED) ||
          (psync_status.status == PSTATUS_OFFLINE) ||
          (status_old.status == PSTATUS_STOPPED) ||
          (status_old.status == PSTATUS_PAUSED) ||
          (status_old.status == PSTATUS_OFFLINE) ) )
    )
      psync_run_ratelimited("rebuild icons", psync_rebuild_icons, 1, 1);
    status_old = psync_status;
    pthread_mutex_unlock(&statusmutex);
    if (!psync_do_run)
      break;
    status_fill_formatted_str(&psync_status, downloadstr, uploadstr);
    debug(D_NOTICE, "sending status update, dwlstr: %s, uplstr: %s", psync_status.downloadstr, psync_status.uploadstr);
    callback(&psync_status);
  }
}

void psync_set_status_callback(pstatus_change_callback_t callback){
  pthread_mutex_lock(&statusmutex);
  statusthreadrunning=1;
  pthread_mutex_unlock(&statusmutex);
  psync_run_thread1("status change", status_change_thread, callback);
}

void psync_send_status_update(){
  if (statusthreadrunning){
    pthread_mutex_lock(&statusmutex);
    if (++statuschanges==0){
      statuschanges++;

      pthread_cond_signal(&statuscond);
    }
    pthread_mutex_unlock(&statusmutex);
  }
}

static void event_thread(void *ptr){
  pevent_callback_t callback=(pevent_callback_t)ptr;
  event_list_t *event;

  while (1){
    pthread_mutex_lock(&eventmutex);

    while (psync_list_isempty(&eventlist))
      pthread_cond_wait(&eventcond, &eventmutex);

    event=psync_list_remove_head_element(&eventlist, event_list_t, list);

    pthread_mutex_unlock(&eventmutex);

    if (!psync_do_run)
      break;

    callback(event->event, event->data);

    if (event->freedata)
      psync_free(event->data.ptr);

    psync_free(event);
  }
}

void psync_set_event_callback(pevent_callback_t callback){
  pthread_mutex_lock(&statusmutex);
  eventthreadrunning=1;
  pthread_mutex_unlock(&statusmutex);
  psync_list_init(&eventlist);
  psync_run_thread1("event", event_thread, callback);
}

void psync_send_event_by_id(psync_eventtype_t eventid, psync_syncid_t syncid, const char *localpath, psync_fileorfolderid_t remoteid){
  if (eventthreadrunning){
    char *remotepath;
    if (eventid&PEVENT_TYPE_FOLDER)
      remotepath=psync_get_path_by_folderid(remoteid, NULL);
    else
      remotepath=psync_get_path_by_fileid(remoteid, NULL);
    if (unlikely_log(!remotepath))
      return;
    psync_send_event_by_path(eventid, syncid, localpath, remoteid, remotepath);
    psync_free(remotepath);
  }
}

void psync_send_event_by_path(psync_eventtype_t eventid, psync_syncid_t syncid, const char *localpath, psync_fileorfolderid_t remoteid, const char *remotepath){
  if (eventthreadrunning){
    event_list_t *event;
    size_t llen, rlen, slen;
    char *lcopy, *rcopy, *strct, *name;
    llen=strlen(localpath)+1;
    rlen=strlen(remotepath)+1;
    if (eventid&PEVENT_TYPE_FOLDER)
      slen=sizeof(psync_file_event_t);
    else
      slen=sizeof(psync_folder_event_t);
    event=(event_list_t *)psync_malloc(sizeof(event_list_t)+slen+llen+rlen);
    strct=(char *)(event+1);
    lcopy=strct+slen;
    rcopy=lcopy+llen;
    memcpy(lcopy, localpath, llen);
    memcpy(rcopy, remotepath, rlen);
    name=strrchr(rcopy, '/')+1;
    if (eventid&PEVENT_TYPE_FOLDER){
      psync_folder_event_t *f=(psync_folder_event_t *)strct;
      f->folderid=remoteid;
      f->name=name;
      f->localpath=lcopy;
      f->remotepath=rcopy;
      f->syncid=syncid;
      event->data.folder=f;
    }
    else{
      psync_file_event_t *f=(psync_file_event_t *)strct;
      f->fileid=remoteid;
      f->name=name;
      f->localpath=lcopy;
      f->remotepath=rcopy;
      f->syncid=syncid;
      event->data.file=f;
    }
    event->event=eventid;
    event->freedata=0;
    pthread_mutex_lock(&eventmutex);
    psync_list_add_tail(&eventlist, &event->list);
    pthread_cond_signal(&eventcond);
    pthread_mutex_unlock(&eventmutex);
  }
}

void psync_send_eventid(psync_eventtype_t eventid){
  if (eventthreadrunning){
    event_list_t *event;

    event=psync_new(event_list_t);
    event->data.ptr=NULL;
    event->event=eventid;
    event->freedata=0;

    pthread_mutex_lock(&eventmutex);
    psync_list_add_tail(&eventlist, &event->list);
    pthread_cond_signal(&eventcond);
    pthread_mutex_unlock(&eventmutex);
  }
}

void psync_send_eventdata(psync_eventtype_t eventid, void *eventdata){

  debug(D_ERROR, "Send event data: eventid: [%lu, eventthreadrunning: [%d]", eventid, eventthreadrunning);

  if (eventthreadrunning){
    event_list_t *event;

    event=psync_new(event_list_t);
    event->data.ptr=eventdata;
    event->event=eventid;
    event->freedata=1;

    pthread_mutex_lock(&eventmutex);
    psync_list_add_tail(&eventlist, &event->list);
    pthread_cond_signal(&eventcond);
    pthread_mutex_unlock(&eventmutex);
  }
  else
    psync_free(eventdata);
}
//Data event methods.
/**********************************************************************************************/
data_event_callback data_event_fptr = NULL;
de_elem_list data_event_elem_list = {0,0};

static pthread_mutex_t data_event_mutex = PTHREAD_MUTEX_INITIALIZER;
/**********************************************************************************************/
void* free_data_event(event_data_struct* elem) {
  if (elem->str1) {
    psync_free(elem->str1);
  }

  if (elem->str2) {
    psync_free(elem->str2);
  }

  psync_free(elem);
}

/**********************************************************************************************/
void* add_elem(event_data_struct* elem, de_elem_list* list) {
  event_data_struct* tmp_elem;

  pthread_mutex_lock(&data_event_mutex);

  if (list->first == 0) {
    list->first = elem;
  }

  if (list->last == 0) {
    list->last = elem;
  }
  else {
    tmp_elem = (event_data_struct*)list->last;
    tmp_elem->elem_next = elem;
  }

  list->last = elem;

  pthread_mutex_unlock(&data_event_mutex);
}
/***********************************************************************/
event_data_struct* pop_elem(de_elem_list* list) {
  event_data_struct* curr_elem;

  if (list->first == 0) {
    return 0;
  }

  pthread_mutex_lock(&data_event_mutex);

  curr_elem = list->first;

  if (list->first == list->last) {
    list->first = 0;
    list->last = 0;
  }
  else {
    list->first = curr_elem->elem_next;
  }

  pthread_mutex_unlock(&data_event_mutex);

  return curr_elem;
}
/**********************************************************************************************/
void data_event_thread(void* ptr) {
  de_elem_list* event_list = (de_elem_list*)ptr;
  event_data_struct* data;
  char* path;
  int i;

  while (1) {
    for (i = 0; i < 10000; i++) {
      data = pop_elem(event_list);

      if (data) {
        debug(D_NOTICE, "Sending data event Event id: [%d] Str1: [%s], Str1: [%s], Uint1:[%lu] Uint2:[%lu]", data->eventid, data->str1, data->str2, data->uint1, data->uint2);
        data_event_fptr(data->eventid, data->str1, data->str2, data->uint1, data->uint2);

        free_data_event(data);
      }
      else {
        break;
      }
    }

    psync_milisleep(1000);
  }
}
/**********************************************************************************************/
void psync_init_data_event(void* ptr) {
  data_event_fptr = (data_event_callback*)ptr;

  psync_run_thread1("Data Event", data_event_thread, &data_event_elem_list);
}
/**********************************************************************************************/
void psync_send_data_event(int event_id, char* str1, char* str2, uint64_t uint1, uint64_t uint2) {
  event_data_struct* data;

  data = psync_new(event_data_struct);

  data->eventid = event_id;
  if(str1)data->str1 = strdup(str1);
  else data->str1 = strdup("null");
  if (str1)data->str2 = strdup(str2);
  else data->str2 = strdup("null");
  data->uint1 = uint1;
  data->uint2 = uint2;

  debug(D_NOTICE, "Send data event: Event Id: [%d] Str1: [%s] Str2: [%s] Uint1: [%llu] Uint2: [%llu]", data->eventid, data->str1, data->str2, data->uint1, data->uint2);

  if (data_event_fptr) {
    add_elem(data, &data_event_elem_list);
  }
  else {
    //debug(D_ERROR, "Data event callback function not set.");
  }
}
/**********************************************************************************************/
void psync_data_event_test(int eventid, char* str1, char* str2, uint64_t uint1, uint64_t uint2) {
  debug(D_NOTICE, "Test Data event callback. eventid [%d]. String1: [%s], String2: [%s], uInt1: [%ul] uInt2: [%ul]", eventid, str1, str2, uint1, uint2);

  return;
}
/**********************************************************************************************/
void wait_auth_token_async(void* data) {
  int res;
  char* req_token;
  wait_login_async_cb callback;

  wait_token_cb_struct* local_data = (wait_token_cb_struct*)(data);
  callback = (wait_login_async_cb*)local_data->calb_ptr;

  res = wait_auth_token(local_data->req_id);

  debug(D_NOTICE, "wait_auth_token_async. Result:[%d]", res);
  psync_free(local_data);

  callback(res);
}
/**********************************************************************************************/
//Data event methods End.