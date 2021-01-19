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

typedef struct _eventParams {
  int paramCnt;
  binparam Params[100];
} eventParams;

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
  const char* wsPath,
  eventParams* requiredParams,
  eventParams* optionalParams,
  binparam* resData,
  char** err);
/**********************************************************************************************************/
void getMACaddr(char* mac_addr);
/**********************************************************************************************************/
void get_machine_name(char* pcName);
/**********************************************************************************************************/