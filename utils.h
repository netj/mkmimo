#ifndef UTILS_H
#define UTILS_H

#include "mkmimo.h"

Buffer *new_buffer();
void enlarge_buffer(Buffer *buf, size_t new_capacity);

#endif /* UTILS_H */
