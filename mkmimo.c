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
    int size;
    // the byte range that contains data
    int begin, end;
    // where a full delimiter is found in the range
    int end_of_last_record;
    // owner input
    struct input *input;
} Buffer;

enum InputState {
    READY,
    BUFFERED,
    ROUTED,
    CLOSED,
};
typedef struct input {
    // input file descriptor
    int fd;
    // name of the file
    char *name;
    // the buffer for reading
    Buffer buffer;
    // state of the input: whether it's ready to read, buffered, or closed
    enum InputState state;
} Input;
typedef struct {
    Input *inputs;
    int num_inputs;
    int num_closed;
} Inputs;
enum OutputState {
    IDLE,
    BUSY,
    DONE,
};
typedef struct output {
    // output file descriptor
    int fd;
    // name of the file
    char *name;
    // pointer to the buffer for writing
    Buffer *buffer;
    // state of the output
    enum OutputState state;
} Output;
typedef struct {
    Output *outputs;
    int num_outputs;
    int num_busy;
    int last_written_output;
} Outputs;

static char NAME_FOR_STDIN[] = "-";
static char NAME_FOR_STDOUT[] = "-";

static int buffer_size = BUFSIZ;
int init_buffer(Buffer *buf, Input *input) {
    buf->data = malloc(buffer_size);
    if (buf->data == NULL) {
        perror("malloc");
        return -1;
    }
    buf->size = buffer_size;
    buf->begin = 0;  // inclusive
    buf->end = 0;    // exclusive
    buf->input = input;
    return buffer_size;
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
            .fd = 0, .name = NAME_FOR_STDIN, .state = READY,
        };
        inputs->inputs[0] = this;
        init_buffer(&inputs->inputs[0].buffer, &inputs->inputs[0]);
    } else {
        inputs->num_inputs = num_in;
        inputs->inputs = calloc(num_in, sizeof(Input));
        for (int i = 0; i < num_in; ++i) {
            char *name = argv[1 + i];
            int fd = open(name, O_RDONLY | O_NONBLOCK);
            if (fd < 0) return 1;
            Input this = {
                .fd = fd, .name = name, .state = READY,
            };
            inputs->inputs[i] = this;
            init_buffer(&inputs->inputs[i].buffer, &inputs->inputs[i]);
        }
    }
    if (num_out == 0) {
        // default to stdout
        outputs->num_outputs = 1;
        outputs->outputs = calloc(1, sizeof(Output));
        Output this = {
            .fd = 1, .name = NAME_FOR_STDOUT, .buffer = NULL, .state = IDLE,
        };
        outputs->outputs[0] = this;
    } else {
        outputs->num_outputs = num_out;
        outputs->outputs = calloc(num_out, sizeof(Output));
        for (int i = 0; i < num_out; ++i) {
            char *name = argv[base_idx_out + i];
            int fd = open(name, O_WRONLY | O_NONBLOCK | O_CREAT | O_TRUNC,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) return 2;
            Output this = {
                .fd = fd, .name = name, .buffer = NULL, .state = IDLE,
            };
            outputs->outputs[i] = this;
        }
    }
    return 0;
}

int read_records_from(Inputs *inputs) {
    int num_input_buffers_ready = 0;
    // TODO select/poll/epoll/kqueue on them
    static struct pollfd *fds = NULL;
    if (fds == NULL) {
        fds = calloc(inputs->num_inputs, sizeof(struct pollfd));
        for (int i = 0; i < inputs->num_inputs; ++i) {
            Input *input = &inputs->inputs[i];
            fds[i].fd = input->fd;
            fds[i].events = POLLIN;
            fds[i].revents = 0;
        }
    }
    int res = poll(fds, inputs->num_inputs, -1 /* wait indefinitely */);
    if (res < 0) {
        perror("poll");
        return -1;
    }
    if (res > 0) {
        // read from available inputs
        for (int i = 0; i < inputs->num_inputs; ++i) {
            Input *input = &inputs->inputs[i];
            if (!(input->state == READY && (fds[i].revents & POLLIN)))
                // skip inputs that aren't ready for another read
                // skip inputs that don't have data ready
                // TODO skip buffers busy for writing
                continue;
            Buffer *buf = &input->buffer;
            // read from input to fill its buffer with at least one record
            for (;;) {
                // TODO move buffer data to front if (buf->begin > 0)
                // try to fill the current buffer
                for (;;) {
                    int num_bytes_readable =
                        buf->size - (buf->end - buf->begin);
                    if (num_bytes_readable <= 0)
                        // stop reading when buffer is full
                        break;
                    DEBUG("%s: can read %d bytes", input->name,
                          num_bytes_readable);
                    int num_bytes_read = read(input->fd, buf->data + buf->end,
                                              num_bytes_readable);
                    DEBUG("%s: %d bytes read", input->name, num_bytes_read);
                    if (num_bytes_read < 0) {
                        if (errno == EAGAIN)
                            // stop reading when it's exhausted
                            break;
                        else {
                            // close the input on other errors
                            perror("read");
                            close(input->fd);
                            input->state = CLOSED;
                            ++inputs->num_closed;
                            break;
                        }
                    } else if (num_bytes_read == 0) {
                        // EOF reached, close the input
                        close(input->fd);
                        input->state = CLOSED;
                        ++inputs->num_closed;
                        break;
                    } else {
                        // read normally, reflect sizes
                        buf->end += num_bytes_read;
                    }
                }
                // find the last record's delimiter in the buffer
                // TODO use strrchr or a faster string matching algorithm
                int end_of_last_record = -1;
                for (int j = buf->end - 1; j >= buf->begin; --j) {
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
                    if (input->state != CLOSED) input->state = BUFFERED;
                    ++num_input_buffers_ready;
                    break;
                } else {
                    // enlarge the buffer and keep reading if a record is larger
                    // than the buffer size
                    void *buf_larger = realloc(buf->data, buf->size * 2);
                    if (buf_larger != NULL) {
                        buf->data = buf_larger;
                        buf->size *= 2;
                        DEBUG("%s: realloc to %d bytes", input->name,
                              buf->size);
                    } else {
                        perror("realloc");
                        // TODO handle out of memory situation
                        break;
                    }
                }
            }
        }
    }
    return num_input_buffers_ready;
}

int route_records_to_an_output(Input *input, Outputs *outputs) {
    // find the next less busy output
    Output *output = NULL;
    for (int i = 0; i < outputs->num_outputs; ++i) {
        ++outputs->last_written_output;
        outputs->last_written_output %= outputs->num_outputs;
        output = &outputs->outputs[outputs->last_written_output];
        if (output->state == IDLE) break;
    }
    if (output == NULL) {
        // TODO could not find an idle output
        return -1;
    }
    // route the input buffer to the output
    output->state = BUSY;
    output->buffer = &input->buffer;
    DEBUG("%s %d bytes routed to %s", input->name,
          (input->buffer.end - input->buffer.begin), output->name);
    if (input->state != CLOSED) input->state = ROUTED;
    return 0;
}

int write_pending_records_to(Outputs *outputs) {
    int num_outputs_pending = 0;
    // write to each output its buffered records
    for (int i = 0; i < outputs->num_outputs; ++i) {
        Output *output = &outputs->outputs[(outputs->last_written_output - i) %
                                           outputs->num_outputs];
        if (output->state != BUSY)
            // look at only BUSY outputs that already have a routed buffer
            continue;
        Buffer *buf = output->buffer;
        // write all bufferred data routed to this output
        for (;;) {
            int num_bytes_writable = buf->end_of_last_record + 1 - buf->begin;
            if (num_bytes_writable <= 0) {
                // stop writing if there's nothing to write
                output->state = IDLE;
                break;
            }
            int num_bytes_written =
                write(output->fd, buf->data + buf->begin, num_bytes_writable);
            if (num_bytes_written > 0) {
                // normal write
                buf->begin += num_bytes_written;
            } else if (num_bytes_written == 0) {
                // zero bytes written, will try again later
                ++num_outputs_pending;
                break;
            } else {
                if (errno == EAGAIN) {
                    // output is busy, will try again later
                    ++num_outputs_pending;
                    break;
                } else {
                    // something went wrong
                    perror("write");
                    close(output->fd);
                    output->state = DONE;
                    break;
                }
            }
        }
        // move buffer contents to front
        memmove(buf->data + 0, buf->data + buf->begin, buf->end - buf->begin);
        buf->end -= buf->begin;
        buf->begin = 0;
        // reflect input's state if output is done
        if (output->state == IDLE) {
            if (output->buffer->input->state != CLOSED)
                output->buffer->input->state = READY;
            output->buffer = NULL;
        }
    }
    return num_outputs_pending;
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

    Inputs inputs;
    Outputs outputs;
    if (parse_arguments(argc, argv, &inputs, &outputs)) {
        perror("mkmimo");
        return 1;
    }

    DEBUG("num_inputs: %d", inputs.num_inputs);
    DEBUG("num_outputs: %d", outputs.num_outputs);

    while (inputs.num_closed < inputs.num_inputs || outputs.num_busy > 0) {
        // read some
        if (inputs.num_closed < inputs.num_inputs) {
            int num_input_buffers_ready = read_records_from(&inputs);
            for (int i = 0; i < inputs.num_inputs; ++i) {
                if (num_input_buffers_ready <= 0) break;
                Input *input = &inputs.inputs[i];
                if (input->state != ROUTED &&
                    input->buffer.end_of_last_record != -1) {
                    route_records_to_an_output(input, &outputs);
                    --num_input_buffers_ready;
                }
            }
        }
        // write some
        outputs.num_busy = write_pending_records_to(&outputs);
    }

    // TODO cleanup: close all outputs and inputs

    return 0;
}
