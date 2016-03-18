#include "buffer.h"
#include "mkmimo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Buffer *new_buffer() {
  Buffer *buf = malloc(sizeof(Buffer));
  if (buf == NULL) {
    perror("malloc");
    return NULL;
  }
  buf->data = malloc(BLOCKSIZE);
  if (buf->data == NULL) {
    perror("malloc");
    return NULL;
  }
  buf->capacity = BLOCKSIZE;
  buf->begin = 0;
  buf->size = 0;
  buf->end_of_last_record = -1;
  return buf;
}

void clear_buffer(Buffer *buf) {
  buf->begin = buf->size = 0;
  buf->end_of_last_record = -1;
}

void enlarge_buffer(Buffer *buf, size_t new_capacity) {
  void *buf_larger = realloc(buf->data, new_capacity);
  if (buf_larger != NULL) {
    buf->data = buf_larger;
    buf->capacity = new_capacity;
  } else {
    perror("realloc");
    // TODO handle out of memory more gracefully?
    abort();
  }
}

/**
 * Move all bytes after the last record separator in the current buffer
 * to the overflow buffer.
 */
inline void move_trailing_data_after_last_record(Buffer *tgt, Buffer *src) {
  int trailing_bytes_begin = src->end_of_last_record + 1;
  int num_trailing_bytes_to_copy =
      src->size - (trailing_bytes_begin - src->begin);
  if (num_trailing_bytes_to_copy > 0) {
    // ensure capacity
    DEBUG(" trailing data found in buffer %p (%d bytes, from %d)", src,
          num_trailing_bytes_to_copy, trailing_bytes_begin);
    int capacity = tgt->capacity;
    while (capacity - tgt->size < num_trailing_bytes_to_copy) {
      capacity *= 2;
    }
    if (capacity > tgt->capacity) {
      DEBUG(" enlarging capacity of buffer %p to %d bytes from %d bytes", tgt,
            capacity, tgt->capacity);
      enlarge_buffer(tgt, capacity);
    }
    // copy data
    DEBUG(" copying trailing data to buffer %p from %p", tgt, src);
    memcpy(tgt->data + tgt->begin, src->data + trailing_bytes_begin,
           num_trailing_bytes_to_copy);
    tgt->size += num_trailing_bytes_to_copy;
    src->size -= num_trailing_bytes_to_copy;
  }
}
