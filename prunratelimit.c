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

#include "pcore.h"
#include "prunratelimit.h"
#include "ptimer.h"
#include "ptree.h"

/*
 * rr_node_base — fields shared by every node type stored in the rate-limit
 * trees. The intrusive tree node is the first member by convention so that
 * a per-tree leaf type can embed `rr_node_base` as its first member and
 * recovery from the tree node is performed via psync_tree_element on the
 * leaf type directly. Do NOT cast `rr_node_base *` to a leaf-type pointer.
 *
 * Protection: every rr_node_base lives in either `tasks` or `debounce_tasks`;
 * both trees and all fields below are protected by task_mutex.
 */
typedef struct {
  psync_tree tree;                       /* protected by task_mutex */
  psync_run_ratelimit_callback0 call;    /* immutable after init */
  const char *name;                      /* immutable after init */
} rr_node_base;

/*
 * psync_rr_ratelimit_node — a node in the `tasks` tree.
 *
 * Lifetime: created by psync_run_ratelimited() on first submit for a given
 *           callback; freed by psync_run_ratelimited_timer() when the
 *           cooldown elapses without a pending trailing fire.
 * Protection: all fields protected by task_mutex.
 */
typedef struct {
  rr_node_base base;
  unsigned char scheduled;               /* protected by task_mutex */
} psync_rr_ratelimit_node;

/*
 * psync_rr_debounce_node — a node in the `debounce_tasks` tree.
 *
 * Lifetime: created by psync_run_debounced() on the leading-edge submit;
 *           freed by psync_run_debounced_timer() on trailing fire or on
 *           teardown after a quiet grace window, or by
 *           psync_run_debounce_cancel() when the caller cancels (with
 *           ownership transfer to the in-flight timer callback if one is
 *           racing — see the cancel implementation comment).
 * Protection: all fields protected by task_mutex.
 *
 * `timer` is the live handle for the currently armed one-shot timer. It is
 * read by psync_run_debounce_cancel() under task_mutex and is rewritten by
 * the timer callback on every CEILING/RETICK re-arm. The callback receives
 * its own firing handle as a parameter and uses that (not node->timer) for
 * psync_timer_stop on its own way out.
 *
 * `cancelled` is the ownership-transfer flag for the cancel/callback race:
 * cancel sets it under the lock when the in-flight timer's psync_timer_stop
 * returned 1 (was running); the callback observes it on its next lock
 * acquisition and frees the node. See psync_run_debounce_cancel().
 */
typedef struct {
  rr_node_base base;
  unsigned char runinthread;             /* immutable after init */
  unsigned char pending;                  /* protected by task_mutex */
  unsigned char cancelled;                /* protected by task_mutex */
  uint32_t gracesec;                     /* immutable after init */
  uint32_t ceilingsec;                   /* immutable after init */
  time_t last_fire;                      /* protected by task_mutex */
  time_t last_submit;                    /* protected by task_mutex */
  psync_timer_t timer;                   /* protected by task_mutex */
} psync_rr_debounce_node;

/* task_mutex serializes all access to `tasks` and `debounce_tasks` and to
 * every field of every node hanging off either tree. Acquired and released
 * within this translation unit only; no nested lock acquisition. Callbacks
 * fired from inside the lock are not allowed (see psync_run_*_timer): the
 * lock is dropped before any user code runs. */
static pthread_mutex_t task_mutex=PTHREAD_MUTEX_INITIALIZER;
static psync_tree *tasks=PSYNC_TREE_EMPTY;            /* protected by task_mutex */
static psync_tree *debounce_tasks=PSYNC_TREE_EMPTY;   /* protected by task_mutex */

/* Locate the tree node whose containing rr_node_base has callback==call.
 *
 * Returns the matching psync_tree* on hit; on miss returns NULL and sets
 * *out_parent / *out_addto to the slot where a new node should be linked
 * via psync_tree_added_at(). Caller must hold task_mutex.
 *
 * The caller recovers the leaf type from the returned tree node via
 * psync_tree_element(tn, <leaf>, base.tree) — never via pointer cast. */
static psync_tree *rr_locate_in_tree_locked(psync_tree **root,
                                            psync_run_ratelimit_callback0 call,
                                            psync_tree **out_parent,
                                            psync_tree ***out_addto){
  psync_tree *tr;
  rr_node_base *base;
  tr=*root;
  if (!tr){
    *out_parent=NULL;
    *out_addto=root;
    return NULL;
  }
  while (1){
    base=psync_tree_element(tr, rr_node_base, tree);
    if (call<base->call){
      if (tr->left)
        tr=tr->left;
      else{
        *out_parent=tr;
        *out_addto=&tr->left;
        return NULL;
      }
    }
    else if (call>base->call){
      if (tr->right)
        tr=tr->right;
      else{
        *out_parent=tr;
        *out_addto=&tr->right;
        return NULL;
      }
    }
    else{
      *out_parent=tr;
      return tr;
    }
  }
}

static void psync_run_ratelimited_timer(psync_timer_t timer, void *ptr){
  psync_rr_ratelimit_node *node=(psync_rr_ratelimit_node *)ptr;
  const char *name=NULL;
  psync_run_ratelimit_callback0 call=NULL;
  int run=0;
  pthread_mutex_lock(&task_mutex);
  if (node->scheduled){
    run=1;
    node->scheduled=0;
    call=node->base.call;
    name=node->base.name;
  }
  else{
    run=0;
    psync_tree_del(&tasks, &node->base.tree);
  }
  pthread_mutex_unlock(&task_mutex);
  if (run){
    debug(D_NOTICE, "running %s in a thread", name);
    psync_run_thread(name, call);
  }
  else{
    psync_timer_stop(timer);
    psync_free(node);
  }
}

void psync_run_ratelimited(const char *name, psync_run_ratelimit_callback0 call, uint32_t minintervalsec, int runinthread){
  psync_tree *parent=NULL, **addto=NULL, *found_tn=NULL;
  psync_rr_ratelimit_node *node=NULL;
  int found=0;
  pthread_mutex_lock(&task_mutex);
  found_tn=rr_locate_in_tree_locked(&tasks, call, &parent, &addto);
  found=(found_tn!=NULL);
  if (found){
    node=psync_tree_element(found_tn, psync_rr_ratelimit_node, base.tree);
    if (node->scheduled)
      debug(D_NOTICE, "skipping run of %s as it is already scheduled", name);
    else{
      debug(D_NOTICE, "scheduling run of %s on existing timer", name);
      node->scheduled=1;
    }
  }
  else{
    node=psync_new(psync_rr_ratelimit_node);
    node->base.call=call;
    node->base.name=name;
    node->scheduled=0;
    *addto=&node->base.tree;
    psync_tree_added_at(&tasks, parent, &node->base.tree);
  }
  pthread_mutex_unlock(&task_mutex);
  if (!found){
    if (runinthread){
      debug(D_NOTICE, "running %s in a thread", name);
      psync_run_thread(name, call);
    }
    else{
      debug(D_NOTICE, "running %s on this thread", name);
      call();
    }
    psync_timer_register(psync_run_ratelimited_timer, minintervalsec, node);
  }
}

/* Disposition the timer callback selects under task_mutex and acts on
 * after the lock is released. */
typedef enum {
  RR_ACTION_RETICK,         /* re-arm timer; no fire, node retained */
  RR_ACTION_CEILING_FIRE,   /* fire on a worker; re-arm; node retained */
  RR_ACTION_TRAILING_FIRE,  /* fire on a worker; tear down */
  RR_ACTION_TEARDOWN        /* no fire; tear down */
} rr_action;

/* Compute next timer delay (in seconds) for a debounced node.
 * Caller holds task_mutex. `now` is sampled at entry; must be consistent
 * with whatever deadlines were used to justify the current call.
 *
 * `time_t` is signed and at least 32-bit; values used in this module are
 * bounded by uint32_t deltas from psync_current_time, so no over/underflow
 * is possible on supported platforms. The minimum delay is clamped to 1
 * second so the timer always makes forward progress. */
static uint32_t delay_for_locked(const psync_rr_debounce_node *node, time_t now){
  time_t to_grace, to_ceiling, d;
  to_grace=node->last_submit+(time_t)node->gracesec-now;
  to_ceiling=node->last_fire+(time_t)node->ceilingsec-now;
  d=to_grace<to_ceiling?to_grace:to_ceiling;
  if (d<1)
    d=1;
  return (uint32_t)d;
}

static void psync_run_debounced_timer(psync_timer_t timer, void *ptr){
  psync_rr_debounce_node *node=(psync_rr_debounce_node *)ptr;
  const char *name=NULL;
  psync_run_ratelimit_callback0 call=NULL;
  time_t now=0, grace_deadline=0, ceiling_deadline=0;
  uint32_t next_delay=0;
  rr_action action;

  pthread_mutex_lock(&task_mutex);
  /* Cancellation rendezvous: psync_run_debounce_cancel removed the node
   * from the tree, set cancelled=1, and is in the process of calling
   * psync_timer_stop on the live timer handle. Cancel transferred the
   * node free to us (its psync_timer_stop returned 1 because we were
   * already running). We must:
   *   1. Free the node ourselves.
   *   2. Mark this firing timer for teardown via psync_timer_stop(timer).
   *      Without this, timer_process_timers would re-arm `timer` on our
   *      return because we set no STOP_AFTER_RUN flag.
   * Calling psync_timer_stop(timer) here is safe even if cancel also
   * targeted this same handle: psync_timer_stop is idempotent under
   * PTIMER_IS_RUNNING (it only OR-sets PTIMER_STOP_AFTER_RUN), and the
   * timer_mutex serializes both calls against the timer thread's
   * post-callback free. No double-free of the timer struct results. */
  if (node->cancelled){
    pthread_mutex_unlock(&task_mutex);
    psync_timer_stop(timer);
    psync_free(node);
    return;
  }
  now=psync_current_time;
  grace_deadline=node->last_submit+(time_t)node->gracesec;
  ceiling_deadline=node->last_fire+(time_t)node->ceilingsec;

  if (!node->pending && now>=grace_deadline){
    /* End of burst, no pending submit. Tear down silently. */
    action=RR_ACTION_TEARDOWN;
    psync_tree_del(&debounce_tasks, &node->base.tree);
  }
  else if (node->pending && now>=grace_deadline){
    /* Trailing fire: burst has been quiet for gracesec. */
    action=RR_ACTION_TRAILING_FIRE;
    call=node->base.call;
    name=node->base.name;
    psync_tree_del(&debounce_tasks, &node->base.tree);
  }
  else if (node->pending && now>=ceiling_deadline){
    /* Ceiling fire: sustained burst, force a fire, stay armed. */
    action=RR_ACTION_CEILING_FIRE;
    call=node->base.call;
    name=node->base.name;
    node->last_fire=now;
    node->pending=0;
    next_delay=delay_for_locked(node, now);
    node->timer=psync_timer_register(psync_run_debounced_timer, next_delay, node);
  }
  else{
    /* Neither deadline reached; submit pushed last_submit forward between
     * our arming and our fire. Re-register with remaining delta. */
    action=RR_ACTION_RETICK;
    next_delay=delay_for_locked(node, now);
    node->timer=psync_timer_register(psync_run_debounced_timer, next_delay, node);
  }
  pthread_mutex_unlock(&task_mutex);

  switch (action){
    case RR_ACTION_TEARDOWN:
      psync_timer_stop(timer);
      psync_free(node);
      break;
    case RR_ACTION_TRAILING_FIRE:
      debug(D_NOTICE, "debounce trailing: running %s in a thread", name);
      psync_run_thread(name, call);
      psync_timer_stop(timer);
      psync_free(node);
      break;
    case RR_ACTION_CEILING_FIRE:
      debug(D_NOTICE, "debounce ceiling: running %s in a thread", name);
      psync_run_thread(name, call);
      psync_timer_stop(timer);
      break;
    case RR_ACTION_RETICK:
      psync_timer_stop(timer);
      break;
  }
}

void psync_run_debounced(const char *name, psync_run_ratelimit_callback0 call,
                         uint32_t gracesec, uint32_t ceilingsec, int runinthread){
  psync_tree *parent=NULL, **addto=NULL, *found_tn=NULL;
  psync_rr_debounce_node *node=NULL;
  uint32_t initial_delay=0;
  int fire_leading=0;
  time_t now=0;

  if (gracesec==0)
    gracesec=1;
  if (ceilingsec<gracesec)
    ceilingsec=gracesec;

  pthread_mutex_lock(&task_mutex);
  now=psync_current_time;
  found_tn=rr_locate_in_tree_locked(&debounce_tasks, call, &parent, &addto);
  if (found_tn){
    node=psync_tree_element(found_tn, psync_rr_debounce_node, base.tree);
    node->last_submit=now;
    node->pending=1;
    /* Existing timer will fire at or before its deadline, observe pending,
     * and re-arm if needed. No cancel / re-arm here. */
    debug(D_NOTICE, "debounce coalesce: %s (pending=1)", name);
  }
  else{
    node=psync_new(psync_rr_debounce_node);
    node->base.call=call;
    node->base.name=name;
    node->runinthread=runinthread?1:0;
    node->pending=0;
    node->cancelled=0;
    node->gracesec=gracesec;
    node->ceilingsec=ceilingsec;
    node->last_fire=now;
    node->last_submit=now;
    *addto=&node->base.tree;
    psync_tree_added_at(&debounce_tasks, parent, &node->base.tree);
    initial_delay=delay_for_locked(node, now);
    /* Register the initial timer under the lock so node->timer is the live
     * handle from the moment the node is visible in the tree. This closes
     * the race where psync_run_debounce_cancel observes the node before the
     * leading-edge caller has finished arming. psync_timer_register only
     * acquires timer_mutex, never task_mutex, so no nested-lock deadlock. */
    node->timer=psync_timer_register(psync_run_debounced_timer, initial_delay, node);
    fire_leading=1;
  }
  pthread_mutex_unlock(&task_mutex);

  if (fire_leading){
    /* Leading-edge fire happens after we drop the lock. If a concurrent
     * cancel removes the node before this point, the leading still runs —
     * we only touch the function-local parameters, not the node. */
    if (runinthread){
      debug(D_NOTICE, "debounce leading: running %s in a thread", name);
      psync_run_thread(name, call);
    }
    else{
      debug(D_NOTICE, "debounce leading: running %s on this thread", name);
      call();
    }
  }
}

/* Cancel any pending debounced task registered for `call`. See header for
 * caller-visible semantics.
 *
 * Race-safety design vs psync_run_debounced_timer:
 *
 *   The timer callback locks task_mutex on entry and re-reads node->timer
 *   on every CEILING/RETICK re-arm. Cancel must therefore coordinate with
 *   a possibly-in-flight callback such that:
 *     a) the node is freed exactly once, and
 *     b) the live timer struct is freed exactly once.
 *
 *   We exploit psync_timer_stop's two-valued return:
 *     - returns 0  ⇒ the timer was not running; psync_timer_stop removed
 *                    and freed the timer struct itself. The callback for
 *                    this handle will NOT run. We own the node free.
 *     - returns 1  ⇒ PTIMER_IS_RUNNING was set; psync_timer_stop only marks
 *                    PTIMER_STOP_AFTER_RUN. The timer thread will free the
 *                    timer struct when the callback returns. The callback
 *                    is or will be running, blocked on or about to acquire
 *                    task_mutex. We transfer node ownership to it via the
 *                    cancelled flag, set under the lock; the callback's
 *                    first action under the lock is to test cancelled and
 *                    drop the node if set.
 *
 *   Order of operations under the lock: locate the node, save the live
 *   timer handle, set cancelled=1, remove from the tree, then unlock
 *   before calling psync_timer_stop (which acquires timer_mutex —
 *   different from task_mutex, so calling under task_mutex would not
 *   deadlock either, but unlocking first reduces lock-hold time and
 *   matches the rest of this module's pattern of "dispose outside the
 *   lock"). Removing from the tree under the lock means a future
 *   psync_run_debounced for the same callback constructs a fresh node;
 *   the cancelled in-flight one is invisible to it. */
int psync_run_debounce_cancel(psync_run_ratelimit_callback0 call){
  psync_tree *parent=NULL, **addto=NULL, *found_tn=NULL;
  psync_rr_debounce_node *node=NULL;
  psync_timer_t t=NULL;
  int found=0;
  int stop_rc=0;

  pthread_mutex_lock(&task_mutex);
  found_tn=rr_locate_in_tree_locked(&debounce_tasks, call, &parent, &addto);
  if (found_tn){
    node=psync_tree_element(found_tn, psync_rr_debounce_node, base.tree);
    t=node->timer;
    /* Log under the lock while node->base.name is still safely reachable.
     * After we drop the lock the in-flight timer callback may free `node`. */
    debug(D_NOTICE, "debounce cancel: %s", node->base.name);
    node->cancelled=1;
    psync_tree_del(&debounce_tasks, &node->base.tree);
    found=1;
  }
  pthread_mutex_unlock(&task_mutex);

  if (found){
    /* If t is NULL the node was constructed but no timer was armed — that
     * cannot happen with the current psync_run_debounced (timer is always
     * armed under the lock before insertion is published), but guard the
     * call anyway: a future contributor might add an off-tree state. */
    if (t)
      stop_rc=psync_timer_stop(t);
    if (stop_rc==0){
      /* Timer was not running; psync_timer_stop reaped the timer struct
       * itself and the callback for `t` will not run. Cancel owns the
       * node — free now. */
      psync_free(node);
    }
    /* else: psync_timer_stop returned 1 — the timer was running, so the
     * callback either is or will be executing. It will observe cancelled=1
     * under task_mutex and free `node` itself. Do not touch `node`. */
  }
  return found;
}
