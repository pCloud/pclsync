#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "psynclib.h"
#include "psettings.h"
#include "gitcommit.h"
#include "cli_ipc.h"

static volatile sig_atomic_t shutdown_flag=0;

static void signal_handler(int sig){
  (void)sig;
  shutdown_flag=1;
}

static void print_version(void){
  printf("pCloud CLI v%s (commit %.8s)\n", PSYNC_LIB_VERSION, GIT_PREV_COMMIT_ID);
}

/* ──────────────────── help texts ──────────────────── */

static void print_help_toplevel(void){
  printf("Usage: cli <command> [options]\n"
         "\n"
         "Commands:\n"
         "  start            Start the sync engine and FUSE mount\n"
         "  stop             Stop a running engine instance\n"
         "  status           Show engine status and statistics\n"
         "  sync status      Show engine state and sync folder pairs\n"
         "  sync add         Add a sync folder pair\n"
         "  sync list        List all sync folder pairs\n"
         "  sync delete      Remove a sync folder pair\n"
         "  crypto status    Show crypto setup, lock state, and folders\n"
         "  crypto list      Print encrypted folder paths (one per line)\n"
         "  crypto unlock    Unlock the crypto folder with a password\n"
         "  crypto lock      Lock the crypto folder\n"
         "\n"
         "Run 'cli <command> --help' for command-specific options.\n");
}

static void print_help_start(void){
  printf("Usage: cli start [OPTIONS]\n"
         "\n"
         "Start the sync engine, FUSE mount, and IPC listener.\n"
         "\n"
         "Options:\n"
         "  -a, --auth TOKEN         Authentication token\n"
         "  -s, --apiserver HOST     API server hostname\n"
         "  -l, --locationid ID      Account location ID (unsigned integer)\n"
         "  -m, --mountpoint PATH    FUSE mount directory\n"
         "  -d, --datadir PATH       Data directory path\n"
         "  -h, --help               Show this help message\n"
         "\n"
         "Note: --auth, --apiserver, and --locationid must be provided together.\n");
}

static void print_help_sync(void){
  printf("Usage: cli sync <subcommand> [options]\n"
         "\n"
         "Subcommands:\n"
         "  status   Show engine state and all sync folder pairs\n"
         "  add      Add a sync folder pair\n"
         "  list     List all sync folder pairs\n"
         "  delete   Remove a sync folder pair\n"
         "\n"
         "Run 'cli sync <subcommand> --help' for details.\n");
}

static void print_help_sync_status(void){
  printf("Usage: cli sync status [OPTIONS]\n"
         "\n"
         "Show the engine sync state and all configured sync folder pairs.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "      --just-state     Print only the engine state (for scripting)\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_sync_add(void){
  printf("Usage: cli sync add [OPTIONS]\n"
         "\n"
         "Add a sync folder pair to the running engine.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -l, --local PATH     Local folder path (required)\n"
         "  -r, --remote PATH    Remote folder path (required)\n"
         "  -t, --type TYPE      Sync type: full (default), download, upload\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_sync_list(void){
  printf("Usage: cli sync list [OPTIONS]\n"
         "\n"
         "List all sync folder pairs from the running engine.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_sync_delete(void){
  printf("Usage: cli sync delete [OPTIONS]\n"
         "\n"
         "Remove a sync folder pair from the running engine.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -i, --id ID          Sync ID to delete (required)\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_status(void){
  printf("Usage: cli status [OPTIONS]\n"
         "\n"
         "Show whether the engine is running and display basic statistics.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_stop(void){
  printf("Usage: cli stop [OPTIONS]\n"
         "\n"
         "Gracefully stop a running engine instance.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_crypto(void){
  printf("Usage: cli crypto <subcommand> [options]\n"
         "\n"
         "Subcommands:\n"
         "  status   Show crypto setup, lock state, subscription, and encrypted folders\n"
         "  list     Print encrypted folder paths (one per line, for scripting)\n"
         "  unlock   Unlock the crypto folder with a password\n"
         "  lock     Lock the crypto folder\n"
         "\n"
         "Run 'cli crypto <subcommand> --help' for details.\n");
}

static void print_help_crypto_status(void){
  printf("Usage: cli crypto status [OPTIONS]\n"
         "\n"
         "Show crypto setup state, lock state, subscription status, and encrypted folder paths.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH       Data directory of running engine (required)\n"
         "      --just-unlock-state  Print only the unlock state: unlocked, locked, or not setup\n"
         "  -h, --help               Show this help message\n");
}

static void print_help_crypto_list(void){
  printf("Usage: cli crypto list [OPTIONS]\n"
         "\n"
         "Print the remote path of each encrypted folder, one per line.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_crypto_unlock(void){
  printf("Usage: cli crypto unlock [OPTIONS]\n"
         "\n"
         "Unlock the crypto folder. The password is read from the terminal without echo.\n"
         "Set PCLOUD_CRYPTO_SECRET to provide the password non-interactively.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -h, --help           Show this help message\n");
}

static void print_help_crypto_lock(void){
  printf("Usage: cli crypto lock [OPTIONS]\n"
         "\n"
         "Lock the crypto folder.\n"
         "\n"
         "Options:\n"
         "  -d, --datadir PATH   Data directory of running engine (required)\n"
         "  -h, --help           Show this help message\n");
}

/* ──────────────────── sync type helpers ──────────────────── */

static int parse_sync_type(const char *str){
  if (strcmp(str, "full")==0)     return PSYNC_FULL;
  if (strcmp(str, "download")==0) return PSYNC_DOWNLOAD_ONLY;
  if (strcmp(str, "upload")==0)   return PSYNC_UPLOAD_ONLY;
  return -1;
}

/* ──────────────────── cmd_start ──────────────────── */

static int cmd_start(int argc, char *argv[]){
  const char *auth=NULL;
  const char *apiserver=NULL;
  const char *mountpoint=NULL;
  const char *datadir=NULL;
  uint32_t locationid=0;
  int have_locationid=0;
  int opt;
  struct sigaction sa;

  static struct option long_options[]={
    {"auth",       required_argument, NULL, 'a'},
    {"apiserver",  required_argument, NULL, 's'},
    {"locationid", required_argument, NULL, 'l'},
    {"mountpoint", required_argument, NULL, 'm'},
    {"datadir",    required_argument, NULL, 'd'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,         0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "a:s:l:m:d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'a':
        auth=optarg;
        break;
      case 's':
        apiserver=optarg;
        break;
      case 'l': {
        char *endptr;
        unsigned long val=strtoul(optarg, &endptr, 10);
        if (*endptr!='\0' || endptr==optarg){
          fprintf(stderr, "Error: --locationid requires a valid unsigned integer\n\n");
          print_help_start();
          return 1;
        }
        locationid=(uint32_t)val;
        have_locationid=1;
        break;
      }
      case 'm':
        mountpoint=optarg;
        break;
      case 'd':
        datadir=optarg;
        break;
      case 'h':
        print_help_start();
        return 0;
      default:
        print_help_start();
        return 1;
    }
  }

  /* Validate: auth, apiserver, locationid must all be provided together or none */
  {
    int auth_count=(auth!=NULL)+(apiserver!=NULL)+have_locationid;
    if (auth_count!=0 && auth_count!=3){
      fprintf(stderr, "Error: --auth, --apiserver, and --locationid must be provided together.\n\n");
      print_help_start();
      return 1;
    }
  }

  print_version();

  if (datadir)
    psync_set_data_directory(datadir);

  psync_init();

  if (mountpoint)
    psync_setting_set_string(_PS(fsroot), mountpoint);

  if (apiserver)
    psync_set_apiserver(apiserver, locationid);

  psync_start_sync(NULL, NULL);

  if (auth)
    psync_set_auth(auth, 0);

  psync_fs_start();

  /* Start IPC server (uses datadir or default path) */
  if (datadir){
    if (cli_ipc_server_start(datadir)<0)
      fprintf(stderr, "Warning: failed to start IPC server\n");
  } else {
    char *default_db=psync_get_default_database_path();
    if (default_db){
      /* extract directory from database file path */
      char *slash=strrchr(default_db, '/');
      if (slash){
        *slash='\0';
        if (cli_ipc_server_start(default_db)<0)
          fprintf(stderr, "Warning: failed to start IPC server\n");
      }
      free(default_db);
    }
  }

  /* Install signal handlers */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler=signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags=0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  /* Wait for shutdown signal */
  while (!shutdown_flag)
    pause();

  printf("Shutting down...\n");
  cli_ipc_server_stop();
  psync_fs_stop();
  psync_destroy();
  return 0;
}

/* ──────────────────── cmd_stop ──────────────────── */

static int cmd_stop(int argc, char *argv[]){
  const char *datadir=NULL;
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'h':
        print_help_stop();
        return 0;
      default:
        print_help_stop();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_stop();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0)
    return 1;

  int ret=cli_ipc_transact(fd, "SHUTDOWN");
  close(fd);
  return ret;
}

/* ──────────────────── cmd_status ──────────────────── */

static int cmd_status(int argc, char *argv[]){
  const char *datadir=NULL;
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'h':
        print_help_status();
        return 0;
      default:
        print_help_status();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_status();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0){
    printf("Engine: not running\n");
    return 1;
  }

  int ret=cli_ipc_transact(fd, "STATUS");
  close(fd);
  return ret;
}

/* ──────────────────── cmd_crypto_status ──────────────────── */

static int cmd_crypto_status(int argc, char *argv[]){
  const char *datadir=NULL;
  int just_unlock_state=0;
  int opt;

  static struct option long_options[]={
    {"datadir",          required_argument, NULL, 'd'},
    {"just-unlock-state",no_argument,       NULL, 'u'},
    {"help",             no_argument,       NULL, 'h'},
    {NULL,               0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg;    break;
      case 'u': just_unlock_state=1; break;
      case 'h':
        print_help_crypto_status();
        return 0;
      default:
        print_help_crypto_status();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_crypto_status();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0){
    printf("Engine: not running\n");
    return 1;
  }

  int ret=cli_ipc_transact(fd, just_unlock_state?"CRYPTO_UNLOCK_STATE":"CRYPTO_STATUS");
  close(fd);
  return ret;
}

/* ──────────────────── cmd_crypto_list ──────────────────── */

static int cmd_crypto_list(int argc, char *argv[]){
  const char *datadir=NULL;
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'h':
        print_help_crypto_list();
        return 0;
      default:
        print_help_crypto_list();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_crypto_list();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0){
    printf("Engine: not running\n");
    return 1;
  }

  int ret=cli_ipc_transact(fd, "CRYPTO_LIST");
  close(fd);
  return ret;
}

/* ──────────────────── cmd_crypto_unlock ──────────────────── */

static int cmd_crypto_unlock(int argc, char *argv[]){
  const char *datadir=NULL;
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'h':
        print_help_crypto_unlock();
        return 0;
      default:
        print_help_crypto_unlock();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_crypto_unlock();
    return 1;
  }

  const char *secret=getenv("PCLOUD_CRYPTO_SECRET");
  if (!secret){
    secret=getpass("Crypto password: ");
    if (!secret){
      fprintf(stderr, "Error: failed to read password\n");
      return 1;
    }
  }

  char cmd[4096+32];
  snprintf(cmd, sizeof(cmd), "CRYPTO_UNLOCK %s", secret);

  int fd=cli_ipc_connect(datadir);
  if (fd<0){
    printf("Engine: not running\n");
    return 1;
  }

  int ret=cli_ipc_transact(fd, cmd);
  close(fd);
  return ret;
}

/* ──────────────────── cmd_crypto_lock ──────────────────── */

static int cmd_crypto_lock(int argc, char *argv[]){
  const char *datadir=NULL;
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'h':
        print_help_crypto_lock();
        return 0;
      default:
        print_help_crypto_lock();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_crypto_lock();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0){
    printf("Engine: not running\n");
    return 1;
  }

  int ret=cli_ipc_transact(fd, "CRYPTO_LOCK");
  close(fd);
  return ret;
}

/* ──────────────────── cmd_sync_add ──────────────────── */

static int cmd_sync_add(int argc, char *argv[]){
  const char *datadir=NULL;
  const char *localpath=NULL;
  const char *remotepath=NULL;
  const char *type_str="full";
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"local",   required_argument, NULL, 'l'},
    {"remote",  required_argument, NULL, 'r'},
    {"type",    required_argument, NULL, 't'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:l:r:t:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg;    break;
      case 'l': localpath=optarg;  break;
      case 'r': remotepath=optarg; break;
      case 't': type_str=optarg;   break;
      case 'h':
        print_help_sync_add();
        return 0;
      default:
        print_help_sync_add();
        return 1;
    }
  }

  if (!datadir || !localpath || !remotepath){
    fprintf(stderr, "Error: --datadir, --local, and --remote are required\n\n");
    print_help_sync_add();
    return 1;
  }

  int synctype=parse_sync_type(type_str);
  if (synctype<0){
    fprintf(stderr, "Error: invalid sync type '%s' (use full, download, or upload)\n", type_str);
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0)
    return 1;

  char cmd[4096];
  snprintf(cmd, sizeof(cmd), "SYNC_ADD %d %s\t%s", synctype, localpath, remotepath);
  int ret=cli_ipc_transact(fd, cmd);
  close(fd);
  return ret;
}

/* ──────────────────── cmd_sync_status ──────────────────── */

static int cmd_sync_status(int argc, char *argv[]){
  const char *datadir=NULL;
  int just_state=0;
  int opt;

  static struct option long_options[]={
    {"datadir",    required_argument, NULL, 'd'},
    {"just-state", no_argument,       NULL, 'j'},
    {"help",       no_argument,       NULL, 'h'},
    {NULL,         0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'j': just_state=1;   break;
      case 'h':
        print_help_sync_status();
        return 0;
      default:
        print_help_sync_status();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_sync_status();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0){
    printf("Engine: not running\n");
    return 1;
  }

  int ret=cli_ipc_transact(fd, just_state?"SYNC_STATE":"SYNC_STATUS");
  close(fd);
  return ret;
}

/* ──────────────────── cmd_sync_list ──────────────────── */

static int cmd_sync_list(int argc, char *argv[]){
  const char *datadir=NULL;
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'h':
        print_help_sync_list();
        return 0;
      default:
        print_help_sync_list();
        return 1;
    }
  }

  if (!datadir){
    fprintf(stderr, "Error: --datadir is required\n\n");
    print_help_sync_list();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0)
    return 1;

  int ret=cli_ipc_transact(fd, "SYNC_LIST");
  close(fd);
  return ret;
}

/* ──────────────────── cmd_sync_delete ──────────────────── */

static int cmd_sync_delete(int argc, char *argv[]){
  const char *datadir=NULL;
  const char *idstr=NULL;
  int opt;

  static struct option long_options[]={
    {"datadir", required_argument, NULL, 'd'},
    {"id",      required_argument, NULL, 'i'},
    {"help",    no_argument,       NULL, 'h'},
    {NULL,      0,                 NULL,  0 }
  };

  optind=1;
  while ((opt=getopt_long(argc, argv, "d:i:h", long_options, NULL))!=-1){
    switch (opt){
      case 'd': datadir=optarg; break;
      case 'i': idstr=optarg;   break;
      case 'h':
        print_help_sync_delete();
        return 0;
      default:
        print_help_sync_delete();
        return 1;
    }
  }

  if (!datadir || !idstr){
    fprintf(stderr, "Error: --datadir and --id are required\n\n");
    print_help_sync_delete();
    return 1;
  }

  int fd=cli_ipc_connect(datadir);
  if (fd<0)
    return 1;

  char cmd[256];
  snprintf(cmd, sizeof(cmd), "SYNC_DELETE %s", idstr);
  int ret=cli_ipc_transact(fd, cmd);
  close(fd);
  return ret;
}

/* ──────────────────── main ──────────────────── */

int main(int argc, char *argv[]){
  if (argc<2){
    print_help_toplevel();
    return 1;
  }

  if (strcmp(argv[1], "--help")==0 || strcmp(argv[1], "-h")==0){
    print_help_toplevel();
    return 0;
  }

  if (strcmp(argv[1], "--version")==0 || strcmp(argv[1], "-v")==0){
    print_version();
    return 0;
  }

  if (strcmp(argv[1], "start")==0)
    return cmd_start(argc-1, argv+1);

  if (strcmp(argv[1], "stop")==0)
    return cmd_stop(argc-1, argv+1);

  if (strcmp(argv[1], "status")==0)
    return cmd_status(argc-1, argv+1);

  if (strcmp(argv[1], "sync")==0){
    if (argc<3){
      print_help_sync();
      return 1;
    }
    if (strcmp(argv[2], "status")==0)
      return cmd_sync_status(argc-2, argv+2);
    if (strcmp(argv[2], "add")==0)
      return cmd_sync_add(argc-2, argv+2);
    if (strcmp(argv[2], "list")==0)
      return cmd_sync_list(argc-2, argv+2);
    if (strcmp(argv[2], "delete")==0)
      return cmd_sync_delete(argc-2, argv+2);
    if (strcmp(argv[2], "--help")==0 || strcmp(argv[2], "-h")==0){
      print_help_sync();
      return 0;
    }
    fprintf(stderr, "Unknown sync subcommand: %s\n\n", argv[2]);
    print_help_sync();
    return 1;
  }

  if (strcmp(argv[1], "crypto")==0){
    if (argc<3){
      print_help_crypto();
      return 1;
    }
    if (strcmp(argv[2], "status")==0)
      return cmd_crypto_status(argc-2, argv+2);
    if (strcmp(argv[2], "list")==0)
      return cmd_crypto_list(argc-2, argv+2);
    if (strcmp(argv[2], "unlock")==0)
      return cmd_crypto_unlock(argc-2, argv+2);
    if (strcmp(argv[2], "lock")==0)
      return cmd_crypto_lock(argc-2, argv+2);
    if (strcmp(argv[2], "--help")==0 || strcmp(argv[2], "-h")==0){
      print_help_crypto();
      return 0;
    }
    fprintf(stderr, "Unknown crypto subcommand: %s\n\n", argv[2]);
    print_help_crypto();
    return 1;
  }

  fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
  print_help_toplevel();
  return 1;
}
