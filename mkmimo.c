#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <errno.h>

#ifdef __APPLE__
#define POLLHUP_SUPPORT_UNRELIABLE
#endif

#define DEFAULT_BLOCKSIZE (4 * BUFSIZ)  // 4096 bytes
static int BLOCKSIZE = DEFAULT_BLOCKSIZE;

// when POLLHUP support is unreliable, use a timeout to detect input EOFs
#ifdef POLLHUP_SUPPORT_UNRELIABLE
#define DEFAULT_POLL_TIMEOUT_MSEC 1000 /*msec*/
#else
#define DEFAULT_POLL_TIMEOUT_MSEC -1 /* wait indefinitely */
#endif
static int POLL_TIMEOUT_MSEC = DEFAULT_POLL_TIMEOUT_MSEC;

// when all output is busy, throttle down by sleeping this much interval
// FIXME defaulting to busy waiting, need to find a principled way for throttling
#define DEFAULT_THROTTLE_SLEEP_MSEC 0
static int THROTTLE_SLEEP_MSEC = DEFAULT_THROTTLE_SLEEP_MSEC;
static struct timespec THROTTLE_TIMESPEC;

static char NAME_FOR_STDIN[] = "/dev/stdin";
static char NAME_FOR_STDOUT[] = "/dev/stdout";

#ifdef DEBUG
#undef DEBUG
#define DEBUG(fmt, args...) fprintf(stderr, "[%d] " fmt "\n", getpid(), args)
#else
#define DEBUG(fmt, args...)
#endif  // DEBUG

#define perrorf(fmt, args...) fprintf(stderr, fmt ": %s", args, strerror(errno))

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
    Buffer *buffer;
    // state flags
    int is_closed;
    int is_near_eof;
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

// a shorthand for updating both is_XYZ flag of an input/output and num_XYZ
// counts
#define SET_FLAG(items, item, flag, flag_val)      \
    do {                                           \
        if (!!(item)->is_##flag != !!(flag_val)) { \
            (item)->is_##flag = !!(flag_val);      \
            if ((flag_val))                        \
                ++(items)->num_##flag;             \
            else                                   \
                --(items)->num_##flag;             \
        }                                          \
    } while (0)
#define SET(item, flag, flag_val) SET_FLAG(item##s, item, flag, flag_val)

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

static inline void enlarge_buffer(Buffer *buf, size_t new_capacity) {
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

/*----------------------------------------------------------------------
 Portable function to set a socket into nonblocking mode.
 Calling this on a socket causes all future read() and write() calls on
 that socket to do only as much as they can immediately, and return
 without waiting.
 If no data can be read or written, they return -1 and set errno
 to EAGAIN (or EWOULDBLOCK).
 Thanks to Bjorn Reese for this code.

 See: http://www.kegel.com/dkftpbench/nonblocking.html
----------------------------------------------------------------------*/
static inline int setNonblocking(int fd) {
    int flags;
/* If they have O_NONBLOCK, use the Posix way to do it */
#if defined(O_NONBLOCK)
    /* Fixme: O_NONBLOCK is defined but broken on SunOS 4.1.x and AIX 3.2.5. */
    if (-1 == (flags = fcntl(fd, F_GETFL, 0))) flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    /* Otherwise, use the old way of doing it */
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

static inline int parse_arguments(int argc, char *argv[], Inputs *inputs,
                                  Outputs *outputs) {
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
            .is_near_eof = 0,
            .is_readable = 0,
            .is_buffered = 0,
        };
        inputs->inputs[0] = this;
        if (setNonblocking(this.fd) < 0) {
            perrorf("setNonblocking %s", this.name);
            return 1;
        }
    } else {
        inputs->num_inputs = num_in;
        inputs->inputs = calloc(num_in, sizeof(Input));
        for (int i = 0; i < num_in; ++i) {
            char *name = argv[1 + i];
            int fd = open(name, O_RDONLY);
            if (fd < 0) {
                perrorf("open %s", name);
                return 1;
            }
            Input this = {
                .fd = fd,
                .name = name,
                .buffer = new_buffer(),
                .is_closed = 0,
                .is_near_eof = 0,
                .is_readable = 0,
                .is_buffered = 0,
            };
            inputs->inputs[i] = this;
            if (setNonblocking(this.fd) < 0) {
                perrorf("setNonblocking %s", this.name);
                return 1;
            }
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
        if (setNonblocking(this.fd) < 0) {
            perrorf("setNonblocking %s", this.name);
            return 2;
        }
    } else {
        outputs->num_outputs = num_out;
        outputs->outputs = calloc(num_out, sizeof(Output));
        for (int i = 0; i < num_out; ++i) {
            char *name = argv[base_idx_out + i];
            int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) {
                perrorf("open %s", name);
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
            if (setNonblocking(this.fd) < 0) {
                perrorf("setNonblocking %s", this.name);
                return 2;
            }
        }
    }
    outputs->last_closed = outputs->num_outputs;
    return 0;
}

static inline void move_closed_inputs_outputs_to_the_end(Inputs *inputs,
                                                         Outputs *outputs) {
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
}

static inline int records_are_flowing_between(Inputs *inputs,
                                              Outputs *outputs) {
    // we can be sure no data will flow if all of the following holds:
    if (
        // 1. all inputs are closed
        inputs->num_closed == inputs->num_inputs &&
        // 2. no data is sitting in input buffers, i.e., all inputs have empty
        // buffers
        inputs->num_buffered == 0 &&
        // 3. no data is pending in output buffers, i.e., all outputs are idle
        outputs->num_busy == 0) {
        DEBUG("%s", "no data flow possible, skipping polling");
        return 0;
    } else
        DEBUG(
            "%d open inputs, %d buffered inputs, %d open outputs, %d busy "
            "outputs",
            inputs->num_inputs - inputs->num_closed, inputs->num_buffered,
            outputs->num_outputs - outputs->num_closed, outputs->num_busy);
    // TODO select/poll/epoll/kqueue on them
    static struct pollfd *fds = NULL;
    static int num_inputs_outputs = 0;
    if (fds == NULL) {
        // allocate poll(2) arguments only once
        num_inputs_outputs = inputs->num_inputs + outputs->num_outputs;
        fds = calloc(num_inputs_outputs, sizeof(struct pollfd));
    }
    move_closed_inputs_outputs_to_the_end(inputs, outputs);
    // set up arguments for poll(2)
    int num_fds_to_poll =
        num_inputs_outputs - inputs->num_closed - outputs->num_closed;
    int num_inputs_to_poll = inputs->num_inputs - inputs->num_closed;
    if (num_fds_to_poll == 0) return 0;
    int num_inputs_to_actually_poll = 0;
    int num_outputs_to_actually_poll = 0;
    for (int i = 0; i < num_fds_to_poll; ++i) {
        struct pollfd *p = &fds[i];
        p->revents = 0;
        if (i < num_inputs_to_poll) {
            Input *input = &inputs->inputs[i];
            p->fd = input->fd;
            // polling all inputs to see if they're readable to fill buffers
            p->events = POLLIN;
            // TODO skip polling inputs with full buffers
            // XXX or should we simply poll only inputs with empty buffers to
            // avoid excessive reads?
            // p->events = !input->is_buffered ? POLLIN : 0;
            if (p->events != 0) ++num_inputs_to_actually_poll;
        } else {
            Output *output = &outputs->outputs[i - num_inputs_to_poll];
            p->fd = output->fd;
            // poll only busy outputs, and regard all idle ones as writable
            // (below)
            p->events = output->is_busy ? POLLOUT : 0;
            if (p->events != 0) ++num_outputs_to_actually_poll;
        }
    }
    // throttle down if all outputs are busy
    if (outputs->num_busy == outputs->num_outputs - outputs->num_closed) {
        DEBUG("throttling down poll %d ms as all outputs are busy",
              THROTTLE_SLEEP_MSEC);
        nanosleep(&THROTTLE_TIMESPEC, NULL);
    }
    // use poll(2) to wait for any I/O events
    DEBUG("polling %d inputs and %d outputs", num_inputs_to_actually_poll,
          num_outputs_to_actually_poll);
    inputs->num_readable = outputs->num_writable = 0;
    int num_events = poll(fds, num_fds_to_poll, POLL_TIMEOUT_MSEC);
    if (num_events < 0) {
        perror("poll");
        return 0;
    } else if (num_events > 0) {
        // update readable/writable states of inputs and outputs
        for (int i = 0; i < num_fds_to_poll; ++i) {
            struct pollfd *p = &fds[i];
            if (i < num_inputs_to_poll) {
                Input *input = &inputs->inputs[i];
                if ((input->is_readable = !!(p->revents & (POLLIN | POLLHUP))))
                    ++inputs->num_readable;
                input->is_near_eof = !!(p->revents & (POLLHUP));
            } else {
                Output *output = &outputs->outputs[i - num_inputs_to_poll];
                // regard idle outputs as writable, and only check whether busy
                // ones become writable
                if ((output->is_writable =
                         (!output->is_busy ||
                          !!(p->revents & (POLLOUT | POLLHUP)))))
                    ++outputs->num_writable;
            }
        }
        DEBUG("poll returned, found %d readable inputs, %d writable outputs",
              inputs->num_readable, outputs->num_writable);
        return 1;
    } else {
        // timeout before any events
        DEBUG("%s", "poll timeout, found no I/O events");
        for (int i = 0; i < num_fds_to_poll; ++i) {
            if (i < num_inputs_to_poll) {
                Input *input = &inputs->inputs[i];
                if ((input->is_readable = 1)) ++inputs->num_readable;
                input->is_near_eof = 0;
            } else {
                Output *output = &outputs->outputs[i - num_inputs_to_poll];
                if ((output->is_writable = 1)) ++outputs->num_writable;
            }
        }
        return 1;
    }
}
static inline int read_from_available(Inputs *inputs) {
    // read from available inputs
    if (inputs->num_readable > 0)
        for (int i = 0; i < inputs->num_inputs; ++i) {
            Input *input = &inputs->inputs[i];
            // skip inputs that are closed or don't have data ready
            if (input->is_closed) continue;
            if (!input->is_readable) continue;
            Buffer *buf = input->buffer;
            // skip inputs whose buffer is full
            if (buf->size == buf->capacity) continue;
            int scan_end_of_record_down_to = buf->end_of_last_record + 1;
            // XXX optionally reading twice to detect the EOF earlier
            for (int num_reads = input->is_near_eof ? 2 : 1; num_reads > 0;
                 --num_reads) {
                // read from the input to fill its buffer with at least one
                // record
                int num_bytes_readable = buf->capacity - buf->size;
                // skip reading if buffer is already full
                if (num_bytes_readable <= 0) {
                    DEBUG("%s: buffer is full: %d used out of %d", input->name,
                          buf->size, buf->capacity);
                    continue;
                }
                DEBUG("%s: can read %d bytes", input->name, num_bytes_readable);
                int num_bytes_read =
                    read(input->fd, buf->data + buf->begin + buf->size,
                         num_bytes_readable);
                DEBUG("%s: %d bytes read", input->name, num_bytes_read);
                if (num_bytes_read < 0) {
                    if (errno == EAGAIN)
                        // stop reading when input is exhausted
                        break;
                    else {
                        // close the input on other errors
                        perrorf("read %s", input->name);
                        DEBUG("%s: input closed due to error", input->name);
                        close(input->fd);
                        SET(input, closed, 1);
                        break;
                    }
                } else if (num_bytes_read == 0) {
                    // EOF reached, close the input
                    DEBUG("%s: input closed", input->name);
                    close(input->fd);
                    SET(input, closed, 1);
                    break;
                } else {
                    // read normally, reflect size increase
                    buf->size += num_bytes_read;
                }
                // find the last record separator in the buffer
                // TODO use strrchr or a faster string matching algorithm
                for (int j = buf->begin + buf->size - 1;
                     j >= scan_end_of_record_down_to; --j) {
                    char c = ((char *)buf->data)[j];
                    // TODO support user defined record delimiters
                    if (c == '\n') {
                        buf->end_of_last_record = j;
                        break;
                    }
                }
                DEBUG("%s: record ends at %d", input->name,
                      buf->end_of_last_record);
                if (buf->end_of_last_record > -1) {
                    // stop reading if at least one record exists in the buffer
                    SET(input, buffered, 1);
                } else if (!input->is_closed && buf->size == buf->capacity) {
                    // enlarge the buffer so a record that is larger than the
                    // current buffer capacity can be read
                    DEBUG("%s: doubling buffer size to %d bytes", input->name,
                          buf->capacity * 2);
                    enlarge_buffer(buf, buf->capacity * 2);
                    // bound the next scan for end-of-record separator
                    scan_end_of_record_down_to = buf->begin + buf->size;
                }
            }
        }
    DEBUG("read from %d readable inputs, %d now buffered", inputs->num_readable,
          inputs->num_buffered);
    return inputs->num_buffered;
}
static inline int write_to_available(Outputs *outputs) {
    // write to each output its buffered records
    if (outputs->num_writable > 0)
        for (int i = 0; i < outputs->num_outputs; ++i) {
            Output *output = &outputs->outputs[i];
            // skip outputs that aren't busy, i.e., have empty buffers
            if (!output->is_busy) continue;
            // skip outputs that aren't writable yet
            if (!output->is_writable) continue;
            Buffer *buf = output->buffer;
            // write bufferred data to the output
            int num_bytes_writable = buf->size;
            if (num_bytes_writable <= 0) {
                // stop writing if there's nothing to write
                SET(output, busy, 0);
                continue;
            }
            int num_bytes_written =
                write(output->fd, buf->data + buf->begin, num_bytes_writable);
            DEBUG("%s: wrote %d bytes", output->name, num_bytes_written);
            if (num_bytes_written >= 0) {
                // normal write
                buf->begin += num_bytes_written;
                buf->size -= num_bytes_written;
                if (buf->size == 0) {
                    SET(output, busy, 0);
                } else {
                    SET(output, busy, 1);
                    DEBUG("%s: %d bytes still left", output->name, buf->size);
                }
            } else {
                if (errno == EAGAIN) {
                    // output is busy, will try again later
                    DEBUG("%s: output busy", output->name);
                    SET(output, busy, 1);
                    DEBUG("%s: %d bytes still left", output->name, buf->size);
                } else {
                    // something went wrong
                    perrorf("write %s", output->name);
                    DEBUG("%s: output closed due to error", output->name);
                    close(output->fd);
                    SET(output, closed, 1);
                    // XXX the buffer should be routed to another output
                }
            }
        }
    DEBUG("wrote to %d writable outputs, %d still busy", outputs->num_writable,
          outputs->num_busy);
    return outputs->num_busy;
}

static inline int exchange_buffered_records(Inputs *inputs, Outputs *outputs) {
    int num_exchanges = 0;
    // every buffered input should swap its buffer with an idle output
    for (int i = 0; i < inputs->num_inputs; ++i) {
        // stop early if it's apparent that no further pairs can be found
        if (inputs->num_buffered <= 0) {
            DEBUG("%s", "exchanging stops as no more inputs are buffered");
            break;
        }
        if (outputs->num_busy == outputs->num_outputs - outputs->num_closed) {
            DEBUG("%s", "exchanging stops as all outputs are busy");
            break;
        }
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
        if (output == NULL) continue;
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
            if (input->buffer->capacity < buf->capacity) {
                DEBUG("enlarging buffer size of %p to %d bytes from %d bytes",
                      input->buffer->data, input->buffer->capacity,
                      buf->capacity);
                enlarge_buffer(input->buffer, buf->capacity);
            }
            DEBUG("copying to %p from %p", input->buffer->data, buf->data);
            memcpy(input->buffer->data + input->buffer->begin,
                   buf->data + trailing_bytes_begin,
                   num_trailing_bytes_to_copy);
            input->buffer->size = num_trailing_bytes_to_copy;
            // truncate size, so the trailing bytes are ignored when output
            buf->size -= num_trailing_bytes_to_copy;
        }
        // now, mark the input as holding an incomplete buffer
        SET(input, buffered, 0);
        // and mark the output as busy
        SET(output, busy, 1);
        // keep track of the number of exchanges
        ++num_exchanges;
    }
    // TODO any closed but busy output's buffer should be swapped with another
    // idle output
    // TODO similarly a straggler output's buffer could be reallocated to
    // another faster one
    DEBUG("exchanged %d input-output pairs", num_exchanges);
    return num_exchanges;
}

static inline void parse_environ(void) {
#define readIntFromEnv(envName, ConfigVar, condition, defaultValue)     \
    do {                                                                \
        char *envValue = getenv(#envName);                              \
        if (envValue != NULL) {                                         \
            ConfigVar = atoi(envValue);                                 \
            if (!(condition)) {                                         \
                fprintf(stderr,                                         \
                        "%d: Invalid " #envName ", using default %d\n", \
                        ConfigVar, defaultValue);                       \
                ConfigVar = defaultValue;                               \
            } else {                                                    \
                DEBUG(#envName "=%d", ConfigVar);                       \
            }                                                           \
        }                                                               \
    } while (0)
    readIntFromEnv(BLOCKSIZE, BLOCKSIZE, BLOCKSIZE > 0,
                   DEFAULT_BLOCKSIZE);
    readIntFromEnv(POLL_TIMEOUT_MSEC, POLL_TIMEOUT_MSEC,
                   POLL_TIMEOUT_MSEC >= -1, DEFAULT_POLL_TIMEOUT_MSEC);
    readIntFromEnv(THROTTLE_SLEEP_MSEC, THROTTLE_SLEEP_MSEC,
                   THROTTLE_SLEEP_MSEC >= 0, DEFAULT_THROTTLE_SLEEP_MSEC);
    // prepare nanosleep's timespec for throttling
    THROTTLE_TIMESPEC.tv_sec = THROTTLE_SLEEP_MSEC / 1000;
    THROTTLE_TIMESPEC.tv_nsec = (THROTTLE_SLEEP_MSEC % 1000) * 1000000;
}

int main(int argc, char *argv[]) {
    parse_environ();

    DEBUG("opening inputs and outputs from %d arguments", argc - 1);
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
