#include "mkmimo.h"

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
