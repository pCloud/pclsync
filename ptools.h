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

//Payload name constants
#define DEVICE_FOLDER_META "devicefoldermeta"
#define BACKUP_FOLDER_META "backupfoldermeta"
#define NO_PAYLOAD         ""

//Parameter name constants
#define FOLDER_ID          "folderid"
#define PARENT_FOLDER_NAME "parentname"

typedef struct _eventParams {
  int paramCnt;
  binparam Params[100];
} eventParams;

typedef struct _folderPath {
  int cnt;
  char* folders[50][100];
} folderPath;
/**********************************************************************************************************/
int create_backend_event(
  const char* binapi,
  const char* category,
  const char* action,
  const char* label,
  const char* auth,
  int          os,
  int          etime,
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
void getMACaddr(char* mac_addr);
/**********************************************************************************************************/
void get_machine_name(char* pcName);
/**********************************************************************************************************/
void parse_os_path(char* path, folderPath* folders);
/**********************************************************************************************************/