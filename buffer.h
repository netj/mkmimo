#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struct input;
typedef struct input_buffer {
  void *data;
  int capacity;
  int begin, size;        // Byte ranging containing data                                                                                                                         
  int end_of_last_record; // Last record seperator found in range                                                                                                                 
} Buffer;

Buffer *new_buffer();
void enlarge_buffer(Buffer *buf, size_t new_capacity);

#endif /* UTILS_H */
