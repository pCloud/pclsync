/* Copyright (c) 2013-2015 pCloud Ltd.
 * All rights reserved.
 *
 * Library containing tool functions, not used in the main
 * functionality. Keeping statistics, getting data for them etc.
 */
#pragma once

#include "papi.h"

#define EVENT_WS "loganalyticsevent"

#define EPARAM_CATEG  "category"
#define EPARAM_ACTION "action"
#define EPARAM_LABEL  "label"
#define EPARAM_OS     "os"
#define EPARAM_TIME   "etime"
#define EPARAM_AUTH   "auth"
#define EPARAM_MAC    "mac_address"
#define EPARAM_KEY    "keys"

#define INST_EVENT_CATEG  "INSTALLATION_PROCESS"
#define INST_EVENT_FLOGIN "FIRST_LOGIN"

//Syncs count constants
#define PSYNC_SYNCS_COUNT  "syncs_count"

#define PSYNC_EVENT_CATEG  "SYNCS_EVENTS"
#define PSYNC_EVENT_ACTION "SYNCS_LOG_COUNT"
#define PSYNC_EVENT_LABEL  "SYNCS_COUNT"


//Payload name constants
#define FOLDER_META "metadata"
#define NO_PAYLOAD         ""

//Parameter name constants
#define FOLDER_ID          "folderid"
#define PARENT_FOLDER_NAME "parentname"

//Parser delimeter symbols
#define DELIM_SEMICOLON ';'

//Web login parameters
#define EPARAM_LOGIN      "typelogin"
#define EPARAM_EXPIRES    "expires"
#define EPARAM_REQ_ID     "request_id"
#define EPARAM_TIMEOUT    "timeout"
#define EPARAM_TOKEN      "token"
#define EPARAM_LOC_ID     "locationid"
#define EPARAM_USER_ID    "userid"
#define EPARAM_REMEMBERME "rememberme"

#if defined(P_OS_WINDOWS)
#define DELIM_DIR   '\\'
#endif

#if defined(P_OS_LINUX)
#define DELIM_DIR  '/'
#endif

#if defined(P_OS_MACOSX)
#define DELIM_DIR  '/'
#endif
typedef struct _eventParams {
  int paramCnt;
  binparam Params[100];
} eventParams;

typedef struct _folderPath {
  int cnt;
  char* folders[50];
} folderPath;

//Stuck sync tasks list.
typedef struct stuck_item_type {
  uint64_t id;
  int      msg_id;
  int      item_type;
  int      retry_cnt;
  uint64_t next_elem;
  const char* path;
  const char* name;
} stuck_item;

typedef struct stuck_item_list_type {
  int stuck_cnt;
  int total_cnt;
  stuck_item* list;
} stuck_list_type;

typedef struct stuck_return_type {
  int   msg_id;
  int   type;
  char* path;
  char* name;
} stuck_return_item;

#define STUCK_ITEM_RET_SIZE 100

typedef struct stuck_return_list_type {
  int elem_count;
  stuck_return_item items[STUCK_ITEM_RET_SIZE];
} stuck_return_list;
/**********************************************************************************************************/
int create_backend_event(
  const char* binapi,
  const char* category,
  const char* action,
  const char* label,
  const char* auth,
  int          os,
  time_t          etime,
  eventParams* params,
  char** err);
/**********************************************************************************************************/
int backend_call(const char* binapi,
  const char*  wsPath,
  const char* payloadName,
  eventParams* requiredParams,
  eventParams* optionalParams,
  binresult**  resData,
  char** err);
/**********************************************************************************************************/
char* getMACaddr();
/**********************************************************************************************************/
char* get_machine_name();
/**********************************************************************************************************/
void parse_os_path(char* path, folderPath* folders, char* delim, int mode);
/**********************************************************************************************************/
void send_psyncs_event(const char* binapi,
                       const char* auth);
/**********************************************************************************************************/
int set_be_file_dates(uint64_t fileid, time_t ctime, time_t mtime);
/**********************************************************************************************************/
 uint32_t get_sync_id_from_fid(uint64_t fid);
/**********************************************************************************************************/
 char* get_sync_folder_by_syncid(uint64_t syncId);
 /**********************************************************************************************************/
 char* get_folder_name_from_path(char* path);
 /**********************************************************************************************************/
#define STUCK_ITEM_RETRY_COUNT 0
#define STUCK_ITEM_TOTAL_COUNT 100

#define STUCK_ITEM_TYPE_FOLDER 1
#define STUCK_ITEM_TYPE_FILE   2

#define STUCK_MSG_UKNOWN        0 //Generic error, when we have nothing more specific.
#define STUCK_MSG_NO_PERMISSION 1 //The app has no permition to read or write the file/folder.

#define STUCK_ITEM_UNKNOWN_FILE   "UNKNOWN_FILE_NAME"
#define STUCK_ITEM_UNKNOWN_FOLDER "UNKNOWN_FOLDER_NAME"
#define STUCK_ITEM_UNKNOWN_PATH   "UNKNOWN_PATH"

#define CANT_FIND_LOG_FILE 10001
#define CANT_CREATE_ZIP_FILE 10002


 void log_list_elem(stuck_item* elem);

 void log_list();

 stuck_item* get_last_element();

 void add_stuck_elem(stuck_item* elem);

 void delete_element(uint64_t id);

 stuck_item* create_stuck_elem(uint64_t id, int msg_id, int item_type, uint64_t next_elem, char* path, char* name);

 stuck_item* search_list(uint64_t id);

 void init_stuck_list();

 stuck_return_list* get_stuck_list();

 char* nvl_str(char* str, const char* def);

 char* get_file_name_from_path(char* path);

 uint64_t get_hash_from_string(char* str);

 char* dns_lookup(const char* addr_host, int port);

 void clean_stuck_list();

 void psync_log_tasks();

 uint64_t Hash64(const void* key, int len, unsigned int seed);
 /**********************************************************************************************************/
 int do_get_crypto_price(char** currency);


 /**************************************** Web login functions *********************************************/
 int get_login_req_id(char** reqId);

 int wait_auth_token(char* request_id);
 /**********************************************************************************************************/
 //Bobo
 static wchar_t* utf8_to_wchar(const char* str);
 
 int uploadLogsToDrive();

 int deleteLogs();
//Bobo
 /**********************************************************************************************************/
