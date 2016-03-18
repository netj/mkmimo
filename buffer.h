#ifndef BUFFER_H
#define BUFFER_H

#include <sys/types.h>

#define DEFAULT_BLOCKSIZE (4 * BUFSIZ)  // 4096
extern int BLOCKSIZE;

struct input;
typedef struct input_buffer {
  void *data;
  int capacity;
  int begin, size;         // Byte range containing data
  int end_of_last_record;  // Last record seperator found in range
} Buffer;

Buffer *new_buffer();
void clear_buffer(Buffer *buf);
void enlarge_buffer(Buffer *buf, size_t new_capacity);
void move_trailing_data_after_last_record(Buffer *target, Buffer *source);

#endif /* BUFFER_H */
