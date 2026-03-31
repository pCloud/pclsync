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

#ifndef _PSYNC_DOCUMENT_EDITING_H
#define _PSYNC_DOCUMENT_EDITING_H

#include "psynclib.h"

/* Initialize document editing subsystem: rwlock + DB cache load.
 * No network needed. Called from psync_init(). */
void psync_do_document_editing_init(void);

/* Start the background refresh thread. Requires auth.
 * Called from psync_start_sync(). */
void psync_do_document_editing_start(void);

/* Stop document editing: clear cache, transition to idle.
 * Reversible — _start() may be called again.
 * Returns 0 on success, -1 if not in RUNNING state. */
int psync_do_document_editing_stop(void);

/* Destroy document editing: clear cache, terminal state.
 * Returns 0 on success, -1 if already destroyed. */
int psync_do_document_editing_destroy(void);

/* Returns 1 if `ext` (lowercase, no leading dot) is a supported document type.
 * Thread-safe; reads from the in-memory cache. */
int psync_do_document_editing_is_supported_ext(const char *ext);

/* Returns a snapshot of supported extensions. Caller owns the returned
 * block and must release it with psync_free(). Always returns a valid
 * pointer (never NULL); count == 0 when list is empty or not yet loaded. */
psync_document_extensions_t *psync_do_document_editing_get_supported_extensions(void);

/* Build an editor URL for the given file. Returns psync_malloc()-allocated
 * string on success, NULL on failure (psync_error set).
 * SECURITY: URL contains the auth token — treat as credential. */
char *psync_do_document_editing_get_url(psync_fileid_t fileid, int mode,
                                        const psync_document_url_opts_t *opts);

/* Force a server refresh of the extensions list.
 * Returns 0 on success, -1 on error (psync_error set). */
int psync_do_document_editing_refresh(void);

#endif
