/* Copyright (c) 2013-2015 pCloud Ltd.
 * All rights reserved.
 *
 * Library containing tool functions, not used in the main 
 * functionality. Keeping statistics, getting data for them etc.
 */
#include "ptools.h"
#include "psettings.h"
#include "plibs.h"
#include "string.h"
#include "stdlib.h"
#include "pnetlibs.h"
#include <stdio.h>
#include "pcallbacks.h"
 
#include "pupload.h"
#include "miniz.h"

#include "pstatus.h"
#include "psettings.h"

//Bobo
#include "plocalscan.h"
#include "psynclib.h"
//Bobo


#if defined(P_OS_WINDOWS)
#define _CRT_SECURE_NO_WARNINGS
#pragma comment(lib, "iphlpapi.lib")

#include <Iphlpapi.h>
#endif

#if defined(P_OS_LINUX)
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#endif

#if defined(P_OS_MACOSX)
#include <unistd.h>
#include <stdio.h>
#endif

stuck_list_type* stuck_sync_tasks = NULL;
static pthread_mutex_t stuck_elem_list_mutex = PTHREAD_MUTEX_INITIALIZER;

/*************************************************************/
char* get_zipLogsFilePath(char* fName) {
  char* zipFilePath;

#if defined(P_OS_WINDOWS)
  zipFilePath = psync_strcat(appDriveLetter, "tmp", PSYNC_DIRECTORY_SEPARATOR, fName, NULL);
#else
  zipFilePath = psync_strcat("/tmp/", fName, NULL);
#endif

  return zipFilePath;
}
/*************************************************************/
char* get_zipLogsFile() {
  char* zipFile;
  char tmp[36];

  struct timespec ts;
  struct tm dt;

  psync_nanotime(&ts);

  gmtime_r(&ts.tv_sec, &dt);

  sprintf(tmp, "%llu_%d_%d_%d_%d_%d", psync_my_userid, dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday, dt.tm_hour, dt.tm_min);

  zipFile = psync_strcat(tmp, ".zip", NULL);

  return zipFile;
}
/*************************************************************/
int zipLogs(char* zipLogsFname) {
  int res = 0;
  mz_bool status;
  mz_zip_archive zip_archive;

  uint64_t fsize = 0;
  psync_stat_t st;
  int ret = 0;

#if defined(P_OS_WINDOWS)
  char* srcFname1 = psync_strcat(appDriveLetter, "tmp", PSYNC_DIRECTORY_SEPARATOR, "psync_err.log", NULL);
  char* srcFname2 = psync_strcat(appDriveLetter, "tmp", PSYNC_DIRECTORY_SEPARATOR, "cbfs_log.log", NULL);
  char* srcFname3 = psync_strcat(psync_get_pcloud_path(), PSYNC_DIRECTORY_SEPARATOR, "wpflog.log", NULL);
#else
  char* srcFname1 = DEBUG_FILE;
#endif

  FILE* srcFile;

  debug(D_NOTICE, "Check log file size: [%s]", srcFname1);
  ret = psync_stat(srcFname1, &st);
  
  fsize = psync_stat_size(&st);

  if (fsize > MAX_LOG_SIZE) {
    debug(D_NOTICE, "Zipped logs too big.  File size: [%llu] > [%llu]", fsize, MAX_LOG_SIZE);

    return LOGS_ZIP_TOO_BIG;
  }

  debug(D_NOTICE, "Create Zip file");

  mz_zip_zero_struct(&zip_archive);

  status = mz_zip_writer_init_file(&zip_archive, zipLogsFname, 0);

  if (status == MZ_FALSE) {
    return FAIL_TO_ZIP_LOGS;
  }

  srcFile = fopen(srcFname1, "r");

  if (srcFile) {
    status = mz_zip_writer_add_cfile(&zip_archive, "psync_err.log", srcFile, MZ_UINT32_MAX, 0, NULL, 0, MZ_DEFAULT_COMPRESSION, NULL, 0, NULL, 0);

    res = fclose(srcFile);
  }
  else {
    debug(D_NOTICE, "Failed to open: [%s]", srcFname1);

    return FAIL_TO_ZIP_LOGS;
  }

#if defined(P_OS_WINDOWS)
  srcFile = fopen(srcFname2, "r");

  if (srcFile) {
    status = mz_zip_writer_add_cfile(&zip_archive, "cbfs_log.log", srcFile, MZ_UINT32_MAX, 0, NULL, 0, MZ_DEFAULT_COMPRESSION, NULL, 0, NULL, 0);

    res = fclose(srcFile);
  }
  else {
    debug(D_NOTICE, "Failed to open: [%s]", srcFname2);
  }

  srcFile = fopen(srcFname3, "r");

  if (srcFile) {
    status = mz_zip_writer_add_cfile(&zip_archive, "wpflog.log", srcFile, MZ_UINT32_MAX, 0, NULL, 0, MZ_DEFAULT_COMPRESSION, NULL, 0, NULL, 0);

    res = fclose(srcFile);
  }
  else {
    debug(D_NOTICE, "Failed to open: [%s]", srcFname3);
  }
#endif

  status = mz_zip_writer_finalize_archive(&zip_archive);

  status = mz_zip_writer_end(&zip_archive);

  debug(D_NOTICE, "Done. Res: [%d] Status: [%d]", res, status);

  return status;
}
/*************************************************************/
int uploadLogsToDrive() {
  int res = 0;
  char *zipLogsFname, *zipLogsFpath;

  zipLogsFname = get_zipLogsFile();
  zipLogsFpath = get_zipLogsFilePath(zipLogsFname);

  res = zipLogs(zipLogsFpath);

  if (res == MZ_TRUE) {
    res = upload_logs(zipLogsFname, zipLogsFpath);

    debug(D_NOTICE, "Done uploading logs. Delete zip file.");
    psync_file_delete(zipLogsFpath);
  }
  else {
    debug(D_NOTICE, "Failed to zip logs. Res: [%d]", res);
  }

  debug(D_NOTICE, "Done uploading logs. Send the data event. Res: [%d]", res);
  psync_send_data_event(PEVENT_UPLOAD_LOGS_DONE, "", "", res, 0);

  return res;
}
/*************************************************************/
char* getMACaddr() {
  char  buffer[128];

  memset(buffer, 0, sizeof(buffer));

#if defined(P_OS_WINDOWS)
  PIP_ADAPTER_INFO AdapterInfo;
  DWORD dwBufLen = sizeof(IP_ADAPTER_INFO);

  AdapterInfo = (IP_ADAPTER_INFO*)malloc(sizeof(IP_ADAPTER_INFO));

  if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_BUFFER_OVERFLOW) {
    free(AdapterInfo);
    AdapterInfo = (IP_ADAPTER_INFO*)malloc(dwBufLen);

    if (AdapterInfo == NULL) {
      debug(D_CRITICAL, "Error allocating memory needed to call GetAdaptersinfo!");
    }
    else {
      if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == NO_ERROR) {
        sprintf(buffer, "%.2x%.2x%.2x%.2x%.2x%.2x", AdapterInfo->Address[0], AdapterInfo->Address[1], AdapterInfo->Address[2], AdapterInfo->Address[3], AdapterInfo->Address[4], AdapterInfo->Address[5]);
      }
      free(AdapterInfo);
    }
  }
#endif

#if defined(P_OS_LINUX)
  int fd;
  struct ifreq ifr;
  char* iface = "eth0";
  unsigned char* mac;

  fd = socket(AF_INET, SOCK_DGRAM, 0);

  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);

  ioctl(fd, SIOCGIFHWADDR, &ifr);

  close(fd);

  mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;

  sprintf(buffer, "%.2x%.2x%.2x%.2x%.2x%.2x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  buffer[12] = 0;
#endif

#if defined(P_OS_MACOSX)
  int   byteRead = 0;
  FILE* stream = popen("ifconfig en0 | grep ether | cut -c 8-24", "r");

  while (!feof(stream) && !ferror(stream)) {
    byteRead = fread(buffer, 1, 128, stream);
  }
  
  buffer[byteRead] = 0;
#endif

  if (buffer[0] == 0) {
    return psync_strdup("GENERIC_MAC");
    
  }
  else {
    return psync_strdup(buffer);
  }
}
/*************************************************************/
int create_backend_event(const char*  binapi,
                         const char*  category,
                         const char*  action,
                         const char*  label,
                         const char*  auth,
                         int          os,
                         time_t       etime,
                         eventParams* params,
                         char**       err) {
  binresult*    res;
  psync_socket* sock;
  uint64_t      result;
  binparam*     paramsLocal;
  int i;
  int pCnt = params->paramCnt; //Number of optional parameters
  int mpCnt = 6; //Number of mandatory params
  int tpCnt; //Total number of parameters
  char* keyParams;
  char charBuff[30][258];

  sock = psync_api_connect(binapi, psync_setting_get_bool(0));

  if (unlikely_log(!sock)) {
    if (err) {
      *err = psync_strdup("Could not connect to the server.");
    }

    return -1;
  }

  if (pCnt > 0) { //We have optional parameters
    tpCnt = pCnt + mpCnt + 1; //+1 for the "key" parameter.
  }
  else {
    tpCnt = mpCnt; //Manadatory parameters only.
  }

  paramsLocal = (binparam*)malloc((tpCnt) * sizeof(binparam)); //Allocate size for all parameters.

  //Set the mandatory pramaters.
  paramsLocal[0] = (binparam)P_STR(EPARAM_CATEG, category);
  paramsLocal[1] = (binparam)P_STR(EPARAM_ACTION, action);
  paramsLocal[2] = (binparam)P_STR(EPARAM_LABEL, label);
  paramsLocal[3] = (binparam)P_STR(EPARAM_AUTH, auth);
  paramsLocal[4] = (binparam)P_NUM(EPARAM_OS, os);
  paramsLocal[5] = (binparam)P_NUM(EPARAM_TIME, etime);

  if (pCnt > 0) {
    keyParams = (char*)malloc(258 * pCnt);
    keyParams[0] = 0;

    for (i = 0; i < pCnt; i++) {
      charBuff[i][0] = 0;

      if (i > 0) {
        strcat(keyParams, ",");
        strcat(keyParams, params->Params[i].paramname);
      }
      else {
        strcat(keyParams, params->Params[i].paramname);
      }

      sprintf(charBuff[i], "key%s", params->Params[i].paramname);

      if (params->Params[i].paramtype == 0) {
        paramsLocal[mpCnt + i] = (binparam)P_STR(charBuff[i], params->Params[i].str);

        continue;
      }

      if (params->Params[i].paramtype == 1) {
        paramsLocal[mpCnt + i] = (binparam)P_NUM(charBuff[i], params->Params[i].num);

        continue;
      }

      if (params->Params[i].paramtype == 2) {
        paramsLocal[mpCnt + i] = (binparam)P_BOOL(charBuff, params->Params[i].num);
        continue;
      }
    }

    paramsLocal[mpCnt + pCnt] = (binparam)P_STR(EPARAM_KEY, keyParams);
  }

  //Comment out because the parameters contain the authentication token
  /*
  for (i = 0; i < tpCnt; i++) {
    if (paramsLocal[i].paramtype == 0) {
      debug(D_NOTICE, "%d: String Param: [%s] - [%s]", i, paramsLocal[i].paramname, paramsLocal[i].str);
      continue;
    }

    if (paramsLocal[i].paramtype == 1) {
      debug(D_NOTICE, "%d: Number Param: [%s] - [%d]", i, paramsLocal[i].paramname, paramsLocal[i].num);
      continue;
    }
  }
  */

  res = do_send_command(sock, EVENT_WS, strlen(EVENT_WS), paramsLocal, tpCnt, -1, 1);

  free(keyParams);
  free(paramsLocal);
  
  if (unlikely_log(!res)) {
    psync_socket_close(sock);

    if (err) {
      *err = psync_strdup("Could not connect to the server.");
    }

    return -1;
  }

  result = psync_find_result(res, "result", PARAM_NUM)->num;

  psync_socket_close(sock);

  if (result) {
    if (err) {
      *err = psync_strdup(psync_find_result(res, "error", PARAM_STR)->str);
    }

    debug(D_CRITICAL, "Event command failed. Error:[%s]", *err);
  }

  return result;
}
/*************************************************************/
int backend_call(const char*  binapi,
                 const char*  wsPath,
                 const char*  payloadName,
                 eventParams* requiredParams,
                 eventParams* optionalParams,
                 binresult**  resData,
                 char**       err) {
  int reqParCnt = requiredParams->paramCnt;
  int optParCnt = optionalParams->paramCnt;
  int totalParCnt = reqParCnt + optParCnt;
  int i;

  binparam* localParams;
  binresult*    res;
  binresult*    payload;
  psync_socket* sock;
  uint64_t      result;

  if(totalParCnt > 0) {
    localParams = (binparam*)malloc((totalParCnt) * sizeof(binparam)); //Allocate size for all required parameters.
  }

  //Add required parameters to the structure
  for (i = 0; i < reqParCnt; i++) {
    if (requiredParams->Params[i].paramtype == 0) {
      localParams[i] = (binparam)P_STR(requiredParams->Params[i].paramname, requiredParams->Params[i].str);

      continue;
    }

    if (requiredParams->Params[i].paramtype == 1) {
      localParams[i] = (binparam)P_NUM(requiredParams->Params[i].paramname, requiredParams->Params[i].num);

      continue;
    }

    if (requiredParams->Params[i].paramtype == 2) {
      localParams[i] = (binparam)P_BOOL(requiredParams->Params[i].paramname, requiredParams->Params[i].num);

      continue;
    }
  }

  //Add optional parameters to the structure
  for (i = reqParCnt; i < totalParCnt; i++) {
    int j = 0;

    if (optionalParams->Params[i].paramtype == 0) {
      localParams[i] = (binparam)P_STR(optionalParams->Params[j].paramname, optionalParams->Params[j].str);

      continue;
    }

    if (optionalParams->Params[i].paramtype == 1) {
      localParams[i] = (binparam)P_NUM(optionalParams->Params[j].paramname, optionalParams->Params[j].num);

      continue;
    }

    if (optionalParams->Params[i].paramtype == 2) {
      localParams[i] = (binparam)P_BOOL(optionalParams->Params[j].paramname, optionalParams->Params[j].num);

      continue;
    }

    j++;
  }

  for (i = 0; i <= totalParCnt; i++) {
    if (localParams[i].paramtype == 0) {
      continue;
    }

    if (localParams[i].paramtype == 1) {
      continue;
    }
  }

  sock = psync_api_connect(binapi, psync_setting_get_bool(0));

  if (unlikely_log(!sock)) {
    if (err) {
      *err = psync_strdup("Could not connect to the server.");
    }

    return -1;
  }

  res = do_send_command(sock, wsPath, strlen(wsPath), localParams, totalParCnt, -1, 1);

  free(localParams);

  if (unlikely_log(!res)) {
    psync_socket_close(sock);

    if (err) {
      *err = psync_strdup("Could not connect to the server.");
    }

    return -1;
  }

  result = psync_find_result(res, "result", PARAM_NUM)->num;

  psync_socket_close(sock);

  if (result) {
    if (err) {
      *err = psync_strdup(psync_find_result(res, "error", PARAM_STR)->str);
    }

    debug(D_CRITICAL, "Backend command failed. Error Code: [%lld], Error:[%s]", result, *err);
  }
  else {
    if(strlen(payloadName) > 0) {
      payload = psync_find_result(res, payloadName, PARAM_HASH);

      *resData = (binresult*)malloc(payload->length * sizeof(binresult));
      memcpy(*resData, payload, (payload->length * sizeof(binresult)));
    }
  }

  return result;
}
/*************************************************************/
char* get_machine_name() {
  int   nameSize = 1024;
  char  pcName[1024];

  pcName[0] = 0;

#if defined(P_OS_WINDOWS)
  int   res;
  TCHAR lpBuffer[MAX_COMPUTERNAME_LENGTH + 1];

  nameSize = MAX_COMPUTERNAME_LENGTH+1;

  res = GetComputerNameW(lpBuffer, &nameSize);

  if (res > 0) {
    res = WideCharToMultiByte(
      CP_UTF8,
      NULL,
      lpBuffer,
      -1,
      pcName,
      1024,
      NULL,
      NULL
    );
  }
  else {
    debug(D_NOTICE, "Failed to get the machine name, Error code: [%d]", GetLastError());
  }

#endif

#if defined(P_OS_LINUX)
  gethostname(pcName, nameSize);
#endif

#if defined(P_OS_MACOSX)
  int byteRead;
  FILE* stream = popen("system_profiler SPSoftwareDataType | grep \"Computer Name\" | cut -d: -f2 | tr -d ' '", "r");

  while (!feof(stream) && !ferror(stream)) {
    byteRead = fread(pcName, 1, 128, stream);
  }
  if(byteRead > 0) {
    pcName[byteRead-1] = 0;
  }
#endif

  if (pcName[0] == 0) {
#if defined(P_OS_WINDOWS)
    strcpy(pcName,"WinMachine");
#endif

#if defined(P_OS_LINUX)
    strcpy(pcName, "LinuxMachine");
#endif

#if defined(P_OS_MACOSX)
    strcpy(pcName, "MacMachine");
#endif
  }

  debug(D_NOTICE, "Got machine name: [%s]", pcName);

  return psync_strdup(pcName);
}
/*************************************************************/
void parse_os_path(char* path, folderPath* folders, char* delim, int mode) {
  char fName[255];
  char* buff;
  int i = 0, j = 0, k = 0;

  if (strlen(path) < 1) {
    return;
  }

  while (1) {
    if (path[i] != delim) {
      if ((path[i] == ':') && (mode == 1)) {
        //In case we meet a ":" as in C:\ we set the name to Drive + the string before the ":"
        fName[k] = NULL;
        buff = psync_strcat("Drive ", &fName, NULL);
        psync_strlcpy(fName, buff, strlen(buff)+1);

        k = k + strlen("Drive ");
      }
      else {
        fName[k] = path[i];
        k++;
      }
    }
    else {
      fName[k] = 0;
      folders->folders[j] = psync_strdup(fName);

      k = 0;
      j++;
    }

    i++;

    if (path[i] == 0) {
      fName[k] = 0;

      if (strlen(fName) > 0) {
        folders->folders[j] = psync_strdup(fName);
        j++;
      }

      break;
    }
  }
  folders->cnt = j;
}
/*************************************************************/
void send_psyncs_event(const char* binapi,
                       const char* auth) {
  psync_folderid_t syncEventFlag = 0;
  time_t           rawtime;
  char*            errMsg;
  psync_sql_res*   sql;

  int intRes;
  int syncCnt = 0;

  errMsg = (char*)malloc(1024 * sizeof(char)); 
  errMsg[0] = 0;

  time(&rawtime);

  syncEventFlag = psync_sql_cellint("SELECT value FROM setting WHERE id='syncEventSentFlag'", 0);

  if (syncEventFlag != 1) {
    syncCnt = psync_sql_cellint("SELECT COUNT(*) FROM syncfolder WHERE synctype != 7", 0);

    if (syncCnt < 1) {
      debug(D_NOTICE, "No syncs, skip the event.");

      return;
    }

    eventParams params = {
      1, //Number of parameters we are passing below.
      {
        P_NUM(PSYNC_SYNCS_COUNT, syncCnt)
      }
    };

    intRes = create_backend_event(
      binapi,
      PSYNC_EVENT_CATEG,  // "SYNCS_EVENTS"
      PSYNC_EVENT_ACTION, // "SYNCS_LOG_COUNT"
      PSYNC_EVENT_LABEL,  // "SYNCS_COUNT"
      auth,
      P_OS_ID,
      rawtime,
      &params,
      &errMsg);

    debug(D_NOTICE, "Syncs Count Event Result:[%d], Message: [%s] .", intRes, errMsg);
    psync_free(errMsg);

    sql = psync_sql_prep_statement("REPLACE INTO setting (id, value) VALUES ('syncEventSentFlag', ?)");
    psync_sql_bind_uint(sql, 1, 1);
    psync_sql_run_free(sql);
  }
}
/*************************************************************/
int set_be_file_dates(uint64_t fileid, time_t ctime, time_t mtime) {
  int callRes;
  char msgErr[1024];
  binresult* retData;

  debug(D_NOTICE, "Update file date in the backend. FileId: [%lld], ctime: [%lld], mtime: [%lld]", fileid, ctime, mtime);

  eventParams optionalParams = {
    0
  };

  eventParams requiredParams1 = {
    5, {
      P_STR("auth", psync_my_auth),
      P_NUM("fileid", fileid),
      P_STR("timeformat", "timestamp"),
      P_NUM("newtm", ctime),
      P_BOOL("isctime", 1)
    }
  };

  callRes = backend_call(
    apiserver,
    "setfilemtime",
    FOLDER_META,
    &requiredParams1,
    &optionalParams,
    &retData,
    msgErr
  );

  debug(D_NOTICE, "cTime res: [%d]", callRes);

  eventParams requiredParams = {
    5, {
      P_STR("auth", psync_my_auth),
      P_NUM("fileid", fileid),
      P_STR("timeformat", "timestamp"),
      P_NUM("newtm", mtime),
      P_BOOL("isctime", 0)
    }
  };

  callRes = backend_call(
    apiserver,
    "setfilemtime",
    FOLDER_META,
    &requiredParams,
    &optionalParams,
    &retData,
    msgErr
  );

  debug(D_NOTICE, "mTime res: [%d]", callRes);

  return callRes;
}
/*************************************************************/
psync_syncid_t get_sync_id_from_fid(psync_folderid_t fid) {
  psync_sql_res* res;
  psync_variant_row row;
  psync_syncid_t syncId = -1;

  res = psync_sql_query("SELECT syncid FROM syncedfolder WHERE folderid = ?");

  psync_sql_bind_uint(res, 1, fid);

  if ((row = psync_sql_fetch_row(res))) {
    syncId = psync_get_number(row[0]);
  }

  psync_sql_free_result(res);

  return syncId;
}
/*************************************************************/
char *get_sync_folder_by_syncid(uint64_t syncId) {
  psync_sql_res* res;
  psync_variant_row row;
  const char* syncName;
  char* retName;

  res = psync_sql_query("SELECT localpath FROM syncfolder sf WHERE sf.id = ?");

  psync_sql_bind_uint(res, 1, syncId);

  if ((row = psync_sql_fetch_row(res))) {
    syncName = psync_get_string(row[0]);
  }
  else {
    psync_sql_free_result(res);
    return NULL;
  }

  retName = strdup(syncName);

  psync_sql_free_result(res);

  return retName;
}
/*************************************************************/
char* get_file_name_from_path(char* path){
  char* name;

  if(!path) {
    return NULL;
  }

  path = path + strlen(path)-1;

  while (*path != NULL) {
    if ((*path == '\\') || (*path == '/')) {
      break;
    }

    name = path;
    path--;
  }

  return strdup(name);
}
/*************************************************************/
char* get_folder_name_from_path(char* path) {
  char* folder;
  int sepFound = 0;

  while (*path != NULL) {
    if ((*path == '\\') || (*path == '/')) {
      folder = ++path;
      sepFound = 1;
    }

    path++;
  }

  if (sepFound) {
    return strdup(folder);
  }
  else {
    return strdup(STUCK_ITEM_UNKNOWN_FOLDER);
  }
}
/*************************************************************/
stuck_item* create_stuck_elem(uint64_t id, int msg_id, int item_type, uint64_t next_elem, char* path, char* name) {
  stuck_item* stuck_elem;

  pthread_mutex_lock(&stuck_elem_list_mutex);

  stuck_elem = psync_malloc(sizeof(stuck_item));

  stuck_elem->id = id;
  stuck_elem->item_type = item_type;
  stuck_elem->msg_id = msg_id;
  stuck_elem->retry_cnt = 0;
  stuck_elem->next_elem = NULL;

  if (name) {
    stuck_elem->name = strdup(name);
  }
  else {
    if (item_type == STUCK_ITEM_TYPE_FOLDER) {
      stuck_elem->name = strdup(STUCK_ITEM_UNKNOWN_FOLDER);
    }
    else {
      stuck_elem->name = strdup(STUCK_ITEM_UNKNOWN_FILE);
    }
  }
  
  if (path) {
    stuck_elem->path = strdup(path);
  }
  else {
    stuck_elem->path = strdup(STUCK_ITEM_UNKNOWN_PATH);
  }

  pthread_mutex_unlock(&stuck_elem_list_mutex);

  return stuck_elem;
}
/***********************************************************************/
void free_stuck_elem(stuck_item* elem) {
  psync_free(elem->name);
  psync_free(elem->path);
  
  psync_free(elem);
}
/***********************************************************************/
void log_list_elem(stuck_item* elem) {
  debug(D_NOTICE, "****** Stuck llist element ********");
  debug(D_NOTICE,"Item Id  : [%llu]", elem->id);
  debug(D_NOTICE,"Elem Type: [%d]", elem->item_type);
  debug(D_NOTICE,"Msg Id   : [%d]", elem->msg_id);
  debug(D_NOTICE,"Retry Cnt: [%d]", elem->retry_cnt);
  debug(D_NOTICE,"Next Elem: [%llu]", elem->next_elem);
  debug(D_NOTICE,"Elem Name: [%s]", elem->name);
  debug(D_NOTICE,"Elem Path: [%s]", elem->path);
  debug(D_NOTICE, "**********************************");
}
/***********************************************************************/
void log_list() {
  int i = 0;
  stuck_item* list = stuck_sync_tasks->list;

  while (1) {
    debug(D_NOTICE,"********************* Stuck List element [%d] *********************", i);
    log_list_elem(list);

    if (list->next_elem == NULL) {
      break;
    }

    list = (stuck_item*)list->next_elem;
    i++;
    debug(D_NOTICE, "******************** Stuck Next elem item id *********************");
  }
}
/***********************************************************************/
stuck_item* get_last_element() {
  stuck_item* local_list;

  local_list = stuck_sync_tasks->list;

  if (local_list->next_elem == NULL) {
    return NULL;
  }

  while (1) {
    local_list = (stuck_item*)local_list->next_elem;

    if (local_list->next_elem == NULL) {
      break;
    }
  }

  return local_list;
}
/***********************************************************************/
void add_stuck_elem(stuck_item* elem) {
  stuck_item* last_elem = NULL;

  pthread_mutex_lock(&stuck_elem_list_mutex);

  log_list_elem(elem);

  if (!stuck_sync_tasks->list) {
    stuck_sync_tasks->list = elem;
    stuck_sync_tasks->total_cnt = 1;

    if ((elem->retry_cnt >= STUCK_ITEM_RETRY_COUNT) && (elem->retry_cnt < STUCK_ITEM_RETRY_COUNT + 2)) {
      stuck_sync_tasks->stuck_cnt = 1;

      psync_send_data_event(PEVENT_STUCK_OBJ_CNT, "", "", stuck_sync_tasks->stuck_cnt, 0);
    }
    else {
      stuck_sync_tasks->stuck_cnt = 0;
    }

    pthread_mutex_unlock(&stuck_elem_list_mutex);

    return;
  }

  debug(D_NOTICE, "Adding element. Name: [%s]. Total number of elements in list: [%d], Stuck element: [%d]", elem->name, stuck_sync_tasks->total_cnt, stuck_sync_tasks->stuck_cnt);

  if (stuck_sync_tasks->stuck_cnt > STUCK_ITEM_TOTAL_COUNT) {
    pthread_mutex_unlock(&stuck_elem_list_mutex);
    debug(D_NOTICE, "Too many stuck elements in the list. Skip adding more.");

    return;
  }

  last_elem = search_list(elem->id);

  if (last_elem) {
    if(last_elem->retry_cnt < STUCK_ITEM_RETRY_COUNT + 2) {
      last_elem->retry_cnt++;
    }
  }
  else{
    if (stuck_sync_tasks->list->next_elem == NULL) { //Only one element in the list.
      stuck_sync_tasks->list->next_elem = (stuck_item*)elem;
    }
    else {
      last_elem = get_last_element(stuck_sync_tasks);// ???? if last_elem == -1
      if(last_elem){
        last_elem->next_elem = (stuck_item*)elem;
      }
    }

    if ((elem->retry_cnt >= STUCK_ITEM_RETRY_COUNT) && (elem->retry_cnt < STUCK_ITEM_RETRY_COUNT + 2)) {
      stuck_sync_tasks->stuck_cnt++;

      uint64_t sc = stuck_sync_tasks->stuck_cnt;
      psync_send_data_event(PEVENT_STUCK_OBJ_CNT, "", "", sc, 0);
    }

    stuck_sync_tasks->total_cnt++;
  }

  pthread_mutex_unlock(&stuck_elem_list_mutex);
}
/***********************************************************************/
stuck_item* search_list(uint64_t id) {
  stuck_item* local_list;

  if (!stuck_sync_tasks->list) {
    return NULL;
  }

  local_list = stuck_sync_tasks->list;

  while (1) {
    if (local_list->id == id) {
      return local_list;
    }

    if (local_list->next_elem == NULL) {
      return NULL;
    }

    local_list = (stuck_item*)local_list->next_elem;
  }
}
/***********************************************************************/
void init_stuck_list() {
  debug(D_NOTICE, "Init stuck list!");

  stuck_sync_tasks = (stuck_list_type*)psync_malloc(sizeof(stuck_list_type));

  stuck_sync_tasks->stuck_cnt = 0;
  stuck_sync_tasks->total_cnt = 0;
  stuck_sync_tasks->list = NULL;
}
/***********************************************************************/
void delete_element(uint64_t id) {
  pthread_mutex_lock(&stuck_elem_list_mutex);

  stuck_item* local_list = stuck_sync_tasks->list;
  stuck_item* last_element = NULL;
   
  debug(D_NOTICE, "Delete element with Id: [%llu], Stuck Cnt: [%d], Total Cnt: [%d]", id, stuck_sync_tasks->stuck_cnt, stuck_sync_tasks->total_cnt);

  if (local_list == NULL) {
    pthread_mutex_unlock(&stuck_elem_list_mutex);

    return;
  }

  while (1) {
    //First element of the list.
    if ((last_element == NULL) && (local_list->id == id)) {
      if (local_list->next_elem == NULL) {
        stuck_sync_tasks->list = NULL;
      }
      else {
        stuck_sync_tasks->list = (stuck_item*)local_list->next_elem;
      }

      if (local_list->retry_cnt >= STUCK_ITEM_RETRY_COUNT) {
        stuck_sync_tasks->stuck_cnt--;
      }

      stuck_sync_tasks->total_cnt--;

      free_stuck_elem(local_list);
      break;
    }

    //Last element of the list.
    if ((last_element != 0) && (local_list->next_elem == NULL) && (local_list->id == id)) {
      last_element->next_elem = NULL;

      if (local_list->retry_cnt >= STUCK_ITEM_RETRY_COUNT) {
        stuck_sync_tasks->stuck_cnt--;
      }

      stuck_sync_tasks->total_cnt--;

      free_stuck_elem(local_list);
      break;
    }

    //Element in the middle of the list.
    if (local_list->id == id) {
      if ((last_element != 0) && (local_list->next_elem != NULL)) {
        last_element->next_elem = (stuck_item*)local_list->next_elem;

        if (local_list->retry_cnt >= STUCK_ITEM_RETRY_COUNT) {
          stuck_sync_tasks->stuck_cnt--;
        }

        stuck_sync_tasks->total_cnt--;

        free_stuck_elem(local_list);
        break;
      }
    }

    if (local_list->next_elem == NULL) {
      pthread_mutex_unlock(&stuck_elem_list_mutex);

      return;
    }

    last_element = local_list;
    local_list = (stuck_item*)local_list->next_elem;
  }

  //Notify new stuck element count
  psync_send_data_event(PEVENT_STUCK_OBJ_CNT, "", "", stuck_sync_tasks->stuck_cnt, 0);

  pthread_mutex_unlock(&stuck_elem_list_mutex);
}
/*************************************************************/
void clean_stuck_list() {
  stuck_item* local_list;
  uint64_t next_elem;

  if (stuck_sync_tasks->list == NULL) {
    return;
  }

  pthread_mutex_lock(&stuck_elem_list_mutex);

  local_list = stuck_sync_tasks->list;

  while (1) {
    next_elem = local_list->next_elem;

    free_stuck_elem(local_list);

    local_list = (stuck_item*)next_elem;

    if (next_elem == NULL) {
      break;
    }
  }

  stuck_sync_tasks->stuck_cnt = 0;
  stuck_sync_tasks->total_cnt = 0;
  stuck_sync_tasks->list = NULL;

  psync_send_data_event(PEVENT_STUCK_OBJ_CNT, "", "", stuck_sync_tasks->stuck_cnt, 0);

  pthread_mutex_unlock(&stuck_elem_list_mutex);
}
/*************************************************************/
stuck_return_list* get_stuck_list() {
  stuck_item* local_list;
  stuck_return_list* list;

  if (!stuck_sync_tasks->list) {
    return NULL;
  }

  pthread_mutex_lock(&stuck_elem_list_mutex);

  list = (stuck_return_list*)psync_malloc(sizeof(stuck_return_list));

  list->elem_count = 0;

  local_list = stuck_sync_tasks->list;

  while (1) {
    if (local_list->retry_cnt >= STUCK_ITEM_RETRY_COUNT) {
      list->items[list->elem_count].name = psync_strdup(local_list->name);
      list->items[list->elem_count].path = psync_strdup(local_list->path);

      list->items[list->elem_count].msg_id = local_list->msg_id;
      list->items[list->elem_count].type   = local_list->item_type;
      list->elem_count++;

      if (list->elem_count >= STUCK_ITEM_RET_SIZE) {
        debug(D_NOTICE, "Maximum return list elements reached. Return.");
        break;
      }
    }

    if (local_list->next_elem == NULL) {
      break;
    }

    local_list = (stuck_item*)local_list->next_elem;
  }

  pthread_mutex_unlock(&stuck_elem_list_mutex);

  return list;
}
/*************************************************************/
char* nvl_str(char* str, const char* def) {
  if (str == NULL) {
    return def;
  }

  return str;
}
/***********************************************************************/
char* dns_lookup(const char* addr_host, int port) {
  char* ip[65];

  memset(ip, 0, sizeof(ip));

#if defined(P_OS_WINDOWS)
  int res;
  struct addrinfo hints, * addr_list;
  char port_str[6];
  WSADATA wsaData;

  if ((res = WSAStartup(MAKEWORD(2, 0), &wsaData)) != 0) {
    debug(D_WARNING, "Error initializing socket: [%s]", res);
    return NULL;
  }

  /* getaddrinfo expects port as a string */
  memset(port_str, 0, sizeof(port_str));
  snprintf(port_str, sizeof(port_str), "%d", port);

  /* Bind to IPv6 and/or IPv4, but only in TCP */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  res = getaddrinfo(addr_host, port_str, &hints, &addr_list);

  if (res != 0) {
    debug(D_WARNING, "Error resolving URL - getaddrinfo: [%d]", res);

    return NULL;
  }

  switch (addr_list->ai_family) {
    case AF_INET:
      inet_ntop(AF_INET, &((struct sockaddr_in*)addr_list->ai_addr)->sin_addr, ip, INET6_ADDRSTRLEN);
      break;
    case AF_INET6:
      inet_ntop(AF_INET6, &((struct sockaddr_in6*)addr_list->ai_addr)->sin6_addr, ip, INET6_ADDRSTRLEN);
      break;
  }
#endif
#if defined(P_OS_LINUX)
  char* cmd_str;
  char* output;

  cmd_str = malloc(sizeof(char) * (strlen(addr_host) + 100));

  sprintf(cmd_str, "ping -q -c1 -t1 %s | tr -d '()' | awk '/^PING/{print $3}'", addr_host);

  FILE* pipe = popen(cmd_str, "r");

  if (!pipe) {
    psync_free(cmd_str);
    return 0;
  }

  output = fgets(ip, sizeof(ip), pipe);

  if (ip[0] != 0) {
    ip[strlen(ip) - 1] = 0; //Overwrites the \n at the end.
  }

  psync_free(cmd_str);
  pclose(pipe);
#endif

#if defined(P_OS_MACOSX)
  char* cmd_str;
  int   byteRead = 0;


  cmd_str = malloc(sizeof(char) * (strlen(addr_host) + 100));

  sprintf(cmd_str, "ping -q -c1 -t1 %s | tr -d '():' | awk '/^PING/{print $3}'", addr_host);

  FILE* stream = popen(cmd_str, "r");

  while (!feof(stream) && !ferror(stream)) {
    byteRead = fread(ip, 1, sizeof(ip), stream);
  }
#endif

  return psync_strdup(ip);
}
/***********************************************************************/
void psync_log_tasks() {
  psync_sql_res* res;
  psync_variant_row row;
  uint64_t taskid;

  debug(D_NOTICE, "************Log tasks!**************");

  res = psync_sql_query("SELECT id, type, itemid, name, inprogress FROM task");

  while (row = psync_sql_fetch_row(res)) {
    debug(D_NOTICE, "Task: [%lld], Type: [%d], ItemId: [%llu], Name: [%s], Inprogress: [%d]", psync_get_number(row[0]), psync_get_number(row[1]), psync_get_number(row[2]), psync_get_string_or_null(row[3]), psync_get_number(row[4]));
  }

  psync_sql_free_result(res);

  debug(D_NOTICE, "*************************************");
}
/***********************************************************************/
uint64_t Hash64(const void* key, int len, unsigned int seed) {
    const unsigned int m = 0x5bd1e995;
    const int r = 24;
    unsigned int h1 = seed ^ len;
    unsigned int h2 = 0;

    const unsigned int* data = (const unsigned int*)key;

    while (len >= 8){
      unsigned int k1 = *data++;
      k1 *= m; k1 ^= k1 >> r; k1 *= m;
      h1 *= m; h1 ^= k1;
      len -= 4;

      unsigned int k2 = *data++;
      k2 *= m; k2 ^= k2 >> r; k2 *= m;
      h2 *= m; h2 ^= k2;
      len -= 4;
    }

    if (len >= 4) {
      unsigned int k1 = *data++;
      k1 *= m; k1 ^= k1 >> r; k1 *= m;
      h1 *= m; h1 ^= k1;
      len -= 4;
    }

    switch (len) {
      case 3: h2 ^= ((unsigned char*)data)[2] << 16;
      case 2: h2 ^= ((unsigned char*)data)[1] << 8;
      case 1: h2 ^= ((unsigned char*)data)[0];
      h2 *= m;
    };

    h1 ^= h2 >> 18; h1 *= m;
    h2 ^= h1 >> 22; h2 *= m;
    h1 ^= h2 >> 17; h1 *= m;
    h2 ^= h1 >> 19; h2 *= m;

    uint64_t h = h1;

    h = (h << 32) | h2;

    return h;
  }
/***********************************************************************/
int do_get_crypto_price(char** currency)
{
  int price = 0, err;
  const binresult* cres, * products;
  binresult* res;
  binparam params[] = { P_STR("auth", psync_my_auth), P_NUM("products", 101/*crypto*/), P_STR("period", "month") };

  if (psync_my_auth[0]) {
    res = psync_api_run_command("getprice", params);
    if (!res) {
      psync_free(res);
      return 0;
    }
    err = psync_find_result(res, "result", PARAM_NUM)->num;
    if (err)
    {
      psync_free(res);
      return 0;
    }

    *currency=strdup(psync_find_result(res, "currency", PARAM_STR)->str);
    products = psync_check_result(res, "products", PARAM_HASH);
    if (products) {
      cres = psync_check_result(products, "101", PARAM_HASH);
      if (cres) price = psync_find_result(cres, "pricecents", PARAM_NUM)->num;
    }
  }
  return price;
}
/***********************************************************************/
int call_ebackend(const char* method, binparam* paramas, int param_cnt, binresult** resData) {
  binresult*    res;
  psync_socket* sock;
  size_t resLen;

  sock = psync_api_connect(PSYNC_API_HOST, psync_setting_get_bool(0));

  if (unlikely_log(!sock)) {
    debug(D_ERROR, "Could not connect to the server.");

    return -1;
  }

  debug(D_NOTICE, "call_ebackend. Send command [%s]", method);

  res = do_send_command(sock, method, strlen(method), paramas, param_cnt, -1, 1);

  psync_socket_close(sock);

  if (!res) {
    return 6002;//Backend code for timeout
  }

  *resData = (binresult*)malloc(res->length * sizeof(binresult));
  memcpy(*resData, res, (res->length * sizeof(binresult)));

  return 0;
}
/***********************************************************************/
int get_login_req_id(char** reqId) {
  int res = -1;
  uint64_t result;
  char* expireTime;
  binresult* resData = NULL;

  res = call_ebackend(WEB_LOGIN_GET_REQ_ID, NULL, 0, &resData);

  if (res != 0) {
    return res;
  }

  result = psync_find_result(resData, "result", PARAM_NUM)->num;

  if (result != 0) {
    debug(D_ERROR, "get_login_req_id. Backend returned error: [%llu]", result);

    return result;
  }

  *reqId = psync_strdup(psync_find_result(resData, "request_id", PARAM_STR)->str);
  debug(D_NOTICE, "get_login_req_id. Request Id: [%s]", *reqId);

  expireTime = psync_strdup(psync_find_result(resData, "expires", PARAM_STR)->str);

  if (resData) {
    psync_free(resData);
  }

  return 0;
}
/***********************************************************************/
int wait_auth_token(char* request_id) {
  int res, loc_id, last_loc_id = 0;
  uint64_t result, currentuserid, newuserid = 666, rememberme = 0;
  char* token;
  binresult* resData = NULL;

  binparam params[] = {
    P_NUM(EPARAM_LOGIN, 1), //Fixed paramaeter, so the backend can recognize the caller.
    P_STR(EPARAM_REQ_ID, request_id),
    P_NUM(EPARAM_TIMEOUT, 500) //Timeout. Optional.
  };

  debug(D_NOTICE, "Wait login token.");

  res = call_ebackend(WEB_LOGIN_WAIT_AUTH, params, 3, &resData);

  if (res != 0) {
    return res;
  }

  result = psync_find_result(resData, "result", PARAM_NUM)->num;

  if (result != 0) {
    debug(D_NOTICE, "Backend returned error: [%llu]", result);

    return result;
  }

  token = psync_strdup(psync_find_result(resData, EPARAM_TOKEN, PARAM_STR)->str);
  loc_id = psync_find_result(resData, EPARAM_LOC_ID, PARAM_NUM)->num;
  newuserid = psync_find_result(resData, EPARAM_USER_ID, PARAM_NUM)->num;

  if (psync_check_result(resData, EPARAM_REMEMBERME, PARAM_BOOL)) {
    rememberme = psync_find_result(resData, EPARAM_REMEMBERME, PARAM_BOOL)->num;
  }

  debug(D_NOTICE, "Login result parameters: RememberMe: [%llu], UserId: [%llu], Location Id: [%d]", rememberme, newuserid, loc_id);

  if (resData) {
    psync_free(resData);
  }

  currentuserid = psync_get_uint_value("userid");

  if (currentuserid) {
    if (currentuserid != newuserid) {
      debug(D_NOTICE, "New user detected. Unlink.");
      psync_unlink();

      psync_recache_contacts = 1;

      psync_set_int_value("userid", newuserid);
    }
  }

  last_loc_id = psync_get_uint_value("location_id");

  psync_set_int_value("last_logged_location_id", loc_id);
  psync_set_int_value("location_id", loc_id);

  if (loc_id == 1) {//User is located in US
    debug(D_NOTICE, "US location detected.");
    psync_set_apiserver(PSYNC_API_HOST_US,loc_id);
  }
  else if((loc_id == 2) || (loc_id == 0)) {//EU user
    debug(D_NOTICE, "EU location detected.");
    psync_set_apiserver(PSYNC_API_HOST, loc_id);
  }
  else {
    debug(D_CRITICAL, "Unknown user location! [%d]", loc_id);
  }

  if (rememberme) {
    psync_strlcpy(psync_my_auth, token, sizeof(psync_my_auth));
  }

  psync_set_auth(token, rememberme);
  
  return result;
}
/**********************************************************************/
int deleteLogs() {
  int res;

#if defined(P_OS_LINUX)
  char* srcFname1 = DEBUG_FILE;

  res = psync_file_delete(srcFname1);
  debug(D_NOTICE, "Deleting log file [%s]. Res: [%d]", srcFname1, res);
#endif

#if defined(P_OS_MACOSX)
  char* srcFname1 = DEBUG_FILE;

  res = psync_file_delete(srcFname1);
  debug(D_NOTICE, "Deleting log file [%s]. Res: [%d]", srcFname1, res);
#endif

#if defined(P_OS_WINDOWS)
  char* srcFname1 = psync_strcat(appDriveLetter, "tmp", PSYNC_DIRECTORY_SEPARATOR, "psync_err.log", NULL);
  char* srcFname2 = psync_strcat(appDriveLetter, "tmp", PSYNC_DIRECTORY_SEPARATOR, "cbfs_log.log", NULL);
  char* srcFname3 = psync_strcat(psync_get_pcloud_path(), PSYNC_DIRECTORY_SEPARATOR, "wpflog.log", NULL);

  res = psync_file_delete(srcFname2);
  debug(D_NOTICE, "Deleting log file [%s]. Res: [%d]", srcFname2, res);

  res = psync_file_delete(srcFname3);
  debug(D_NOTICE, "Deleting log file [%s]. Res: [%d]", srcFname3, res);
#endif

  res = psync_file_delete(srcFname1);
  debug(D_NOTICE, "Deleting log file [%s]. Res: [%d]", srcFname1, res);

  return res;
}
/**********************************************************************/
//Upload tasks methods
int create_upload_task(int type, int status, uint64_t size, int level, uint64_t parentfid, char* fname, char* path) {
  psync_sql_res* res;
  uint64_t upTaskId;

  debug(D_NOTICE, "BOBO: create_upload_task. Start Transaction. Type: [%d], Status: [%d], Size: [%llu] Level: [%d], parentfid: [%llu], Name: [%s], Path: [%s]", type, status, size, level, parentfid, fname, path);

  psync_sql_start_transaction();

  debug(D_NOTICE, "BOBO: create_upload_task. Prepare statement.");

  res = psync_sql_prep_statement("INSERT INTO upload_tasks(type, status, size, level, parentfid, fname, fpath) VALUES (? ,? ,? ,? ,? ,?, ?); ");
  
  debug(D_NOTICE, "BOBO: create_upload_task. Bind variables.");

  psync_sql_bind_int(res,  1, type);
  psync_sql_bind_int(res,  2, status);
  psync_sql_bind_uint(res, 3, size);
  psync_sql_bind_int(res,  4, level);
  psync_sql_bind_uint(res, 5, parentfid);

  psync_sql_bind_lstring(res, 6, fname, strlen(fname));
  psync_sql_bind_lstring(res, 7, path, strlen(path));

  debug(D_NOTICE, "BOBO: create_upload_task. Run query.");

  if (unlikely(psync_sql_run_free(res))) { 
    psync_sql_rollback_transaction();

    debug(D_NOTICE, "BOBO: create_upload_task. Transaction failed.");

    return -1;
  }

  upTaskId = psync_sql_insertid();

  psync_sql_commit_transaction();

  debug(D_NOTICE, "BOBO: create_upload_task. Return UpTaskId: [%llu]", upTaskId);

  return upTaskId;
}
/**********************************************************************/
uint64_t create_uptask_lfolder_in_db(uint64_t parent_folder_id, char* foname) {
  psync_sql_res* sql;
  psync_fileid_t localfolderid;

  debug(D_NOTICE, "BOBO: Add local folder in DB. Parent Folder Id: [%llu] Name: [%s]", parent_folder_id, foname);

  psync_sql_start_transaction();

  sql = psync_sql_prep_statement("REPLACE INTO localfolder (localparentfolderid, syncid, name) VALUES (?, ?, ?)");

  psync_sql_bind_uint(sql, 1, parent_folder_id); //parent_folder_id
  psync_sql_bind_uint(sql, 2, 0); //syncid
  psync_sql_bind_lstring(sql, 3, foname, strlen(foname));

  psync_sql_run_free(sql);

  psync_sql_commit_transaction();

  localfolderid = psync_sql_insertid();

  debug(D_NOTICE, "BOBO: Returning Local folder Id: [%llu]", localfolderid);

  return localfolderid;
}
/**********************************************************************/
uint64_t create_local_file_in_db(uint64_t parent_folder_id) {
  psync_sql_res* sql;
  psync_fileid_t localfileid;

  debug(D_NOTICE, "BOBO: Add local file in DB. Parent Folder Id: [%llu]", parent_folder_id);

  psync_sql_start_transaction();

  sql = psync_sql_prep_statement("REPLACE INTO localfile (localparentfolderid, syncid) VALUES (?, ?)");

  psync_sql_bind_uint(sql, 1, parent_folder_id); //parent_folder_id
  psync_sql_bind_uint(sql, 2, 0); //syncid
  psync_sql_run_free(sql);

  psync_sql_commit_transaction();

  localfileid = psync_sql_insertid();

  debug(D_NOTICE, "BOBO: Returning Local File Id: [%llu]", localfileid);

  return localfileid;
}
/**********************************************************************/
void upload_tasks_status_thread() {
  psync_variant* row;
  uint64_t Waiting = 0, InProgress = 0, Finished = 0, Failed = 0;


  debug(D_NOTICE, "BOBO: PSYNC_UPLOAD_FILE:[%d]", NTO_STR(PSYNC_UPLOAD_FILE));

  while (1) {
    row = psync_sql_row("SELECT IFNULL(SUM(status = "NTO_STR(PUPTASK_STATUS_WAITING)"), 0)    AS Waiting,    "
                        "       IFNULL(SUM(status = "NTO_STR(PUPTASK_STATUS_INPROGRESS)"), 0) AS InProgress, "
                        "       IFNULL(SUM(status = "NTO_STR(PUPTASK_STATUS_FINISHED)"), 0)   AS Finished,   "
                        "       IFNULL(SUM(status = "NTO_STR(PUPTASK_STATUS_FAILED)"), 0)     AS Failed      "
                        "  FROM upload_tasks"
                        " WHERE type = 3" //Files only
                        );

    if (row) {
      //debug(D_NOTICE, "BOBO: Upload tasks Last Status: Waiting: [%llu], In Progress: [%llu], Finished: [%llu], Failed: [%llu] ", Waiting, InProgress, Finished, Failed);
 
      if ((Waiting    != psync_get_number(row[0])) ||
          (InProgress != psync_get_number(row[1])) ||
          (Finished   != psync_get_number(row[2])) ||
          (Failed     != psync_get_number(row[3]))) {
        debug(D_NOTICE, "BOBO: Change in stats. Send event.");

        Waiting    = psync_get_number(row[0]);
        InProgress = psync_get_number(row[1]);
        Finished   = psync_get_number(row[2]);
        Failed     = psync_get_number(row[3]);

        psync_send_data_event(PEVENT_UPL_TASKS_STAT, NULL, NULL, (Finished), (Waiting + InProgress + Finished + Failed)); //Finished, Total

        if ((Waiting == 0) && (InProgress == 0)){
          psync_send_data_event(PEVENT_UPL_TASKS_FINISH, NULL, NULL, (Finished), (Waiting + InProgress + Finished + Failed)); //Finished, Total
        }
      }
      else {
        //debug(D_NOTICE, "BOBO: No change in stats. Wait.");
      }

      psync_free(row);
    }
    else {
      debug(D_NOTICE, "BOBO: error selecting upload tasks stats.");
    }

    psync_milisleep(2000);
  }
}
/**********************************************************************/
uptask_item_list* get_uptask_item_list(int status) {
  uptask_item_list uptask_list;
  psync_sql_res* res;
  psync_variant_row row;
  int i = 0;

  debug(D_NOTICE, "BOBO: get_uptask_item_list. Start. Status: [%d]", status);

  res = psync_sql_query("SELECT type, status, fpath, fname, size, error_code "
                        "  FROM upload_tasks "
                        " WHERE (status & ?)"
                        " LIMIT 10");

  psync_sql_bind_uint(res, 1, status);

  while (row = psync_sql_fetch_row(res)) {
    uptask_list.list[i].item_type = psync_get_number(row[0]);
    uptask_list.list[i].item_status = psync_get_number(row[1]);
    uptask_list.list[i].path = psync_strdup(psync_get_string(row[2]));
    uptask_list.list[i].name = psync_strdup(psync_get_string(row[3]));
    uptask_list.list[i].size = psync_get_number(row[4]);
    uptask_list.list[i].error_code = psync_get_number(row[5]);

    debug(D_NOTICE, "BOBO: Add to list: Type: [%d], Status: [%d], Path:[%s], Name:[%s] Size: [%llu] Error Code: [%d].", uptask_list.list[i].item_type, uptask_list.list[i].item_status, uptask_list.list[i].path, uptask_list.list[i].name, uptask_list.list[i].size, uptask_list.list[i].error_code);

    //psync_free(row);

    i++;
  }

  uptask_list.item_cnt = i;

  psync_sql_free_result(res);

  debug(D_NOTICE, "BOBO: Set list Cound To: [%d]", uptask_list.item_cnt);

  return &uptask_list;
}
/**********************************************************************/
void log_uptasks() {
  int i;

  uptask_item_list* uptask_list;

  uptask_list = get_uptask_item_list(15);

  debug(D_NOTICE, "BOBO: Uptask Count: [%d]", uptask_list->item_cnt);

  if (uptask_list != NULL) {
    debug(D_NOTICE, "***********************************************************");
    for (i = 0; i < uptask_list->item_cnt; i++) {
      debug(D_NOTICE, "BOBO: Task: Status: [%d] Type: [%d] Name: [%s] Path: [%s]", uptask_list->list[i].item_status, uptask_list->list[i].item_type, uptask_list->list[i].name, uptask_list->list[i].path);
      psync_free(uptask_list->list[i].name);
      psync_free(uptask_list->list[i].path);

      if (i > 15) {
        break;
      }
    }
    debug(D_NOTICE, "***********************************************************");
  }
  else {
    debug(D_NOTICE, "BOBO: No tasks to log.");
  }

  //psync_free(uptask_list);
}
/**********************************************************************/
int64_t get_db_id() {
  psync_sql_res* res;

  psync_sql_start_transaction();

  res = psync_sql_prep_statement("INSERT INTO pagecache (type) VALUES ("NTO_STR(PAGE_TYPE_FREE)")");

  psync_sql_commit_transaction();

  psync_sql_free_result(res);
}
/**********************************************************************/
/**********************************************************************/
/**********************************************************************/