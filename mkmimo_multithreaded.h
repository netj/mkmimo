#ifndef MKMIMO_MULTITHREADED_H
#define MKMIMO_MULTITHREADED_H

#include "mkmimo.h"

int mkmimo_multithreaded(Inputs *inputs, Outputs *outputs);

#define DEFAULT_MULTIBUFFERING 2  // use double buffering by default

#endif /* MKMIMO_MULTITHREADED_H */
