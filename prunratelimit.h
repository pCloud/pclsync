/* Copyright (c) 2015 Anton Titov.
 * Copyright (c) 2015 pCloud Ltd.
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

#ifndef _PSYNC_RUNRATELIMIT_H
#define _PSYNC_RUNRATELIMIT_H

#include <stdint.h>

typedef void (*psync_run_ratelimit_callback0)();

/* Leading-edge throttle with trailing coalescing.
 *
 * Semantics per unique `call` pointer:
 *   - Leading edge: the first submit after a quiet period fires `call`
 *     immediately. If runinthread is zero, `call` runs on the caller's
 *     thread; otherwise it is dispatched to a worker via psync_run_thread.
 *   - Cooldown: a timer is armed for `minintervalsec` seconds. Submits
 *     that arrive during the cooldown are coalesced — they do NOT fire
 *     `call` and do NOT extend or reset the cooldown. At most one such
 *     pending submit is remembered via an internal flag.
 *   - Trailing edge: when the cooldown timer expires, if at least one
 *     submit arrived during the window, `call` runs once (on a worker
 *     thread) and a fresh cooldown begins — the timer does not re-arm
 *     itself; the next submit will find the node and set the flag again.
 *     If no submit arrived during the window, the node is torn down and
 *     the next submit starts cold with a new leading-edge fire.
 *
 * The net effect is "at most one execution per `minintervalsec` window,
 * but the last submit in a burst is always delivered." Use this for rate
 * limiting where reacting quickly to the first event matters more than
 * reacting quickly to the last one in a burst. For the opposite tradeoff,
 * see psync_run_debounced().
 *
 * A given `call` pointer must NOT be registered via both this function
 * and psync_run_debounced().
 *
 * Notes:
 *   - `call` must not recursively invoke psync_run_ratelimited() with
 *     the same callback pointer from within its own body: the tree node
 *     is not yet visible at the time the leading-edge call runs.
 */
void psync_run_ratelimited(const char *name, psync_run_ratelimit_callback0 call, uint32_t minintervalsec, int runinthread);

/* Hybrid leading + trailing debounce with a max-delay ceiling.
 *
 * Semantics per unique `call` pointer:
 *   - Leading edge: the first submit after a quiet period fires `call`
 *     immediately. If runinthread is zero, `call` runs on the caller's
 *     thread; otherwise it is dispatched to a worker via psync_run_thread.
 *   - Subsequent submits within the grace window do NOT re-fire; they
 *     extend the grace period up to the hard ceiling.
 *   - Trailing edge: when the stream has been quiet for `gracesec`, fire
 *     `call` once more (always on a worker thread) and tear down state.
 *   - Ceiling: if the stream never goes quiet, fire `call` every
 *     `ceilingsec` seconds to prevent starvation.
 *
 * Constraints: `gracesec >= 1`, `ceilingsec >= gracesec`. Values outside
 * these bounds are coerced.
 *
 * A given `call` pointer must NOT be registered via both this function
 * and psync_run_ratelimited().
 */
void psync_run_debounced(const char *name, psync_run_ratelimit_callback0 call,
                         uint32_t gracesec, uint32_t ceilingsec, int runinthread);

/* Cancel any pending debounced task registered for `call`.
 *
 * If a matching node exists in the debounce registry, stops its currently
 * armed timer, removes the node from the registry, and frees it. Drops any
 * `pending` trailing-fire — the cancelled task does NOT fire on the way out.
 * Callers wanting flush-then-cancel must invoke `call` themselves first.
 *
 * If a leading-edge fire is concurrently in progress on another thread (the
 * caller of psync_run_debounced has not yet returned from the user callback),
 * cancellation cannot rescind it; only the trailing fire and any future
 * timer-driven re-fire are suppressed.
 *
 * Returns 1 if a task was found and cancelled, 0 if nothing matched.
 *
 * Safe to call from any thread and at any time, including:
 *   - when no task for `call` is currently registered (returns 0),
 *   - from inside a callback previously dispatched by psync_run_debounced()
 *     or psync_run_ratelimited(),
 *   - concurrently with psync_run_debounced() submits for the same or a
 *     different `call` from other threads.
 *
 * Has no effect on the older psync_run_ratelimited() registry. */
int psync_run_debounce_cancel(psync_run_ratelimit_callback0 call);

#endif
