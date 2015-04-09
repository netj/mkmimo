#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>

#ifdef DEBUG
#undef DEBUG
#define DEBUG(fmt, args...) fprintf(stderr, fmt "\n", args);
#else
#define DEBUG(fmt, args...)
#endif  // DEBUG

struct input;
typedef struct input_buffer {
    // memory
    void *data;
    // capacity
    int capacity;
    // the byte range that contains data
    int begin, size;
    // where a full record separator is found in the range
    int end_of_last_record;
} Buffer;

typedef struct input {
    // input file descriptor
    int fd;
    // name of the file
    char *name;
    // the buffer for reading
    // TODO change this to pointer so buffer can be swapped btwn outputs
    Buffer *buffer;
    // state flags
    int is_closed;
    int is_readable;
    int is_buffered;
} Input;
typedef struct {
    // given inputs
    Input *inputs;
    int num_inputs;
    // this points to the index to insert the next closed one
    int last_closed;
    // how many are already closed
    int num_closed;
    // how many can be read without blocking
    int num_readable;
    // how many have buffers ready for output
    int num_buffered;
} Inputs;

typedef struct output {
    // output file descriptor
    int fd;
    // name of the file
    char *name;
    // pointer to the buffer for writing
    Buffer *buffer;
    // state flags
    int is_closed;
    int is_writable;
    int is_busy;
} Output;
typedef struct {
    // given outputs
    Output *outputs;
    int num_outputs;
    int last_closed;  // this points to the index to insert the next closed one
    int next_output;  // this points to the last used output for exchange
    // how many are already closed
    int num_closed;
    // how many outputs can be written without blocking
    int num_writable;
    // how many outputs have non-empty buffers
    int num_busy;
} Outputs;

static char NAME_FOR_STDIN[] = "-";
static char NAME_FOR_STDOUT[] = "-";

static int buffer_size = BUFSIZ;
Buffer *new_buffer() {
    Buffer *buf = malloc(sizeof(Buffer));
    if (buf == NULL) {
        perror("malloc");
        return NULL;
    }
    buf->data = malloc(buffer_size);
    if (buf->data == NULL) {
        perror("malloc");
        return NULL;
    }
    buf->capacity = buffer_size;
    buf->begin = 0;
    buf->size = 0;
    buf->end_of_last_record = -1;
    return buf;
}

int parse_arguments(int argc, char *argv[], Inputs *inputs, Outputs *outputs) {
    // first, count number of input/output arguments
    int num_in = 0, num_out = 0;
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
    // open them
    if (num_in == 0) {
        // default to stdin
        inputs->num_inputs = 1;
        inputs->inputs = calloc(1, sizeof(struct input));
        struct input this = {
            .fd = 0,
            .name = NAME_FOR_STDIN,
            .buffer = new_buffer(),
            .is_closed = 0,
            .is_readable = 0,
            .is_buffered = 0,
        };
        inputs->inputs[0] = this;
    } else {
        inputs->num_inputs = num_in;
        inputs->inputs = calloc(num_in, sizeof(Input));
        for (int i = 0; i < num_in; ++i) {
            char *name = argv[1 + i];
            int fd = open(name, O_RDONLY | O_NONBLOCK);
            if (fd < 0) {
                perror("open");
                return 1;
            }
            Input this = {
                .fd = fd,
                .name = name,
                .buffer = new_buffer(),
                .is_closed = 0,
                .is_readable = 0,
                .is_buffered = 0,
            };
            inputs->inputs[i] = this;
        }
    }
    inputs->last_closed = inputs->num_inputs;
    if (num_out == 0) {
        // default to stdout
        outputs->num_outputs = 1;
        outputs->outputs = calloc(1, sizeof(Output));
        Output this = {
            .fd = 1,
            .name = NAME_FOR_STDOUT,
            .buffer = new_buffer(),
            .is_closed = 0,
            .is_writable = 0,
            .is_busy = 0,
        };
        outputs->outputs[0] = this;
    } else {
        outputs->num_outputs = num_out;
        outputs->outputs = calloc(num_out, sizeof(Output));
        for (int i = 0; i < num_out; ++i) {
            char *name = argv[base_idx_out + i];
            int fd = open(name, O_WRONLY | O_NONBLOCK | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) {
                perror("open");
                return 2;
            }
            Output this = {
                .fd = fd,
                .name = name,
                .buffer = new_buffer(),
                .is_closed = 0,
                .is_writable = 0,
                .is_busy = 0,
            };
            outputs->outputs[i] = this;
        }
    }
    outputs->last_closed = outputs->num_outputs;
    return 0;
}

int records_are_flowing_between(Inputs *inputs, Outputs *outputs) {
    if (inputs->num_closed == inputs->num_inputs && inputs->num_buffered == 0 &&
        outputs->num_busy == 0)
        return 0;
    // TODO select/poll/epoll/kqueue on them
    static struct pollfd *fds = NULL;
    static int num_inputs_outputs = 0;
    if (fds == NULL) {
        // allocate poll(2) arguments only once
        num_inputs_outputs = inputs->num_inputs + outputs->num_outputs;
        fds = calloc(num_inputs_outputs, sizeof(struct pollfd));
    }
    // move closed inputs/outputs to the end and exclude from polling
    if (inputs->num_inputs - inputs->last_closed < inputs->num_closed)
        for (int i = 0; i < inputs->last_closed; ++i) {
            Input *input = &inputs->inputs[i];
            if (!input->is_closed) continue;
            input->is_readable = 0;
            // find the last input not closed
            int j = inputs->last_closed - 1;
            while (j > i && inputs->inputs[j].is_closed) --j;
            inputs->last_closed = j;
            // stop if everything past this input is closed
            if (j <= i) break;
            // move the closed one to the end
            DEBUG("moving closed input %s to back: %d", input->name, j);
            Input tmp = *input;
            *input = inputs->inputs[j];
            inputs->inputs[j] = tmp;
        }
    if (outputs->num_outputs - outputs->last_closed < outputs->num_closed)
        for (int i = 0; i < outputs->last_closed; ++i) {
            Output *output = &outputs->outputs[i];
            if (!output->is_closed) continue;
            output->is_writable = 0;
            // find the last output not closed
            int j = outputs->last_closed - 1;
            while (j > i && outputs->outputs[j].is_closed) --j;
            outputs->last_closed = j;
            // stop if everything past this output is closed
            if (j <= i) break;
            // move the closed one to the end
            DEBUG("moving closed output %s to back", output->name);
            Output tmp = *output;
            *output = outputs->outputs[j];
            outputs->outputs[j] = tmp;
        }
    // set up arguments for poll(2)
    int num_fds_to_poll =
        num_inputs_outputs - inputs->num_closed - outputs->num_closed;
    int num_inputs_to_poll = inputs->num_inputs - inputs->num_closed;
    if (num_fds_to_poll == 0) return 0;
    for (int i = 0; i < num_fds_to_poll; ++i) {
        struct pollfd *p = &fds[i];
        if (i < num_inputs_to_poll) {
            Input *input = &inputs->inputs[i];
            p->fd = input->fd;
            p->events = !input->is_buffered ? POLLIN : 0;
            p->revents = 0;
        } else {
            Output *output = &outputs->outputs[i - num_inputs_to_poll];
            p->fd = output->fd;
            // XXX setting POLLOUT causes busy waiting
            p->events = 0;  // XXX output->is_busy ? POLLOUT : 0;
            // DEBUG("%s: poll->events = %d", output->name, p->events);
            p->revents = 0;
        }
    }
    // use poll(2) to wait for any I/O events
    DEBUG("polling %d inputs/outputs", num_fds_to_poll);
    int num_events = poll(fds, num_fds_to_poll,
                          1000 /*msec*/);  //-1 /* wait indefinitely */);
    if (num_events < 0) {
        perror("poll");
        return 0;
    } else if (num_events > 0) {
        // update readable/writable states of inputs and outputs
        inputs->num_readable = outputs->num_writable = 0;
        for (int i = 0; i < num_fds_to_poll; ++i) {
            struct pollfd *p = &fds[i];
            if (i < num_inputs_to_poll) {
                Input *input = &inputs->inputs[i];
                if ((input->is_readable = !!(p->revents & POLLIN)))
                    ++inputs->num_readable;
            } else {
                Output *output = &outputs->outputs[i - num_inputs_to_poll];
                // TODO handle POLLHUP
                // XXX is_writable may be useless with poll(2)?
                if ((output->is_writable = 1)) ++outputs->num_writable;
            }
        }
        DEBUG("poll returned, found %d readable inputs, %d writable outputs",
              inputs->num_readable, outputs->num_writable);
        return 1;
    } else {
        DEBUG("%s", "poll timeout, found no I/O events");
        // no events
        return 1;
    }
}

int read_from_available(Inputs *inputs) {
    if (inputs->num_readable == 0) return 0;
    int num_input_buffers_ready = 0;
    // read from available inputs
    for (int i = 0; i < inputs->num_inputs; ++i) {
        Input *input = &inputs->inputs[i];
        // skip inputs that are closed or don't have data ready
        if (input->is_closed || !input->is_readable) continue;
        Buffer *buf = input->buffer;
        // skip inputs whose buffer is full
        if (buf->size == buf->capacity) continue;
        int scan_end_of_record_down_to = buf->end_of_last_record + 1;
        // read from the input to fill its buffer with at least one record
        int num_bytes_readable = buf->capacity - buf->size;
        // skip reading if buffer is already full
        if (num_bytes_readable <= 0) continue;
        DEBUG("%s: can read %d bytes", input->name, num_bytes_readable);
        int num_bytes_read = read(input->fd, buf->data + buf->begin + buf->size,
                                  num_bytes_readable);
        DEBUG("%s: %d bytes read", input->name, num_bytes_read);
        if (num_bytes_read < 0) {
            if (errno == EAGAIN)
                // stop reading when input is exhausted
                continue;
            else {
                // close the input on other errors
                perror("read");
                DEBUG("%s: input closed due to error", input->name);
                close(input->fd);
                input->is_closed = 1;
                ++inputs->num_closed;
                continue;
            }
        } else if (num_bytes_read == 0) {
            // EOF reached, close the input
            DEBUG("%s: input closed", input->name);
            close(input->fd);
            input->is_closed = 1;
            ++inputs->num_closed;
        } else {
            // read normally, reflect size increase
            buf->size += num_bytes_read;
        }
        // find the last record separator in the buffer
        // TODO use strrchr or a faster string matching algorithm
        int end_of_last_record = -1;
        for (int j = buf->begin + buf->size - 1;
             j >= scan_end_of_record_down_to; --j) {
            char c = ((char *)buf->data)[j];
            // TODO support user defined record delimiters
            if (c == '\n') {
                end_of_last_record = j;
                break;
            }
        }
        DEBUG("%s: record ends at %d", input->name, end_of_last_record);
        buf->end_of_last_record = end_of_last_record;
        if (end_of_last_record > -1) {
            // stop reading if at least one record exists in the buffer
            input->is_buffered = 1;
            ++num_input_buffers_ready;
        } else if (!input->is_closed && buf->size == buf->capacity) {
            // enlarge the buffer so a record that is larger than the current
            // buffer capacity can be read
            void *buf_larger = realloc(buf->data, buf->capacity * 2);
            if (buf_larger != NULL) {
                buf->data = buf_larger;
                buf->capacity *= 2;
                DEBUG("%s: realloc to %d bytes", input->name, buf->capacity);
                // bound the next scan for end-of-record separator
                scan_end_of_record_down_to = buf->begin + buf->size;
            } else {
                perror("realloc");
                // TODO handle out of memory more gracefully?
                abort();
            }
        }
    }
    DEBUG("read from inputs %d readable, %d now buffered, was %d",
          inputs->num_readable, num_input_buffers_ready, inputs->num_buffered);
    inputs->num_buffered = num_input_buffers_ready;
    return num_input_buffers_ready;
}

int write_to_available(Outputs *outputs) {
    if (outputs->num_writable == 0) return 0;
    int num_outputs_pending = 0;
    // write to each output its buffered records
    for (int i = 0; i < outputs->num_outputs; ++i) {
        Output *output = &outputs->outputs[i];
        // skip outputs that aren't writable yet
        if (!output->is_writable) continue;
        // skip outputs that aren't busy, i.e., have empty buffers
        if (!output->is_busy) continue;
        Buffer *buf = output->buffer;
        // write bufferred data to the output
        int num_bytes_writable = buf->size;
        if (num_bytes_writable <= 0) {
            // stop writing if there's nothing to write
            output->is_busy = 0;
            continue;
        }
        int num_bytes_written =
            write(output->fd, buf->data + buf->begin, num_bytes_writable);
        if (num_bytes_written >= 0) {
            // normal write
            DEBUG("%s: wrote %d bytes", output->name, num_bytes_written);
            buf->begin += num_bytes_written;
            buf->size -= num_bytes_written;
            if (buf->size == 0) {
                output->is_busy = 0;
            } else {
                output->is_busy = 1;
                ++num_outputs_pending;
                DEBUG("%s: %d bytes still left", output->name, buf->size);
            }
        } else {
            if (errno == EAGAIN) {
                // output is busy, will try again later
                DEBUG("%s: output busy", output->name);
                output->is_busy = 1;
                ++num_outputs_pending;
                DEBUG("%s: %d bytes still left", output->name, buf->size);
                continue;
            } else {
                // something went wrong
                perror("write");
                DEBUG("%s: output closed due to error", output->name);
                close(output->fd);
                output->is_closed = 1;
                ++outputs->num_closed;
                // XXX the buffer should be routed to another output
            }
        }
    }
    DEBUG("wrote to outputs %d writable, %d busy, and %d still busy",
          outputs->num_writable, outputs->num_busy, num_outputs_pending);
    outputs->num_busy = num_outputs_pending;
    return num_outputs_pending;
}

int exchange_buffered_records(Inputs *inputs, Outputs *outputs) {
    int num_exchanges = 0;
    // every buffered input should swap its buffer with an idle output
    for (int i = 0; i < inputs->num_inputs; ++i) {
        // stop early if it's apparent that no further pairs can be found
        if (inputs->num_buffered <= 0) break;
        if (outputs->num_busy == outputs->num_outputs - outputs->num_closed)
            break;
        // find an input whose buffer contains records
        Input *input = &inputs->inputs[i];
        if (!input->is_buffered) continue;
        // find an output that isn't busy, i.e., whose buffer is free
        Output *output = NULL;
        for (int j = 0; j < outputs->num_outputs; ++j) {
            Output *o = &outputs->outputs[outputs->next_output];
            ++outputs->next_output;
            outputs->next_output %= outputs->num_outputs;
            if (o->is_busy) continue;
            output = o;
            break;
        }
        // stop if no idle output can be found
        if (output == NULL) break;
        DEBUG("routing %d bytes: %s > %s",
              input->buffer->end_of_last_record + 1 - input->buffer->begin,
              input->name, output->name);
        // swap buffers between the buffered input and the idle output
        Buffer *buf = input->buffer;
        input->buffer = output->buffer;
        output->buffer = buf;
        // reset input buffer
        input->buffer->size = 0;
        input->buffer->begin = 0;
        input->buffer->end_of_last_record = -1;
        // make sure the trailing bytes at the end of input's buffer isn't lost
        int trailing_bytes_begin =
            buf->end_of_last_record + 1 /* length of the record separator */;
        int num_trailing_bytes_to_copy =
            buf->size - (trailing_bytes_begin - buf->begin);
        if (num_trailing_bytes_to_copy > 0) {
            DEBUG("trailing_bytes_begin=%d, num_trailing_bytes_to_copy=%d",
                  trailing_bytes_begin, num_trailing_bytes_to_copy);
            DEBUG("copying to %p from %p", input->buffer->data, buf->data);
            memcpy(input->buffer->data + input->buffer->begin,
                   buf->data + trailing_bytes_begin,
                   num_trailing_bytes_to_copy);
            input->buffer->size = num_trailing_bytes_to_copy;
            // truncate size, so the trailing bytes are ignored when output
            buf->size -= num_trailing_bytes_to_copy;
        }
        // now, mark the input as holding an incomplete buffer
        input->is_buffered = 0;
        --inputs->num_buffered;
        // and mark the output as busy
        output->is_busy = 1;
        ++outputs->num_busy;
        // keep track of the number of exchanges
        ++num_exchanges;
    }
    // TODO any closed but busy output's buffer should be swapped with another
    // idle output
    DEBUG("exchanged %d input-output pairs", num_exchanges);
    return num_exchanges;
}

int main(int argc, char *argv[]) {
    // read BLOCKSIZE environment variable
    char *blocksize = getenv("BLOCKSIZE");
    if (blocksize != NULL) {
        buffer_size = atoi(blocksize);
        if (buffer_size <= 0) {
            fprintf(stderr, "%d: Invalid BLOCKSIZE, using default %d\n",
                    buffer_size, BUFSIZ);
            buffer_size = BUFSIZ;
        }
    }

    Inputs inputs = {0};
    Outputs outputs = {0};
    if (parse_arguments(argc, argv, &inputs, &outputs)) {
        perror("mkmimo");
        return 1;
    }

    DEBUG("num_inputs: %d", inputs.num_inputs);
    DEBUG("num_outputs: %d", outputs.num_outputs);

    while (records_are_flowing_between(&inputs, &outputs)) {
        write_to_available(&outputs);
        if (read_from_available(&inputs) > 0)
            while (exchange_buffered_records(&inputs, &outputs) > 0)
                write_to_available(&outputs);
        DEBUG("%s", "----------------------------------------");
    }

    DEBUG("%s", "all done");

    // TODO cleanup: close all outputs and inputs

    return 0;
}
