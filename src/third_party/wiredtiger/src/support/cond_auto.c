/*-
 * Copyright (c) 2014-2020 MongoDB, Inc.
 * Copyright (c) 2008-2014 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

#include "wt_internal.h"

/*
 * This is an implementation of condition variables that automatically adjust the wait time
 * depending on whether the wake is resulting in useful work.
 */

/*
 * __wt_cond_auto_alloc --
 *     Allocate and initialize an automatically adjusting condition variable.
 */
int
__wt_cond_auto_alloc(
  WT_SESSION_IMPL *session, const char *name, uint64_t min, uint64_t max, WT_CONDVAR **condp)
{
    WT_CONDVAR *cond;

    WT_RET(__wt_cond_alloc(session, name, condp));
    cond = *condp;

    cond->min_wait = min;
    cond->max_wait = max;
    cond->prev_wait = min;

    return (0);
}

/*
 * __wt_cond_auto_wait_signal --
 *     Wait on a mutex, optionally timing out. If we get it before the time out period expires, let
 *     the caller know.
 */
void
__wt_cond_auto_wait_signal(WT_SESSION_IMPL *session, WT_CONDVAR *cond, bool progress,
  bool (*run_func)(WT_SESSION_IMPL *), bool *signalled)
{
    uint64_t delta;

    /*
     * Catch cases where this function is called with a condition variable that wasn't initialized
     * to do automatic adjustments.
     */
    WT_ASSERT(session, cond->min_wait != 0);

    WT_STAT_CONN_INCR(session, cond_auto_wait);
    if (progress)
        cond->prev_wait = cond->min_wait;
    else {
        delta = WT_MAX(1, (cond->max_wait - cond->min_wait) / 10);
        cond->prev_wait = WT_MIN(cond->max_wait, cond->prev_wait + delta);
    }

    __wt_cond_wait_signal(session, cond, cond->prev_wait, run_func, signalled);

    if (progress || *signalled)
        WT_STAT_CONN_INCR(session, cond_auto_wait_reset);
    if (*signalled)
        cond->prev_wait = cond->min_wait;
}

/*
 * __wt_cond_auto_wait --
 *     Wait on a mutex, optionally timing out. If we get it before the time out period expires, let
 *     the caller know.
 */
void
__wt_cond_auto_wait(
  WT_SESSION_IMPL *session, WT_CONDVAR *cond, bool progress, bool (*run_func)(WT_SESSION_IMPL *))
{
    bool notused;

    __wt_cond_auto_wait_signal(session, cond, progress, run_func, &notused);
}
