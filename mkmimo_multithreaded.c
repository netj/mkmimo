#include <pthread.h>
#include <semaphore.h>
#include "mkmimo_multithreaded.h"
#include "mkmimo.h"
#include "queue.h"
#include "buffer.h"

/* Globals */
Queue *full_buffers;
Queue *empty_buffers;
pthread_mutex_t full_buffers_lock;
pthread_mutex_t empty_buffers_lock;

int full_buffers_count;
int empty_buffers_count;
pthread_cond_t full_buffers_count_nonzero;
pthread_cond_t empty_buffers_count_nonzero;
pthread_mutex_t full_buffers_count_lock;
pthread_mutex_t empty_buffers_count_lock;

int num_input_threads;
pthread_mutex_t num_threads_lock;

/* Macros */
#define synchronized(fncall, mutex) \
  do {                              \
    pthread_mutex_lock(&mutex);     \
    fncall;                         \
    pthread_mutex_unlock(&mutex);   \
  } while (0)

#define synchronized_with_value(type, fncall, mutex) \
  {                                                  \
    pthread_mutex_lock(&mutex);                      \
    type retval = fncall;                            \
    pthread_mutex_unlock(&mutex);                    \
    retval                                           \
  }

/**
 * Find the last record separator in a full buffer.
 */
static inline void find_record_separator(Buffer *buf,
                                         int scan_end_of_record_down_to) {
  for (int j = buf->begin + buf->size - 1; j >= scan_end_of_record_down_to;
       --j) {
    char c = ((char *)buf->data)[j];
    if (c == '\n') {
      buf->end_of_last_record = j;
      break;
    }
  }
}

/**
 * Reads records from an input into a buffer.
 */
static inline void fill_buffer(Input *input) {
  Buffer *buf = input->buffer;
  int scan_end_of_record_down_to = buf->end_of_last_record + 1;
  DEBUG("%s", "In fill buffer fn!");

  for (int num_reads = input->is_near_eof ? 2 : 1; num_reads > 0; --num_reads) {
    int num_bytes_readable = buf->capacity - buf->size;
    DEBUG("%s: can read %d bytes", input->name, num_bytes_readable);

    int num_bytes_read =
        read(input->fd, buf->data + buf->begin + buf->size, num_bytes_readable);
    DEBUG("%s: %d bytes read", input->name, num_bytes_read);

    if (num_bytes_read < 0) {
      if (errno == EAGAIN) {
        // Stop reading when input is exhausted
        break;

      } else {
        // Close input on other errors
        // perrorf("read %s", input->name);
        DEBUG("%s: input closed due to error", input->name);
        close(input->fd);
        input->is_closed = 1;
        break;
      }

    } else if (num_bytes_read == 0) {
      // EOF reached, close input
      DEBUG("%s: input closed", input->name);
      close(input->fd);
      input->is_closed = 1;
      break;

    } else {
      // Normal read
      buf->size += num_bytes_read;
    }

    find_record_separator(buf, scan_end_of_record_down_to);
    DEBUG("%s: record ends at %d", input->name, buf->end_of_last_record);

    // Stop reading if at least one complete record exists in the buffer
    if (buf->end_of_last_record > -1) {
      input->is_buffered = 1;

    } else if (!input->is_closed && buf->size == buf->capacity) {
      // Enlarge the buffer so a record that is larger than the current
      // buffer capacity can be read
      DEBUG("%s: doubling buffer size to %d bytes", input->name,
            buf->capacity * 2);
      enlarge_buffer(buf, buf->capacity * 2);

      // Bound the next scan for end-of-record separator
      scan_end_of_record_down_to = buf->begin + buf->size;
    }
  }
}

/**
 * Move all bytes after the last record separator in the current buffer
 * to the overflow buffer.
 */
static inline void move_extra_bytes(Buffer *buf, Buffer *overflow) {
  int start = buf->end_of_last_record + 1;
  int num_extra_bytes = buf->size - start;
  while (num_extra_bytes > overflow->size) {
    enlarge_buffer(overflow, overflow->capacity * 2);
  }

  memcpy(overflow, (buf->data + start), num_extra_bytes);
  buf->size -= num_extra_bytes;
  overflow->size = num_extra_bytes;
}

static inline Buffer *dequeue_empty_buffer() {
  pthread_mutex_lock(&empty_buffers_count_lock);
  while (empty_buffers_count == 0)
    pthread_cond_wait(&empty_buffers_count_nonzero, &empty_buffers_count_lock);
  pthread_mutex_lock(&empty_buffers_lock);
  Buffer *buf = dequeue(empty_buffers);
  pthread_mutex_unlock(&empty_buffers_lock);
  empty_buffers_count--;
  pthread_mutex_unlock(&empty_buffers_count_lock);
  return buf;
}

static inline void queue_buffer(Buffer *buf, Queue *q, pthread_mutex_t *q_lock,
                                pthread_mutex_t *count_lock, int *count,
                                pthread_cond_t *cond_var) {
  pthread_mutex_lock(q_lock);
  queue(q, buf);
  pthread_mutex_unlock(q_lock);
  pthread_mutex_lock(count_lock);
  pthread_cond_signal(cond_var);
  (*count)++;
  pthread_mutex_unlock(count_lock);
}

/**
 * Function executed by the input threads. Grabs an empty buffer from
 * the empty buffers queue, fills it, and adds it to the full buffers
 * queue for processing by the output threads.
 */
static inline void *input_thread(void *arg) {
  Input *input = arg;
  input->buffer = dequeue_empty_buffer();
  DEBUG("%s grabbed an empty buffer from the queue", input->name);

  while (!input->is_closed) {
    fill_buffer(input);
    DEBUG("%s filled buffer", input->name);

    Buffer *overflow = dequeue_empty_buffer();
    DEBUG("%s grabbed an empty buffer from the queue", input->name);

    if (!input->is_closed) {
      move_extra_bytes(input->buffer, overflow);
    }

    if (input->buffer->size > 0) {
      queue_buffer(input->buffer, full_buffers, &full_buffers_lock,
                   &full_buffers_count_lock, &full_buffers_count,
                   &full_buffers_count_nonzero);
      input->buffer = overflow;
    }
  }

  // Decrement global input thread count and exit
  pthread_mutex_lock(&num_threads_lock);
  num_input_threads--;
  pthread_mutex_unlock(&num_threads_lock);
  return 0;
}

/**
 * Returns true if at least one input thread is still running, or if
 * there are more full buffers in the queue; false otherwise.
 */
static inline int more_output() {
  pthread_mutex_lock(&num_threads_lock);
  pthread_mutex_lock(&full_buffers_lock);
  bool has_more = !(num_input_threads == 0 && is_empty(full_buffers));
  pthread_mutex_unlock(&num_threads_lock);
  pthread_mutex_unlock(&full_buffers_lock);
  return has_more;
}

static inline Buffer *dequeue_full_buffer() {
  pthread_mutex_lock(&full_buffers_count_lock);
  while (empty_buffers_count == 0)
    pthread_cond_wait(&full_buffers_count_nonzero, &full_buffers_count_lock);
  pthread_mutex_lock(&full_buffers_lock);
  Buffer *buf = dequeue(full_buffers);
  pthread_mutex_unlock(&full_buffers_lock);
  full_buffers_count--;
  pthread_mutex_unlock(&full_buffers_count_lock);
  return buf;
}

/**
 * Writes the context of a full buffer to an output feed.
 */
static inline int empty_buffer(Output *output, Buffer *buf) {
  int num_bytes_writable = buf->size;
  if (num_bytes_writable <= 0) {
    return 0;
  }

  int num_bytes_written =
      write(output->fd, buf->data + buf->begin, num_bytes_writable);
  DEBUG("%s: wrote %d bytes", output->name, num_bytes_written);

  // Something went wrong
  if (num_bytes_written == 0) {
    perrorf("write %s", output->name);
    DEBUG("%s: output closed due to error", output->name);
    close(output->fd);
    output->is_closed = 1;
    queue_buffer(buf, full_buffers, &full_buffers_lock,
                 &full_buffers_count_lock, &full_buffers_count,
                 &full_buffers_count_nonzero);
    return 1;
  }

  buf->begin += num_bytes_written;
  buf->size -= num_bytes_written;
  return 0;
}

/**
 * Function executed by the output threads. Reads a full buffer that's
 * been filled by the empty buffer threads, empties it, and adds the
 * buffer back into the empty buffer queue.
 */
static inline void *output_thread(void *arg) {
  Output *output = arg;  //*((output *) arg);

  while (!output->is_closed && more_output()) {
    pthread_mutex_lock(&full_buffers_lock);
    output->buffer = dequeue_full_buffer();
    DEBUG("%s", "got a filled buffer");

    if (empty_buffer(output, output->buffer)) {
      // Emptying the buffer failed
      continue;
    }
    queue_buffer(output->buffer, empty_buffers, &empty_buffers_lock,
                 &empty_buffers_count_lock, &empty_buffers_count,
                 &empty_buffers_count_nonzero);
  }
  return 0;
}

/**
 * Initialize 2I + O empty buffers and add them to the empty queue.
 */
static inline void initialize_buffers(Inputs *inputs, Outputs *outputs,
                                      Queue *empty_buffers) {
  int num_buffers = (2 * inputs->num_inputs) + outputs->num_outputs;
  DEBUG("Adding %d to the empty buffers queue.", num_buffers);
  for (int i = 0; i < num_buffers; i++) {
    queue_buffer(new_buffer(), empty_buffers, &empty_buffers_lock,
                 &empty_buffers_count_lock, &empty_buffers_count,
                 &empty_buffers_count_nonzero);
  }
}

inline int mkmimo_multithreaded(Inputs *inputs, Outputs *outputs) {
  full_buffers = new_queue();
  empty_buffers = new_queue();

  initialize_buffers(inputs, outputs, empty_buffers);

  int num_threads = inputs->num_inputs + outputs->num_outputs;
  pthread_t threads[num_threads];

  for (int i = 0; i < inputs->num_inputs; i++) {
    DEBUG("Spawning input thread number %d.", i);
    pthread_create(&threads[i], NULL, input_thread, &(inputs->inputs[i]));
    pthread_mutex_lock(&num_threads_lock);
    num_input_threads++;
    pthread_mutex_unlock(&num_threads_lock);
  }
  for (int i = 0; i < outputs->num_outputs; i++) {
    DEBUG("Spawning output thread number %d.", i);
    if (pthread_create(&threads[i + inputs->num_inputs], NULL, output_thread,
                       &(outputs->outputs[i]))) {
      perror("pthread_create");
      return 1;
    }
  }

  // Join all of the threads
  for (int i = 0; i < num_threads; i++) {
    pthread_join(threads[i], NULL);
  }

  return 0;
}
