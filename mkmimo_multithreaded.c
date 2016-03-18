#include <pthread.h>
#include <semaphore.h>
#include "mkmimo_multithreaded.h"
#include "mkmimo.h"
#include "queue.h"
#include "buffer.h"

// Buffer pools
Queue *full_buffers;
Queue *empty_buffers;

// Flags
int data_is_flowing_in = 1;
int data_should_flow_in = 1;
int data_should_flow_out = 1;

/**
  * Stop all threads
  */
static inline void teardown_all_threads(void) {
  // XXX this tears down all input threads
  data_should_flow_in = 0;
  // XXX this tears down all output threads
  data_should_flow_out = 0;
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
 * Move all bytes after the last record separator in the current buffer
 * to the overflow buffer.
 */
static inline void move_extra_bytes(Buffer *src, Buffer *tgt) {
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

/**
  * Grab a buffer from the empty pool and clear it for fresh data.
  */
static inline Buffer *grab_empty_buffer(void) {
  Buffer *buf = dequeue_or_wait(empty_buffers);
  clear_buffer(buf);
  return buf;
}

/**
 * Function executed by the input threads. Grabs an empty buffer from
 * the empty buffers queue, fills it, and adds it to the full buffers
 * queue for processing by the output threads.
 */
static void *read_buffers_from_input(void *arg) {
  Input *input = arg;

  input->buffer = grab_empty_buffer();
  DEBUG("%s: grabbed an empty buffer %p", input->name, input->buffer);
  while (data_should_flow_in) {
    // Read from input to fill up the buffer with at least one record
    Buffer *buf = input->buffer;
    int scan_end_of_record_down_to = buf->end_of_last_record + 1;
    for (;;) {
      int num_bytes_readable = buf->capacity - buf->size;
      DEBUG("%s: can read %d bytes", input->name, num_bytes_readable);

      int num_bytes_read = read(input->fd, buf->data + buf->begin + buf->size,
                                num_bytes_readable);
      DEBUG("%s: %d bytes read", input->name, num_bytes_read);

      if (num_bytes_read < 0) {
        // Close input upon errors
        perrorf("read %s", input->name);
        DEBUG("%s: input closed due to error", input->name);
        close(input->fd);
        input->is_closed = 1;
        teardown_all_threads();
        break;

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

      // Stop reading if at least one complete record has been read into the
      // buffer
      if (buf->end_of_last_record > -1) {
        break;

      } else if (buf->size == buf->capacity) {
        // Enlarge the buffer so a record that is larger than the current
        // buffer capacity can be read
        DEBUG("%s: doubling buffer size to %d bytes", input->name,
              buf->capacity * 2);
        enlarge_buffer(buf, buf->capacity * 2);

        // Bound the next scan for end-of-record separator
        scan_end_of_record_down_to = buf->begin + buf->size;
      }
    }
    DEBUG("%s: filled buffer %p, holding %d bytes", input->name, input->buffer,
          input->buffer->size);

    if (input->is_closed) {
      // Once input is closed, submit the last filled buffer to output threads,
      // and exit the loop since no more can be read
      DEBUG("%s: submitting the last filled buffer %p", input->name,
            input->buffer);
      queue_and_signal(full_buffers, input->buffer);
      break;
    } else if (input->buffer->size > 0) {
      // Otherwise, keep only complete records in the buffer and move the
      // trailing bytes to a new empty buffer
      DEBUG("%s: grabbing next empty buffer", input->name);
      Buffer *overflow = grab_empty_buffer();
      DEBUG("%s: grabbed an empty buffer %p", input->name, overflow);
      // Submit the trimmed buffer and continue the same steps with the new
      // buffer
      DEBUG("%s: submitting after trimming the filled buffer %p", input->name,
            input->buffer);
      move_extra_bytes(input->buffer, overflow);
      queue_and_signal(full_buffers, input->buffer);
      input->buffer = overflow;
    } else {
      // XXX This should never happen, but it's harmless try to fill the buffer
      // again if it ever does
      DEBUG(
          "%s: XXX THIS SHOULD NEVER HAPPEN! but harmless to try filling the "
          "same buffer again",
          input->name);
    }
  }

  DEBUG("%s: stops input thread", input->name);
  return NULL;
}

/**
 * Function executed by the output threads. Reads a filled buffer produced by
 * input threads,
 * writes it, and adds the
 * buffer back into the empty buffer queue.
 */
static void *write_buffers_to_output(void *arg) {
  Output *output = arg;

  while (data_should_flow_out) {
    // Grab a filled buffer
    DEBUG("%s: waiting for a filled buffer", output->name);
    Buffer *buf = output->buffer = dequeue_or_wait(full_buffers);
    DEBUG("%s: got a filled buffer %p, holding %d bytes", output->name, buf,
          buf->size);

    // Write all buffered data to the output
    int num_bytes_writable = buf->size;
    int buf_offset = 0;
    while (num_bytes_writable > 0) {
      int num_bytes_written = write(
          output->fd, buf->data + buf->begin + buf_offset, num_bytes_writable);
      DEBUG("%s: wrote %d bytes", output->name, num_bytes_written);

      if (num_bytes_written <= 0) {
        perrorf("write %s", output->name);
        DEBUG("%s: output closed due to error", output->name);
        close(output->fd);
        output->is_closed = 1;
        teardown_all_threads();
        break;
      }

      buf_offset += num_bytes_written;
      num_bytes_writable -= num_bytes_written;
    }

    if (num_bytes_writable == 0) {
      // Return the buffer back to the pool and continue with the next available
      // buffer
      queue_and_signal(empty_buffers, buf);
      DEBUG("%s: recycling the buffer %p", output->name, buf);
    } else {
      // Otherwise, the output was closed before everything in the buffer was
      // written, so send the buffer back to filled pool, so someone else can
      // handle it
      DEBUG("%s: resubmitting the buffer %p since output closed prematurely",
            output->name, buf);
      queue_and_signal(full_buffers, buf);
      // XXX This can inevitably create duplicate records
      // TODO Allow user to choose whether to drop or retransmit such records
    }

    // Stop once the output is closed
    if (output->is_closed) {
      DEBUG("%s: output is now closed", output->name);
      break;
    }

    // Also stop if no data is flowing in and remains in the pool
    if (!data_is_flowing_in && is_empty(full_buffers)) {
      DEBUG("%s: anticipates no more buffers to arrive", output->name);
      data_should_flow_out = 0;
      break;
    }
  }

  DEBUG("%s: stops output thread", output->name);
  return NULL;
}

/**
  * Function executed as a thread once all input threads are finished to wake
  * pending output threads to flush all the buffered data.
  */
static void *flush_remaining_data(void *arg) {
  Outputs *outputs = arg;
  int num_flushes = 2 * outputs->num_outputs;
  for (int i = 0; i < num_flushes; ++i) {
    Buffer *empty = grab_empty_buffer();
    DEBUG("Submitting an empty buffer to wake an output thread (%d remaining)",
          num_flushes - 1 - i);
    queue_and_signal(full_buffers, empty);
  }
  return NULL;
}

/**
  * Multi-threaded implementation of mkmimo
  */
inline int mkmimo_multithreaded(Inputs *inputs, Outputs *outputs) {
  full_buffers = new_queue();
  empty_buffers = new_queue();

  // Initialize 2I + O empty buffers and add them to the empty queue.
  int num_buffers = (2 * inputs->num_inputs) + outputs->num_outputs;
  DEBUG("Creating %d empty buffers", num_buffers);
  for (int i = 0; i < num_buffers; i++) {
    queue(empty_buffers, new_buffer());
  }

  // Initialize the state
  data_is_flowing_in = 1;

  // Spawn a thread for every input and output
  pthread_t output_threads[outputs->num_outputs];
  for (int i = 0; i < outputs->num_outputs; i++) {
    Output *output = &(outputs->outputs[i]);
    DEBUG("Spawning output thread for %s", output->name);
    CHECK_ERRNO(pthread_create, &output_threads[i], NULL,
                write_buffers_to_output, output);
  }
  pthread_t input_threads[inputs->num_inputs];
  for (int i = 0; i < inputs->num_inputs; i++) {
    Input *input = &(inputs->inputs[i]);
    DEBUG("Spawning input thread for %s", input->name);
    CHECK_ERRNO(pthread_create, &input_threads[i], NULL,
                read_buffers_from_input, input);
  }

  // Wait for all input threads to read all data
  for (int i = 0; i < inputs->num_inputs; i++) {
    DEBUG("Waiting for %s and %d more input threads to finish",
          inputs->inputs[i].name, inputs->num_inputs - 1 - i);
    CHECK_ERRNO(pthread_join, input_threads[i], NULL);
  }
  DEBUG("%s", "All input threads finished");
  // Let output threads know no more data is coming in
  data_is_flowing_in = 0;
  // Spawn a thread for waking up all pending output threads
  pthread_t flush_thread;
  CHECK_ERRNO(pthread_create, &flush_thread, NULL, flush_remaining_data,
              outputs);
  // Wait for all output threads to finish writing the buffers
  for (int i = 0; i < outputs->num_outputs; i++) {
    DEBUG("Waiting for %s and %d more output threads to finish",
          outputs->outputs[i].name, outputs->num_outputs - 1 - i);
    CHECK_ERRNO(pthread_join, output_threads[i], NULL);
  }
  CHECK_ERRNO(pthread_cancel, flush_thread);

  return 0;
}
