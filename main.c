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
#include "mkmimo.h"
#include "mkmimo_nonblocking.h"
#include "mkmimo_multithreaded.h"

/* Declared externally in mkmimo.h */
int BLOCKSIZE = DEFAULT_BLOCKSIZE;

static char NAME_FOR_STDIN[] = "/dev/stdin";
static char NAME_FOR_STDOUT[] = "/dev/stdout";

static inline void clean_up(Inputs *inputs, Outputs *outputs) {
  for (int i = 0; i < inputs->num_inputs; i++) {
    close(inputs->inputs[i].fd);
    inputs->inputs[i].is_closed = 1;
  }
}

static inline int open_inputs(char *argv[], Inputs *inputs, int num_in,
                              int std_in) {
  inputs->num_inputs = num_in;
  inputs->inputs = calloc(inputs->num_inputs, sizeof(struct input));

  for (int i = 0; i < inputs->num_inputs; i++) {
    // Open file or default to stdin
    char *name = NAME_FOR_STDIN;
    int fd = 0;
    if (!std_in) {
        name = argv[1 + i];
        fd = open(name, O_RDONLY);
        if (fd < 0) {
          perrorf("open %s", name);
          return 1;
        }
      }

    // Initialize input struct
    struct input this = { .fd = fd,
                          .name = name,
                          .buffer = NULL,
                          .is_closed = 0,
                          .is_near_eof = 0,
                          .is_readable = 0,
                          .is_buffered = 0, };

    inputs->inputs[i] = this;
  }

  inputs->last_closed = inputs->num_inputs;
  return 0;
}

static inline int open_outputs(char *argv[], Outputs *outputs, int num_out,
                               int base_idx_out, int std_out) {
  outputs->num_outputs = num_out;
  outputs->outputs = calloc(num_out, sizeof(Output));

  for (int i = 0; i < num_out; i++) {
    char *name = NAME_FOR_STDOUT;
    int fd = 1;
    if (!stdout) {
        name = argv[base_idx_out + i];
        fd = open(name, O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
          perrorf("open %s", name);
          return 1;
        }
      }
    struct output this = { .fd = fd,
                    .name = name,
                    .buffer = NULL,
                    .is_closed = 0,
                    .is_writable = 0,
                    .is_busy = 0, };
    outputs->outputs[i] = this;
  }

  outputs->last_closed = outputs->num_outputs;
  return 0;
}

static inline int parse_arguments(int argc, char *argv[], Inputs *inputs,
                                  Outputs *outputs) {

  // Count number of inputs and outputs
  int num_in = 0, num_out = 0, std_in = 0, std_out = 0;
  int base_idx_out = 1;
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], ">", 2)) {
      num_in = num_out;
      base_idx_out = i + 1;
      num_out = argc - base_idx_out;
      break;
    }
    ++num_out;
  }

  // If no inputs specified, default to stdin/ out
  if (num_in == 0) {
    std_in = 1;
    num_in = 1;
  }
  if (num_out == 0) {
    std_out = 1;
    num_out = 1;
  }

  if (open_inputs(argv, inputs, num_in, std_in)) {
    return 1;
  } else if (open_outputs(argv, outputs, num_out, base_idx_out, std_out)) {
    return 2;
  }
  return 0;
}

static inline void parse_environ(void) {
  readIntFromEnv(BLOCKSIZE, BLOCKSIZE, BLOCKSIZE > 0, DEFAULT_BLOCKSIZE);
}

int main(int argc, char *argv[]) {
  printf("HI!");

  parse_environ();

  DEBUG("Opening inputs and outputs from %d arguments...", argc - 1);
  Inputs inputs = { 0 };
  Outputs outputs = { 0 };
  if (parse_arguments(argc, argv, &inputs, &outputs)) {
    perror("mkmimo");
    return 1;
  }

  DEBUG("Reading from %d inputs...", inputs.num_inputs);
  DEBUG("Writing to %d outputs...", outputs.num_outputs);

  if (1) { // TODO
    mkmimo_nonblocking(&inputs, &outputs);
  } else {
    mkmimo_multithreaded(&inputs, &outputs);
  }

  clean_up(&inputs, &outputs);
  DEBUG("%s", "All done!");
  return 0;
}
