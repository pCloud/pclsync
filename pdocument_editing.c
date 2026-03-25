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

#include "pdocument_editing.h"
#include "plibs.h"
#include "pcompat.h"
#include "papi.h"
#include "pnetlibs.h"
#include "plocks.h"
#include "pcallbacks.h"
#include "psettings.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

const psync_document_url_opts_t psync_document_url_default_opts = {NULL, NULL, NULL, NULL};

static psync_rwlock_t doctype_cache_rwlock;
static psync_document_extensions_t *doctype_cache = NULL;

static int ext_cmp(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Allocate a single contiguous block for extensions struct + pointer array + strings */
static psync_document_extensions_t *build_extensions_block(const char **exts, size_t count) {
  size_t i, total_str;
  psync_document_extensions_t *block;
  char *strp;

  if (!count) {
    block = (psync_document_extensions_t *)psync_malloc(sizeof(psync_document_extensions_t));
    block->count = 0;
    return block;
  }

  total_str = 0;
  for (i = 0; i < count; i++)
    total_str += strlen(exts[i]) + 1;

  block = (psync_document_extensions_t *)psync_malloc(
    sizeof(psync_document_extensions_t) + count * sizeof(const char *) + total_str);
  block->count = count;

  strp = (char *)block + sizeof(psync_document_extensions_t) + count * sizeof(const char *);
  for (i = 0; i < count; i++) {
    size_t len = strlen(exts[i]) + 1;
    memcpy(strp, exts[i], len);
    block->extensions[i] = strp;
    strp += len;
  }

  qsort((void *)block->extensions, count, sizeof(const char *), ext_cmp);
  return block;
}

static void load_cache_from_db(void) {
  psync_sql_res *res;
  psync_str_row row;
  const char **arr;
  size_t cnt, cap;
  psync_document_extensions_t *block;

  res = psync_sql_query_rdlock("SELECT extension FROM document_filetype ORDER BY extension");
  if (!res)
    return;

  cap = 32;
  cnt = 0;
  arr = (const char **)psync_malloc(cap * sizeof(const char *));

  while ((row = psync_sql_fetch_rowstr(res)) != NULL) {
    if (cnt >= cap) {
      cap *= 2;
      arr = (const char **)psync_realloc(arr, cap * sizeof(const char *));
    }
    arr[cnt++] = row[0];
  }

  if (cnt > 0) {
    block = build_extensions_block(arr, cnt);
    psync_rwlock_wrlock(&doctype_cache_rwlock);
    if (doctype_cache)
      psync_free(doctype_cache);
    doctype_cache = block;
    psync_rwlock_unlock(&doctype_cache_rwlock);
  }

  psync_free(arr);
  psync_sql_free_result(res);
}

static void save_extensions_to_db(const binresult *filetypes_hash) {
  psync_sql_res *stmt;
  uint32_t i;

  psync_sql_start_transaction();

  stmt = psync_sql_prep_statement("DELETE FROM document_filetype");
  if (stmt)
    psync_sql_run_free(stmt);

  stmt = psync_sql_prep_statement("REPLACE INTO document_filetype (extension) VALUES (?)");
  if (!stmt) {
    psync_sql_rollback_transaction();
    return;
  }
  for (i = 0; i < filetypes_hash->length; i++) {
    psync_sql_reset(stmt);
    psync_sql_bind_string(stmt, 1, filetypes_hash->hash[i].key);
    psync_sql_run(stmt);
  }
  psync_sql_free_result(stmt);
  psync_sql_commit_transaction();
}

/* Returns 1 if the two sorted extension sets differ */
static int extensions_changed(const psync_document_extensions_t *old_block,
                               const char **new_exts, size_t new_cnt) {
  size_t i;
  if (!old_block)
    return new_cnt > 0;
  if (old_block->count != new_cnt)
    return 1;
  for (i = 0; i < new_cnt; i++)
    if (strcmp(old_block->extensions[i], new_exts[i]) != 0)
      return 1;
  return 0;
}

static psync_document_extensions_t *clone_extensions_block(const psync_document_extensions_t *src) {
  size_t i, total_str;
  psync_document_extensions_t *dst;
  char *strp;

  if (!src || !src->count) {
    dst = (psync_document_extensions_t *)psync_malloc(sizeof(psync_document_extensions_t));
    dst->count = 0;
    return dst;
  }

  total_str = 0;
  for (i = 0; i < src->count; i++)
    total_str += strlen(src->extensions[i]) + 1;

  dst = (psync_document_extensions_t *)psync_malloc(
    sizeof(psync_document_extensions_t) + src->count * sizeof(const char *) + total_str);
  dst->count = src->count;

  strp = (char *)dst + sizeof(psync_document_extensions_t) + src->count * sizeof(const char *);
  for (i = 0; i < src->count; i++) {
    size_t len = strlen(src->extensions[i]) + 1;
    memcpy(strp, src->extensions[i], len);
    dst->extensions[i] = strp;
    strp += len;
  }
  return dst;
}

static int do_refresh(void) {
  binresult *bres;
  const binresult *filetypes, *res_code;
  uint64_t result;
  const char **sorted_exts;
  psync_document_extensions_t *new_block, *old_block, *evt_block;
  size_t cnt, i;
  int changed;

  if (psync_my_auth[0] == 0)
    return -1;

  {
    binparam params[] = {P_STR("auth", psync_my_auth)};
    bres = psync_api_run_command("docsrv/supportedfiletypes", params);
  }

  if (!bres) {
    debug(D_WARNING, "document editing: network error fetching supported file types");
    psync_error = PERROR_NET_ERROR;
    return -1;
  }

  res_code = psync_find_result(bres, "result", PARAM_NUM);
  result = res_code->num;

  if (result == 2266) {
    /* Feature disabled — clear cache */
    psync_sql_start_transaction();
    {
      psync_sql_res *stmt = psync_sql_prep_statement("DELETE FROM document_filetype");
      if (stmt)
        psync_sql_run_free(stmt);
    }
    psync_sql_commit_transaction();

    psync_rwlock_wrlock(&doctype_cache_rwlock);
    old_block = doctype_cache;
    doctype_cache = build_extensions_block(NULL, 0);
    psync_rwlock_unlock(&doctype_cache_rwlock);
    if (old_block)
      psync_free(old_block);

    psync_send_eventdata(PEVENT_DOCUMENT_TYPES_CHANGED, build_extensions_block(NULL, 0));
    psync_free(bres);
    return 0;
  }

  if (result != 0) {
    debug(D_WARNING, "document editing: API error %lu fetching supported file types",
          (unsigned long)result);
    psync_free(bres);
    return -1;
  }

  filetypes = psync_find_result(bres, "filetypes", PARAM_HASH);
  cnt = filetypes->length;

  /* Build sorted array of extension strings for comparison */
  sorted_exts = (const char **)psync_malloc(cnt * sizeof(const char *));
  for (i = 0; i < cnt; i++)
    sorted_exts[i] = filetypes->hash[i].key;
  qsort((void *)sorted_exts, cnt, sizeof(const char *), ext_cmp);

  psync_rwlock_rdlock(&doctype_cache_rwlock);
  changed = extensions_changed(doctype_cache, sorted_exts, cnt);
  psync_rwlock_unlock(&doctype_cache_rwlock);

  if (changed) {
    save_extensions_to_db(filetypes);
    new_block = build_extensions_block(sorted_exts, cnt);

    psync_rwlock_wrlock(&doctype_cache_rwlock);
    old_block = doctype_cache;
    doctype_cache = new_block;
    psync_rwlock_unlock(&doctype_cache_rwlock);

    if (old_block)
      psync_free(old_block);

    /* Build separate allocation for the event (event system may free it) */
    evt_block = build_extensions_block(sorted_exts, cnt);
    psync_send_eventdata(PEVENT_DOCUMENT_TYPES_CHANGED, evt_block);
  }

  psync_free(sorted_exts);
  psync_free(bres);
  return 0;
}

static void refresh_thread(void) {
  do_refresh();
}

void psync_do_document_editing_init(void) {
  psync_rwlock_init(&doctype_cache_rwlock);
  load_cache_from_db();
}

void psync_do_document_editing_start(void) {
  psync_run_thread("document editing types refresh", refresh_thread);
}

int psync_do_document_editing_is_supported_ext(const char *ext) {
  int found = 0;
  psync_rwlock_rdlock(&doctype_cache_rwlock);
  if (doctype_cache && doctype_cache->count > 0) {
    if (bsearch(&ext, doctype_cache->extensions, doctype_cache->count,
                 sizeof(const char *), ext_cmp) != NULL)
      found = 1;
  }
  psync_rwlock_unlock(&doctype_cache_rwlock);
  return found;
}

psync_document_extensions_t *psync_do_document_editing_get_supported_extensions(void) {
  psync_document_extensions_t *ret;
  psync_rwlock_rdlock(&doctype_cache_rwlock);
  ret = clone_extensions_block(doctype_cache);
  psync_rwlock_unlock(&doctype_cache_rwlock);
  return ret;
}

static uint32_t map_api_error(uint64_t code) {
  switch (code) {
    case 1029: return PERROR_INVALID_PARAMETER;
    case 1021: return PERROR_INVALID_PARAMETER;
    case 2003: return PERROR_ACCESS_DENIED;
    case 2009: return PERROR_FILE_NOT_FOUND;
    case 2075: return PERROR_NOT_BUSINESS_ACCOUNT;
    case 2266: return PERROR_FEATURE_DISABLED;
    case 5000: return PERROR_INTERNAL_SERVER_ERROR;
    default:   return PERROR_NET_ERROR;
  }
}

char *psync_do_document_editing_get_url(psync_fileid_t fileid, int mode,
                                        const psync_document_url_opts_t *opts) {
  binresult *bres;
  const binresult *link_res, *res_code;
  uint64_t result, locationid;
  const char *link;
  char *url;
  char auth[64];
  char locid_str[32];

  if (!opts)
    opts = &psync_document_url_default_opts;

  memcpy(auth, psync_my_auth, sizeof(auth));
  if (auth[0] == 0) {
    psync_error = PERROR_NET_ERROR;
    return NULL;
  }

  locationid = psync_setting_get_uint(_PS(location_id));

  {
    binparam params[] = {
      P_STR("auth", auth),
      P_NUM("fileid", fileid),
      P_NUM("os", P_OS_ID),
      P_NUM("mode", mode)
    };
    bres = psync_api_run_command("docsrv/getdocumentcode", params);
  }

  if (!bres) {
    psync_error = PERROR_NET_ERROR;
    return NULL;
  }

  res_code = psync_find_result(bres, "result", PARAM_NUM);
  result = res_code->num;

  if (result != 0) {
    psync_error = map_api_error(result);
    psync_free(bres);
    return NULL;
  }

  link_res = psync_find_result(bres, "link", PARAM_STR);
  link = link_res->str;

  /* Build URL with optional query parameters */
  {
    char *b64url = NULL;
    size_t b64len = 0;
    const char *sep = "?";

    url = psync_strcat(link, NULL);

    if (opts->lang) {
      char *tmp = psync_strcat(url, sep, "lang=", opts->lang, NULL);
      psync_free(url);
      url = tmp;
      sep = "&";
    }
    if (opts->theme) {
      char *tmp = psync_strcat(url, sep, "theme=", opts->theme, NULL);
      psync_free(url);
      url = tmp;
      sep = "&";
    }
    if (opts->gobackurl) {
      b64url = (char *)psync_base64_encode(
        (const unsigned char *)opts->gobackurl,
        strlen(opts->gobackurl), &b64len);
      if (b64url) {
        char *tmp = psync_strcat(url, sep, "gobackurl=", b64url, NULL);
        psync_free(url);
        url = tmp;
        sep = "&";
        psync_free(b64url);
      }
    }
    if (opts->forcedisplay) {
      char *tmp = psync_strcat(url, sep, "forcedisplay=", opts->forcedisplay, NULL);
      psync_free(url);
      url = tmp;
      sep = "&";
    }
    {
      char *tmp = psync_strcat(url, sep, "pcauth=", auth, NULL);
      psync_free(url);
      url = tmp;
    }
    snprintf(locid_str, sizeof(locid_str), "%lu", (unsigned long)locationid);
    {
      char *tmp = psync_strcat(url, "&locationid=", locid_str, NULL);
      psync_free(url);
      url = tmp;
    }
  }

  psync_free(bres);
  return url;
}

int psync_do_document_editing_refresh(void) {
  return do_refresh();
}
