/* Test shim for ptimer.h */
#ifndef _PSYNC_TIMER_H
#define _PSYNC_TIMER_H

#include <time.h>

static inline time_t psync_timer_time(void) {
  return time(NULL);
}

#endif
