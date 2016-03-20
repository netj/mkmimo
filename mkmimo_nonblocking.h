#ifndef MKMIMO_NONBLOCKING_H
#define MKMIMO_NONBLOCKING_H

#ifdef __APPLE__
#define POLLHUP_SUPPORT_UNRELIABLE
#define _DARWIN_C_SOURCE
#endif

#include "mkmimo.h"

int mkmimo_nonblocking(Inputs *inputs, Outputs *outputs);

#endif /* MKMIMO_NONBLOCKING_H */
