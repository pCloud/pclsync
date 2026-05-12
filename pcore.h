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

#ifndef _PSYNC_CORE_H
#define _PSYNC_CORE_H

#include "pcompiler.h"
#include "pcompat.h"
#include "psynclib.h"
#include "pstrings.h"
#include "pencoding.h"

#include <string.h>

#define D_NONE     0
#define D_BUG      10
#define D_CRITICAL 20
#define D_ERROR    30
#define D_WARNING  40
#define D_NOTICE   50

#define DEBUG_LEVELS {\
 {D_BUG, "BUG"},\
 {D_CRITICAL, "CRITICAL ERROR"},\
 {D_ERROR, "ERROR"},\
 {D_WARNING, "WARNING"},\
 {D_NOTICE, "NOTICE"}\
}

#ifndef DEBUG_LEVEL
//Set debug Level
#define DEBUG_LEVEL D_NOTICE
#endif

#define IS_DEBUG (DEBUG_LEVEL>=D_WARNING)

#ifndef P_OS_WINDOWS
#define DEBUG_FILE "/tmp/psync_err.log"
#endif

#define LOGS_FOLDER "pSync_logs"

#if defined(assert)
#undef assert
#endif

#define PSYNC_SSL_DEBUG_LEVEL 0 /* Please make sure this setting is always set to 0 for release builds !!! Possible values are in the range [0, 5] */

#define debug(level, ...) do {if (level<=DEBUG_LEVEL) psync_debug(__FILE__, __FUNCTION__, __LINE__, level, __VA_ARGS__);} while (0)
#define assert(cond) do {if (D_WARNING<=DEBUG_LEVEL && unlikely(!(cond))) { debug(D_WARNING, "assertion %s failed, aborting", TO_STR(cond)); abort();}} while (0)
#define assertw(cond) do {if (D_WARNING<=DEBUG_LEVEL && unlikely(!(cond))) { debug(D_WARNING, "assertion %s failed", TO_STR(cond));}} while (0)
#define debug_execute(level, expr) do {if (level<=DEBUG_LEVEL) (expr);} while (0)

#define PSYNC_TNUMBER 1
#define PSYNC_TSTRING 2
#define PSYNC_TREAL   3
#define PSYNC_TNULL   4
#define PSYNC_TBOOL   5

#define psync_is_null(v) ((v).type==PSYNC_TNULL)
#define psync_get_number(v) (likely((v).type==PSYNC_TNUMBER)?(v).num:psync_err_number_expected(__FILE__, __FUNCTION__, __LINE__, &(v)))
#define psync_get_snumber(v) (likely((v).type==PSYNC_TNUMBER)?(int64_t)((v).num):(int64_t)psync_err_number_expected(__FILE__, __FUNCTION__, __LINE__, &(v)))
#define psync_get_number_or_null(v) (((v).type==PSYNC_TNUMBER)?(v).num:(likely((v).type==PSYNC_TNULL)?0:psync_err_number_expected(__FILE__, __FUNCTION__, __LINE__, &(v))))
#define psync_get_snumber_or_null(v) (((v).type==PSYNC_TNUMBER)?(int64_t)(v).num:(likely((v).type==PSYNC_TNULL)?0:(int64_t)psync_err_number_expected(__FILE__, __FUNCTION__, __LINE__, &(v))))
#define psync_get_string(v) (likely((v).type==PSYNC_TSTRING)?(v).str:psync_err_string_expected(__FILE__, __FUNCTION__, __LINE__, &(v)))
#define psync_get_string_or_null(v) (((v).type==PSYNC_TSTRING)?(v).str:(likely((v).type==PSYNC_TNULL)?NULL:psync_err_string_expected(__FILE__, __FUNCTION__, __LINE__, &(v))))
#define psync_dup_string(v) (likely((v).type==PSYNC_TSTRING)?psync_strndup((v).str, (v).length):psync_strdup(psync_err_string_expected(__FILE__, __FUNCTION__, __LINE__, &(v))))
#define psync_get_lstring(v, l) psync_lstring_expected(__FILE__, __FUNCTION__, __LINE__, &(v), l)
#define psync_get_lstring_or_null(v, l) ((v).type==PSYNC_TNULL?NULL:psync_lstring_expected(__FILE__, __FUNCTION__, __LINE__, &(v), l))
#define psync_get_real(v) (likely((v).type==PSYNC_TREAL)?(v).real:psync_err_real_expected(__FILE__, __FUNCTION__, __LINE__, &(v)))

#if D_WARNING<=DEBUG_LEVEL
#define likely_log(x) (likely(x)?1:psync_debug(__FILE__, __FUNCTION__, __LINE__, D_WARNING, "assertion likely_log(%s) failed", TO_STR(x))*0)
#define unlikely_log(x) (unlikely(x)?psync_debug(__FILE__, __FUNCTION__, __LINE__, D_WARNING, "assertion unlikely_log(%s) failed", TO_STR(x)):0)
#if defined(PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP)
#undef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
#endif
#define pthread_mutex_lock(mutex) \
  do {\
    int __mutex_result=pthread_mutex_lock(mutex);\
    if (unlikely(__mutex_result)){\
      debug(D_CRITICAL, "pthread_mutex_lock returned %d", __mutex_result);\
      abort();\
    }\
  } while (0)
#define pthread_mutex_unlock(mutex) \
  do {\
    int __mutex_result=pthread_mutex_unlock(mutex);\
    if (unlikely(__mutex_result)){\
      debug(D_CRITICAL, "pthread_mutex_unlock returned %d", __mutex_result);\
      abort();\
    }\
  } while (0)
#define PRINT_RETURN(x) ((x)*psync_debug(__FILE__, __FUNCTION__, __LINE__, D_NOTICE, "returning %d", (int)(x)))
#define PRINT_RETURN_CONST(x) ((x)*psync_debug(__FILE__, __FUNCTION__, __LINE__, D_NOTICE, "returning " #x))
#define PRINT_RETURN_FORMAT(x, format, ...) ((x)*psync_debug(__FILE__, __FUNCTION__, __LINE__, D_NOTICE, "returning %d" format, (int)(x), __VA_ARGS__))
#define PRINT_NEG_RETURN(x) ((x<0)?(x)*psync_debug(__FILE__, __FUNCTION__, __LINE__, D_WARNING, "returning %d", (int)(x)):(x))
#define PRINT_NEG_RETURN_FORMAT(x, format, ...) ((x<0)?(x)*psync_debug(__FILE__, __FUNCTION__, __LINE__, D_WARNING, "returning %d "  format, (int)(x), __VA_ARGS__):(x))
#else
#define likely_log likely
#define unlikely_log unlikely
#define PRINT_RETURN(x) (x)
#define PRINT_RETURN_CONST(x) (x)
#define PRINT_RETURN_FORMAT(x, format, ...) (x)
#define PRINT_NEG_RETURN(x) (x)
#define PRINT_NEG_RETURN_FORMAT(x, format, ...) (x)
#endif

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))

#include "pmem.h"

#define psync_get_result_cell(res, row, col) (res)->data[(row)*(res)->cols+(col)]

typedef struct {
  uint32_t type;
  uint32_t length;
  union {
    uint64_t num;
    int64_t snum;
    const char *str;
    double real;
  };
} psync_variant;

typedef struct {
  uint32_t rows;
  uint32_t cols;
  uint64_t data[];
} psync_full_result_int;

struct psync_list_builder_t_;

typedef struct psync_list_builder_t_ psync_list_builder_t;

struct psync_task_manager_t_;

typedef struct psync_task_manager_t_* psync_task_manager_t;

typedef const uint64_t* psync_uint_row;
typedef const char* const* psync_str_row;
typedef const psync_variant* psync_variant_row;

typedef void (*psync_run_after_t)(void *);
typedef int (*psync_list_builder_sql_callback)(psync_list_builder_t *, void *, psync_variant_row);

typedef void (*psync_task_callback_t)(void *, void *);


typedef void (*psync_transaction_callback_t)(void *);

extern int psync_do_run;
extern int psync_diff_run;
extern int psync_diff_waiting;

extern int psync_recache_contacts;
extern pstatus_t psync_status;
extern char psync_my_auth[64], psync_my_2fa_code[32], *psync_my_2fa_token, *psync_my_verify_token;
extern int psync_my_2fa_code_type, psync_my_2fa_trust, psync_my_2fa_has_devices, psync_my_2fa_type;
extern uint64_t psync_my_userid;
extern pthread_mutex_t psync_my_auth_mutex;
extern PSYNC_THREAD uint32_t psync_error;

int psync_rename_conflicted_file(const char *path);

void psync_libs_init();
void psync_run_after_sec(psync_run_after_t run, void *ptr, uint32_t seconds);
void psync_free_after_sec(void *ptr, uint32_t seconds);

psync_list_builder_t *psync_list_builder_create(size_t element_size, size_t offset);
void *psync_list_bulder_add_element(psync_list_builder_t *builder);
void psync_list_add_string_offset(psync_list_builder_t *builder, size_t offset);
void psync_list_add_lstring_offset(psync_list_builder_t *builder, size_t offset, size_t length);
void *psync_list_builder_finalize(psync_list_builder_t *builder);

psync_task_manager_t psync_task_run_tasks(psync_task_callback_t const *callbacks, void *const *params, int cnt);
void *psync_task_get_result(psync_task_manager_t tm, int id);
void psync_task_free(psync_task_manager_t tm);
int psync_task_complete(void *h, void *data);

void psync_pqsort(void *base, size_t cnt, size_t sort_first, size_t size, int (*compar)(const void *, const void *));
void psync_qpartition(void *base, size_t cnt, size_t sort_first, size_t size, int (*compar)(const void *, const void *));

void psync_try_free_memory();

int psync_debug(const char *file, const char *function, int unsigned line, int unsigned level, const char *fmt, ...) PSYNC_COLD PSYNC_FORMAT(printf, 5, 6)  PSYNC_NONNULL(5);

uint64_t psync_err_number_expected(const char *file, const char *function, int unsigned line, const psync_variant *v) PSYNC_COLD;
const char *psync_err_string_expected(const char *file, const char *function, int unsigned line, const psync_variant *v) PSYNC_COLD;
const char *psync_lstring_expected(const char *file, const char *function, int unsigned line, const psync_variant *v, size_t *len) PSYNC_NONNULL(4, 5);
double psync_err_real_expected(const char *file, const char *function, int unsigned line, const psync_variant *v) PSYNC_COLD;

static inline size_t psync_strlcpy(char *dst, const char *src, size_t size){
  size_t len;
  len=strlen(src);
  if (likely_log(len<size)){
    memcpy(dst, src, len+1);
    return len;
  }
  else if (likely_log(size)){
    memcpy(dst, src, size-1);
    dst[size-1]=0;
    return size-1;
  }
  else
    return 0;
}

#define LOST_AND_FOUND_FNAME "pCloud_lost_and_found"

extern psync_folderid_t lost_and_found_fid;
#endif
