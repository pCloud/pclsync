/* Copyright (c) 2013-2015 pCloud Ltd.
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

#include "cli_ipc.h"
#include "pcompat.h"
#include "psynclib.h"
#include "pfolder.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define IPC_BUFSIZE 4096

static volatile int ipc_shutdown=0;
static int ipc_listen_fd=-1;
static char ipc_sock_path[108]; /* sizeof(sun_path) on Linux */

/* ───────────────────────── helpers ───────────────────────── */

static int build_sock_path(char *buf, size_t bufsz, const char *datadir){
  int n=snprintf(buf, bufsz, "%s/%s", datadir, CLI_IPC_SOCK_NAME);
  if (n<0 || (size_t)n>=bufsz){
    fprintf(stderr, "Error: datadir path too long for Unix socket\n");
    return -1;
  }
  return 0;
}

/* Read a single \n-terminated line from fd into buf (NUL-terminated, newline stripped).
 * Returns number of bytes read (excluding newline), or -1 on error/EOF. */
static ssize_t ipc_readline(int fd, char *buf, size_t bufsz){
  size_t pos=0;
  while (pos<bufsz-1){
    ssize_t n=read(fd, buf+pos, 1);
    if (n<=0)
      return pos>0?(ssize_t)pos:-1;
    if (buf[pos]=='\n'){
      buf[pos]='\0';
      return (ssize_t)pos;
    }
    pos++;
  }
  buf[pos]='\0';
  return (ssize_t)pos;
}

static int ipc_writeline(int fd, const char *line){
  size_t len=strlen(line);
  char nl='\n';
  ssize_t w=write(fd, line, len);
  if (w<0) return -1;
  w=write(fd, &nl, 1);
  return w<0?-1:0;
}

static void ipc_send_ok(int fd, const char *payload){
  if (payload){
    char buf[IPC_BUFSIZE];
    snprintf(buf, sizeof(buf), "OK %s", payload);
    ipc_writeline(fd, buf);
  } else {
    ipc_writeline(fd, "OK");
  }
  ipc_writeline(fd, ".");
}

static void ipc_send_err(int fd, const char *reason){
  char buf[IPC_BUFSIZE];
  snprintf(buf, sizeof(buf), "ERR %s", reason);
  ipc_writeline(fd, buf);
  ipc_writeline(fd, ".");
}

static const char *synctype_to_str(uint32_t t){
  switch (t){
    case PSYNC_DOWNLOAD_ONLY: return "download";
    case PSYNC_UPLOAD_ONLY:   return "upload";
    case PSYNC_FULL:          return "full";
    default:                  return "unknown";
  }
}

/* ──────────────────── server command handlers ──────────────────── */

/* SYNC_ADD <synctype_int> <localpath>\t<remotepath> */
static void cmd_sync_add(int fd, const char *args){
  char *tab;
  int synctype;
  char argcopy[IPC_BUFSIZE];

  strncpy(argcopy, args, sizeof(argcopy)-1);
  argcopy[sizeof(argcopy)-1]='\0';

  /* parse synctype (first token) */
  char *space=strchr(argcopy, ' ');
  if (!space){
    ipc_send_err(fd, "invalid SYNC_ADD format");
    return;
  }
  *space='\0';
  synctype=atoi(argcopy);
  if (synctype<PSYNC_SYNCTYPE_MIN || synctype>PSYNC_SYNCTYPE_MAX){
    ipc_send_err(fd, "invalid sync type");
    return;
  }

  /* remainder: localpath\tremotepath */
  char *rest=space+1;
  tab=strchr(rest, '\t');
  if (!tab){
    ipc_send_err(fd, "missing tab delimiter between local and remote path");
    return;
  }
  *tab='\0';
  char *localpath=rest;
  char *remotepath=tab+1;

  if (strlen(localpath)==0 || strlen(remotepath)==0){
    ipc_send_err(fd, "empty local or remote path");
    return;
  }

  int ret=psync_add_sync_by_path_delayed(localpath, remotepath, (psync_synctype_t)synctype);
  if (ret==0)
    ipc_send_ok(fd, "0");
  else {
    char errbuf[256];
    snprintf(errbuf, sizeof(errbuf), "psync_add_sync_by_path_delayed failed (%d)", ret);
    ipc_send_err(fd, errbuf);
  }
}

/* SYNC_LIST */
static void cmd_sync_list(int fd){
  psync_folder_list_t *list=psync_get_sync_list();
  if (!list){
    ipc_send_err(fd, "failed to get sync list");
    return;
  }
  ipc_writeline(fd, "OK");
  for (size_t i=0; i<list->foldercnt; i++){
    psync_folder_t *f=&list->folders[i];
    char line[IPC_BUFSIZE];
    snprintf(line, sizeof(line), "%u\t%s\t%s\t%s",
             (unsigned)f->syncid, f->localpath, f->remotepath,
             synctype_to_str(f->synctype));
    ipc_writeline(fd, line);
  }
  ipc_writeline(fd, ".");
  free(list);
}

/* SYNC_DELETE <syncid> */
static void cmd_sync_delete(int fd, const char *args){
  char *endptr;
  unsigned long val=strtoul(args, &endptr, 10);
  if (*endptr!='\0' && *endptr!='\n'){
    ipc_send_err(fd, "invalid sync id");
    return;
  }
  int ret=psync_delete_sync((psync_syncid_t)val);
  if (ret==0)
    ipc_send_ok(fd, NULL);
  else {
    char errbuf[256];
    snprintf(errbuf, sizeof(errbuf), "failed to delete sync %lu", val);
    ipc_send_err(fd, errbuf);
  }
}

/* STATUS */
static const char *status_code_to_str(uint32_t status){
  switch (status){
    case PSTATUS_READY:                   return "ready";
    case PSTATUS_DOWNLOADING:             return "downloading";
    case PSTATUS_UPLOADING:               return "uploading";
    case PSTATUS_DOWNLOADINGANDUPLOADING: return "syncing";
    case PSTATUS_LOGIN_REQUIRED:          return "login required";
    case PSTATUS_BAD_LOGIN_DATA:          return "bad login data";
    case PSTATUS_BAD_LOGIN_TOKEN:         return "bad login token";
    case PSTATUS_ACCOUNT_FULL:            return "account full";
    case PSTATUS_DISK_FULL:               return "disk full";
    case PSTATUS_PAUSED:                  return "paused";
    case PSTATUS_STOPPED:                 return "stopped";
    case PSTATUS_OFFLINE:                 return "offline";
    case PSTATUS_CONNECTING:              return "connecting";
    case PSTATUS_SCANNING:                return "scanning";
    default:                              return "unknown";
  }
}

static void cmd_status(int fd){
  pstatus_t st;
  psync_get_status(&st);

  psync_folder_list_t *list=psync_get_sync_list();
  size_t synccnt=list?list->foldercnt:0;
  if (list)
    free(list);

  ipc_writeline(fd, "OK");

  char line[IPC_BUFSIZE];
  snprintf(line, sizeof(line), "status\t%s", status_code_to_str(st.status));
  ipc_writeline(fd, line);
  snprintf(line, sizeof(line), "syncs\t%zu", synccnt);
  ipc_writeline(fd, line);
  snprintf(line, sizeof(line), "uploads\t%u pending, %u active, %u B/s",
           st.filestoupload, st.filesuploading, st.uploadspeed);
  ipc_writeline(fd, line);
  snprintf(line, sizeof(line), "downloads\t%u pending, %u active, %u B/s",
           st.filestodownload, st.filesdownloading, st.downloadspeed);
  ipc_writeline(fd, line);
  if (st.remoteisfull)
    ipc_writeline(fd, "warning\taccount storage is full");
  if (st.localisfull)
    ipc_writeline(fd, "warning\tlocal disk is full");

  ipc_writeline(fd, ".");
}

/* SYNC_STATUS */
static void cmd_sync_status(int fd){
  pstatus_t st;
  psync_get_status(&st);

  psync_folder_list_t *list=psync_get_sync_list();
  if (!list){
    ipc_send_err(fd, "failed to get sync list");
    return;
  }

  ipc_writeline(fd, "OK");

  char line[IPC_BUFSIZE];
  snprintf(line, sizeof(line), "state\t%s", status_code_to_str(st.status));
  ipc_writeline(fd, line);

  for (size_t i=0; i<list->foldercnt; i++){
    psync_folder_t *f=&list->folders[i];
    snprintf(line, sizeof(line), "%u\t%s\t%s\t%s",
             (unsigned)f->syncid, f->localpath, f->remotepath,
             synctype_to_str(f->synctype));
    ipc_writeline(fd, line);
  }

  ipc_writeline(fd, ".");
  free(list);
}

/* SYNC_STATE */
static void cmd_sync_state(int fd){
  pstatus_t st;
  psync_get_status(&st);
  ipc_writeline(fd, "OK");
  ipc_writeline(fd, status_code_to_str(st.status));
  ipc_writeline(fd, ".");
}

/* CRYPTO_STATUS */
static void cmd_crypto_status(int fd){
  char line[IPC_BUFSIZE];
  int is_setup=psync_crypto_issetup();
  int is_started=psync_crypto_isstarted();
  int has_sub=psync_crypto_hassubscription();
  int is_expired=psync_crypto_isexpired();
  time_t expires=psync_crypto_expires();

  ipc_writeline(fd, "OK");

  snprintf(line, sizeof(line), "setup\t%s", is_setup?"yes":"no");
  ipc_writeline(fd, line);
  snprintf(line, sizeof(line), "locked\t%s", is_started?"no":"yes");
  ipc_writeline(fd, line);

  if (!has_sub)
    snprintf(line, sizeof(line), "subscription\tnone");
  else if (is_expired)
    snprintf(line, sizeof(line), "subscription\texpired");
  else
    snprintf(line, sizeof(line), "subscription\tactive");
  ipc_writeline(fd, line);

  if (expires){
    char datebuf[32];
    struct tm *tm=localtime(&expires);
    strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", tm);
    snprintf(line, sizeof(line), "expires\t%s", datebuf);
    ipc_writeline(fd, line);
  }

  if (is_setup){
    psync_folderid_t *fids=psync_crypto_folderids();
    if (fids){
      for (int i=0; fids[i]!=PSYNC_CRYPTO_INVALID_FOLDERID; i++){
        char *path=psync_get_path_by_folderid(fids[i], NULL);
        if (path){
          snprintf(line, sizeof(line), "folder\t%s", path);
          ipc_writeline(fd, line);
          free(path);
        }
      }
      free(fids);
    }
  }

  ipc_writeline(fd, ".");
}

/* CRYPTO_UNLOCK_STATE */
static void cmd_crypto_unlock_state(int fd){
  ipc_writeline(fd, "OK");
  if (!psync_crypto_issetup())
    ipc_writeline(fd, "not setup");
  else if (psync_crypto_isstarted())
    ipc_writeline(fd, "unlocked");
  else
    ipc_writeline(fd, "locked");
  ipc_writeline(fd, ".");
}

/* CRYPTO_LIST */
static void cmd_crypto_list(int fd){
  char line[IPC_BUFSIZE];
  ipc_writeline(fd, "OK");
  psync_folderid_t *fids=psync_crypto_folderids();
  if (fids){
    for (int i=0; fids[i]!=PSYNC_CRYPTO_INVALID_FOLDERID; i++){
      char *path=psync_get_path_by_folderid(fids[i], NULL);
      if (path){
        snprintf(line, sizeof(line), "%s", path);
        ipc_writeline(fd, line);
        free(path);
      }
    }
    free(fids);
  }
  ipc_writeline(fd, ".");
}

/* CRYPTO_UNLOCK <password> */
static void cmd_crypto_unlock(int fd, const char *password){
  int ret=psync_crypto_start(password);
  switch (ret){
    case PSYNC_CRYPTO_START_SUCCESS:
      ipc_writeline(fd, "OK unlocked");
      break;
    case PSYNC_CRYPTO_START_ALREADY_STARTED:
      ipc_writeline(fd, "OK already unlocked");
      break;
    case PSYNC_CRYPTO_START_BAD_PASSWORD:
      ipc_writeline(fd, "ERR bad password");
      break;
    case PSYNC_CRYPTO_START_NOT_SETUP:
      ipc_writeline(fd, "ERR crypto not set up");
      break;
    case PSYNC_CRYPTO_START_NOT_LOGGED_IN:
      ipc_writeline(fd, "ERR not logged in");
      break;
    case PSYNC_CRYPTO_START_CANT_CONNECT:
      ipc_writeline(fd, "ERR cannot connect");
      break;
    default:
      ipc_writeline(fd, "ERR unlock failed");
      break;
  }
  ipc_writeline(fd, ".");
}

/* CRYPTO_LOCK */
static void cmd_crypto_lock(int fd){
  int ret=psync_crypto_stop();
  switch (ret){
    case PSYNC_CRYPTO_STOP_SUCCESS:
      ipc_writeline(fd, "OK locked");
      break;
    case PSYNC_CRYPTO_STOP_NOT_STARTED:
      ipc_writeline(fd, "OK already locked");
      break;
    default:
      ipc_writeline(fd, "ERR lock failed");
      break;
  }
  ipc_writeline(fd, ".");
}

/* FOLDER_PATH <folderid> */
static void cmd_folder_path(int fd, const char *arg){
  char line[IPC_BUFSIZE];
  psync_folderid_t fid=(psync_folderid_t)strtoull(arg, NULL, 10);
  char *path=psync_get_path_by_folderid(fid, NULL);
  if (path){
    snprintf(line, sizeof(line), "OK %s", path);
    ipc_writeline(fd, line);
    free(path);
  } else {
    ipc_writeline(fd, "ERR folder not found");
  }
  ipc_writeline(fd, ".");
}

/* FILE_PATH <fileid> */
static void cmd_file_path(int fd, const char *arg){
  char line[IPC_BUFSIZE];
  psync_fileid_t fid=(psync_fileid_t)strtoull(arg, NULL, 10);
  char *path=psync_get_path_by_fileid(fid, NULL);
  if (path){
    snprintf(line, sizeof(line), "OK %s", path);
    ipc_writeline(fd, line);
    free(path);
  } else {
    ipc_writeline(fd, "ERR file not found");
  }
  ipc_writeline(fd, ".");
}

/* SHUTDOWN */
static void cmd_shutdown(int fd){
  ipc_send_ok(fd, "shutting down");
  close(fd);
  /* Signal the main process to initiate graceful shutdown */
  kill(getpid(), SIGTERM);
}

/* ──────────────────── server accept loop ──────────────────── */

static void ipc_handle_connection(void *arg){
  int fd=*(int *)arg;
  free(arg);
  char buf[IPC_BUFSIZE];
  ssize_t n=ipc_readline(fd, buf, sizeof(buf));
  if (n<=0){
    close(fd);
    return;
  }

  if (strncmp(buf, "SYNC_ADD ", 9)==0)
    cmd_sync_add(fd, buf+9);
  else if (strcmp(buf, "SYNC_LIST")==0)
    cmd_sync_list(fd);
  else if (strcmp(buf, "SYNC_STATUS")==0)
    cmd_sync_status(fd);
  else if (strcmp(buf, "SYNC_STATE")==0)
    cmd_sync_state(fd);
  else if (strncmp(buf, "SYNC_DELETE ", 12)==0)
    cmd_sync_delete(fd, buf+12);
  else if (strcmp(buf, "STATUS")==0)
    cmd_status(fd);
  else if (strcmp(buf, "CRYPTO_STATUS")==0)
    cmd_crypto_status(fd);
  else if (strcmp(buf, "CRYPTO_UNLOCK_STATE")==0)
    cmd_crypto_unlock_state(fd);
  else if (strncmp(buf, "CRYPTO_UNLOCK ", 14)==0)
    cmd_crypto_unlock(fd, buf+14);
  else if (strcmp(buf, "CRYPTO_LIST")==0)
    cmd_crypto_list(fd);
  else if (strcmp(buf, "CRYPTO_LOCK")==0)
    cmd_crypto_lock(fd);
  else if (strncmp(buf, "FOLDER_PATH ", 12)==0)
    cmd_folder_path(fd, buf+12);
  else if (strncmp(buf, "FILE_PATH ", 10)==0)
    cmd_file_path(fd, buf+10);
  else if (strcmp(buf, "SHUTDOWN")==0)
    cmd_shutdown(fd);
  else
    ipc_send_err(fd, "unknown command");

  if (strcmp(buf, "SHUTDOWN")!=0)
    close(fd);
}

static void ipc_accept_loop(void){
  while (!ipc_shutdown){
    int cl=accept(ipc_listen_fd, NULL, NULL);
    if (cl<0)
      break;
    int *fdp=(int *)malloc(sizeof(int));
    if (!fdp){
      close(cl);
      continue;
    }
    *fdp=cl;
    psync_run_thread1("cli IPC handler", ipc_handle_connection, fdp);
  }
}

/* ──────────────────── server public API ──────────────────── */

int cli_ipc_server_start(const char *datadir){
  struct sockaddr_un addr;

  if (build_sock_path(ipc_sock_path, sizeof(ipc_sock_path), datadir)<0)
    return -1;

  ipc_listen_fd=socket(AF_UNIX, SOCK_STREAM, 0);
  if (ipc_listen_fd<0){
    perror("cli_ipc: socket");
    return -1;
  }

  unlink(ipc_sock_path);

  memset(&addr, 0, sizeof(addr));
  addr.sun_family=AF_UNIX;
  memcpy(addr.sun_path, ipc_sock_path, strlen(ipc_sock_path));

  if (bind(ipc_listen_fd, (struct sockaddr *)&addr,
           strlen(ipc_sock_path)+sizeof(addr.sun_family))<0){
    perror("cli_ipc: bind");
    close(ipc_listen_fd);
    ipc_listen_fd=-1;
    return -1;
  }

  if (listen(ipc_listen_fd, 5)<0){
    perror("cli_ipc: listen");
    close(ipc_listen_fd);
    unlink(ipc_sock_path);
    ipc_listen_fd=-1;
    return -1;
  }

  ipc_shutdown=0;
  psync_run_thread("cli IPC listener", ipc_accept_loop);
  return 0;
}

void cli_ipc_server_stop(void){
  ipc_shutdown=1;
  if (ipc_listen_fd>=0){
    shutdown(ipc_listen_fd, SHUT_RDWR);
    close(ipc_listen_fd);
    ipc_listen_fd=-1;
  }
  if (ipc_sock_path[0])
    unlink(ipc_sock_path);
}

/* ──────────────────── client public API ──────────────────── */

int cli_ipc_connect(const char *datadir){
  struct sockaddr_un addr;
  char path[108];
  int fd;

  if (build_sock_path(path, sizeof(path), datadir)<0)
    return -1;

  fd=socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd<0){
    perror("cli_ipc: socket");
    return -1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family=AF_UNIX;
  memcpy(addr.sun_path, path, strlen(path));

  if (connect(fd, (struct sockaddr *)&addr,
              strlen(path)+sizeof(addr.sun_family))<0){
    if (errno==ENOENT)
      fprintf(stderr, "Error: no running engine found (no socket at %s)\n", path);
    else if (errno==ECONNREFUSED)
      fprintf(stderr, "Error: engine not responding (socket at %s)\n", path);
    else
      perror("cli_ipc: connect");
    close(fd);
    return -1;
  }

  return fd;
}

int cli_ipc_transact(int fd, const char *command){
  char buf[IPC_BUFSIZE];
  int result=1;

  if (ipc_writeline(fd, command)<0){
    fprintf(stderr, "Error: failed to send command\n");
    return 1;
  }

  /* read first line: OK or ERR */
  ssize_t n=ipc_readline(fd, buf, sizeof(buf));
  if (n<0){
    fprintf(stderr, "Error: no response from engine\n");
    return 1;
  }

  if (strncmp(buf, "OK", 2)==0){
    result=0;
    /* print any payload after "OK " */
    if (n>3)
      printf("%s\n", buf+3);
  } else if (strncmp(buf, "ERR ", 4)==0){
    fprintf(stderr, "Error: %s\n", buf+4);
    result=1;
  } else {
    fprintf(stderr, "Error: unexpected response: %s\n", buf);
    result=1;
  }

  /* read remaining lines until "." terminator */
  while ((n=ipc_readline(fd, buf, sizeof(buf)))>=0){
    if (n==1 && buf[0]=='.')
      break;
    if (n>0)
      printf("%s\n", buf);
  }

  return result;
}
