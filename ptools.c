/* Copyright (c) 2013-2015 pCloud Ltd.
 * All rights reserved.
 *
 * Library containing tool functions, not used in the main 
 * functionality. Keeping statistics, getting data for them etc.
 */
#define _CRT_SECURE_NO_WARNINGS
#include <ptools.h>

/*************************************************************/
char* getMACaddr() {
  PIP_ADAPTER_INFO AdapterInfo;
  DWORD dwBufLen = sizeof(IP_ADAPTER_INFO);
  char* mac_addr[100];
  char* chunk[2];

  AdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));

  if (AdapterInfo == NULL) {
    printf("Error allocating memory needed to call GetAdaptersinfo\n");
    free(mac_addr);

    return NULL;
  }

  if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_BUFFER_OVERFLOW) {
    free(AdapterInfo);
    AdapterInfo = (IP_ADAPTER_INFO*)malloc(dwBufLen);

    if (AdapterInfo == NULL) {
      printf("Error allocating memory needed to call GetAdaptersinfo\n");
      free(mac_addr);

      return NULL;
    }
  }

  if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == NO_ERROR) {
    PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;

    do {
      mac_addr[0] = 0;

      for (int i = 0; i < pAdapterInfo->AddressLength; i = i + 1) {
        sprintf(chunk, "%02X", pAdapterInfo->Address[i]);

        strcat(mac_addr, chunk);

        if (i < pAdapterInfo->AddressLength - 1) {
          strcat(mac_addr, ":");
        }
      }

      break;
      pAdapterInfo = pAdapterInfo->Next;
    } while (pAdapterInfo);
  }

  free(AdapterInfo);

  return mac_addr;
}
/*************************************************************/
void add_event_param(eventParams* eParams, char* paramName, char* paramVal) {
/*
  uint16_t paramtype;
  uint16_t paramnamelen;
  uint32_t opts;
  const char* paramname;
  union {
    uint64_t num;
    const char* str;
  };
*/
  /*
  if (!eParams->Params) {
    eParams->Params = (binparam*){0};
  }
  */
  eParams->Params[eParams->paramCnt].paramtype = PARAM_STR;
  
  eParams->Params[eParams->paramCnt].paramnamelen = strlen(paramName);
  strcpy(eParams->Params[eParams->paramCnt].paramname, paramName);

  eParams->Params[eParams->paramCnt].opts = strlen(paramVal);
  strcpy(eParams->Params[eParams->paramCnt].str, paramVal);

  eParams->paramCnt++;
}
/*************************************************************/
int get_os_id() {
#if defined(P_OS_LINUX)   
   return 1;
#endif
#if defined(P_OS_WINDOWS) 
   return 5; 
#endif
#if defined(P_OS_MACOSX)  
   return 6; 
#endif
#if defined(P_OS_LINUX)   
   return 7; 
#endif

  return 0;
}
/*************************************************************/
int create_backend_event(eventParams* params,
                         const char*  binapi,
                         unsigned int locationid,
                         char**       err) {
  binresult*    res;
  psync_socket* sock;
  uint64_t      result;
  int i;

  if (binapi) {
    psync_set_apiserver(binapi, locationid);
  }
  else {
    if (err) {
      *err = psync_strdup("Could not connect to the server.");
    }

    psync_set_apiserver(PSYNC_API_HOST, PSYNC_LOCATIONID_DEFAULT);

    return -1;
  }

  sock = psync_api_connect(binapi, psync_setting_get_bool(0));

  if (unlikely_log(!sock)) {
    if (err) {
      *err = psync_strdup("Could not connect to the server.");
    }

    psync_set_apiserver(PSYNC_API_HOST, PSYNC_LOCATIONID_DEFAULT);

    return -1;
  }

  //binparam* paramsLocal = (binparam*)malloc(params->paramCnt * sizeof(binparam));

  //memcpy(paramsLocal, &params->Params, params->paramCnt * sizeof(binparam));

  debug(D_WARNING, "BOBO: Number of input params: [%d]", params->paramCnt);
  /*
  for (i = 0; i < params->paramCnt; i++) {
    debug(D_WARNING, "BOBO: %d) Input Paramer Name: [%s] - Value:[%s]", i, params->Params[i].paramname, params->Params[i].str);
  }

  for (i = 0; i < params->paramCnt; i++) {
    debug(D_WARNING, "BOBO: %d) Local Parameter Name: [%s] - Value:[%s]", i, paramsLocal[i].paramname, paramsLocal[i].str);
  }
  */
  debug(D_WARNING, "BOBO: Sending command.");
  //#define send_command(sock, cmd, params) do_send_command(sock, cmd, strlen(cmd), params, sizeof(params)/sizeof(binparam), -1, 1)
  //binresult *do_send_command(psync_socket *sock, const char *command, size_t cmdlen, const binparam *params, size_t paramcnt, int64_t datalen, int readres)
  //res = do_send_command(sock, EVENT_WS, strlen(EVENT_WS), paramsLocal, params->paramCnt, -1, 1);

  binparam paramsLocal[] = { P_STR("category", "INSTALLATION_PROCESS"),
    P_STR("action", "FIRST_LOGIN"),
    P_STR("label", "TEST"),
    P_STR("mac_address", "CC:48:3A:3C:31:38"),
    P_NUM("os", 5),
    P_NUM("etime", 1607080865) };

  res = send_command(sock, EVENT_WS, paramsLocal);
  //free(paramsLocal);
  psync_set_apiserver(PSYNC_API_HOST, PSYNC_LOCATIONID_DEFAULT);

  debug(D_WARNING, "BOBO: Command sent.");
  
  if (unlikely_log(!res)) {
    debug(D_WARNING, "BOBO: Can't get the result.");
    psync_socket_close(sock);

    if (err) {
      *err = psync_strdup("Could not connect to the server.");
    }

    psync_set_apiserver(PSYNC_API_HOST, PSYNC_LOCATIONID_DEFAULT);

    return -1;
  }

  result = psync_find_result(res, "result", PARAM_NUM)->num;
  debug(D_WARNING, "BOBO: Found result [%d].", result);

  if (result) {
    debug(D_WARNING, "command register returned code %u", (unsigned)result);

    if (err) {
      *err = psync_strdup(psync_find_result(res, "error", PARAM_STR)->str);
    }

    psync_set_apiserver(PSYNC_API_HOST, PSYNC_LOCATIONID_DEFAULT);
  }

  psync_socket_close(sock);
  psync_free(res);

  //Back to the real server. To be removed!
  psync_set_apiserver(PSYNC_API_HOST, PSYNC_LOCATIONID_DEFAULT);

  return result;
}
/*************************************************************/