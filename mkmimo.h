#ifndef MKMIMO_H
#define MKMIMO_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include "buffer.h"

#ifdef DEBUG
#undef DEBUG
#define DEBUG(fmt, args...) fprintf(stderr, "[%d] " fmt "\n", getpid(), args)
#else
#define DEBUG(fmt, args...)
#endif

#define perrorf(fmt, args...) fprintf(stderr, fmt ": %s", args, strerror(errno))

typedef struct input {
  int fd;
  char *name;
  Buffer *buffer;
  int is_closed;
  int is_near_eof;
  int is_readable;
  int is_buffered;
} Input;

typedef struct {
  Input *inputs;
  int num_inputs;

  int last_closed;   // Index to insert next closed input
  int num_closed;    // Num already closed
  int num_readable;  // Num ready to read w/o blocking
  int num_buffered;  // Num ready for output
} Inputs;

typedef struct output {
  int fd;
  char *name;
  Buffer *buffer;

  int is_closed;
  int is_writable;
  int is_busy;
} Output;

typedef struct {
  Output *outputs;
  int num_outputs;
  int last_closed;   // Index to insert next closed output
  int next_output;   // Index of the last used output for exchange
  int num_closed;    // Num already closed
  int num_writable;  // Num ready to write w/o blocking
  int num_busy;      // Num outputs w/ non-empty buffers
} Outputs;

// a shorthand for updating both is_XYZ flag of an input/output and num_XYZ
// counts
#define SET_FLAG(items, item, flag, flag_val)  \
  do {                                         \
    if (!!(item)->is_##flag != !!(flag_val)) { \
      (item)->is_##flag = !!(flag_val);        \
      if ((flag_val))                          \
        ++(items)->num_##flag;                 \
      else                                     \
        --(items)->num_##flag;                 \
    }                                          \
  } while (0)
#define SET(item, flag, flag_val) SET_FLAG(item##s, item, flag, flag_val)

#define readIntFromEnv(envName, ConfigVar, condition, defaultValue)     \
  do {                                                                  \
    char *envValue = getenv(#envName);                                  \
    if (envValue != NULL) {                                             \
      ConfigVar = atoi(envValue);                                       \
      if (!(condition)) {                                               \
        fprintf(stderr, "%d: Invalid " #envName ", using default %d\n", \
                ConfigVar, defaultValue);                               \
        ConfigVar = defaultValue;                                       \
      } else {                                                          \
        DEBUG(#envName "=%d", ConfigVar);                               \
      }                                                                 \
    }                                                                   \
  } while (0)

#endif /* MKMIMO_H */
