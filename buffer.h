#ifndef BUFFER_H
#define BUFFER_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_BLOCKSIZE (4 * BUFSIZ) // 4096
extern int BLOCKSIZE;

struct input;
typedef struct input_buffer {
  void *data;
  int capacity;
  int begin, size;        // Byte range containing data
  int end_of_last_record; // Last record seperator found in range
} Buffer;

Buffer *new_buffer();
void enlarge_buffer(Buffer *buf, size_t new_capacity);

#endif /* BUFFER_H */
