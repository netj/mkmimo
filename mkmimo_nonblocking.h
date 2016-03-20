#ifndef MKMIMO_NONBLOCKING_H
#define MKMIMO_NONBLOCKING_H

#ifdef __APPLE__
#define POLLHUP_SUPPORT_UNRELIABLE
#define _DARWIN_C_SOURCE
#endif

#include "mkmimo.h"

int mkmimo_nonblocking(Inputs *inputs, Outputs *outputs);

// when POLLHUP support is unreliable, use a timeout to detect input EOFs
#ifdef POLLHUP_SUPPORT_UNRELIABLE
#define DEFAULT_POLL_TIMEOUT_MSEC 1000 /* msec */
#else
#define DEFAULT_POLL_TIMEOUT_MSEC -1 /* wait indefinitely */
#endif

// when no I/O can be done, throttle down by sleeping this much interval,
// instead of busy waiting
#define DEFAULT_THROTTLE_SLEEP_USEC 1

#endif /* MKMIMO_NONBLOCKING_H */
