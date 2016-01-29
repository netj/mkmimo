#include "mkmimo_multithreaded.h"
#include "queue.h"

int initialize_buffers(Input *inputs, Output *outputs, Queue *empty_buffers) {
}

int mkmimo_multithreaded(Inputs *inputs, Outputs *outputs) {
  Queue *full_buffers = new_queue();
  Queue *empty_buffers = new_queue();

  initialize_buffers();

  return 0;
}
