#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

typedef struct circular_buffer {
    // memory
    void *data;
    // capacity
    int size;
    // the byte range that contains data
    int begin, end;
} Buffer;
typedef struct record {
    // the buffer containing the record
    Buffer *buffer;
    // the byte range of the record in that buffer
    int begin, end;
} Record;

typedef struct input {
    // input file descriptor
    int fd;
    // name of the file
    char *name;
    // pointer to buffer for reading
    Buffer buffer;
    // the first few records in the buffer
    Record records[10];
} Input;
typedef struct output {
    // output file descriptor
    int fd;
    // name of the file
    char *name;
    // queue of records to write
    Record *records[10];
} Output;

static char NAME_FOR_STDIN[] = "-";
static char NAME_FOR_STDOUT[] = "-";

int parse_arguments(int argc, char *argv[], int *num_inputs,
                    struct input **inputs, int *num_outputs,
                    struct output **outputs) {
    // first, count number of input/output arguments
    int num_in = 0, num_out = 0;
    int base_idx_out = 0;
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
        *num_inputs = 1;
        *inputs = calloc(1, sizeof(struct input));
        struct input this = {
            .fd = 0, .name = NAME_FOR_STDIN,
        };
        **inputs = this;
    } else {
        *num_inputs = num_in;
        *inputs = calloc(num_in, sizeof(Input));
        for (int i = 0; i < num_in; ++i) {
            char *name = argv[i];
            int fd = open(name, O_RDONLY | O_NONBLOCK);
            if (fd < 0) return 1;
            Input this = {
                .fd = fd, .name = name,
            };
            (*inputs)[i] = this;
        }
    }
    if (num_out == 0) {
        // default to stdout
        *num_outputs = 1;
        *outputs = calloc(1, sizeof(Output));
        Output this = {
            .fd = 1, .name = NAME_FOR_STDOUT,
        };
        **outputs = this;
    } else {
        *num_outputs = num_out;
        *outputs = calloc(num_out, sizeof(Output));
        for (int i = 0; i < num_out; ++i) {
            char *name = argv[base_idx_out + i];
            int fd = open(name, O_WRONLY | O_NONBLOCK | O_CREAT,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) return 2;
            Output this = {
                .fd = fd, .name = name,
            };
            (*outputs)[i] = this;
        }
    }
    return 0;
}

int read_records_from(Input *inputs, int num_inputs, int *num_closed) {
    // TODO select/poll/epoll/kqueue on them
    // TODO read from available inputs
    // TODO count available records
    // TODO update number of closed inputs if any
    int num_records_read = 0;
    return num_records_read;
}

int has_next_record(Input *input, Record **record) {
    // TODO point to the next record in the given input if it has one
    int whether_input_has_a_record = 0;
    return whether_input_has_a_record;
}

int route_record_to_an_output(Record *record, Output *outputs, int num_outputs,
                              int *last_written_output) {
    // TODO find the next less busy output
    // TODO buffer given record to that output
    return 0;
}

int write_pending_records_to(Output *outputs, int num_outputs) {
    // TODO write to each output its buffered records
    int num_outputs_pending = 0;
    return num_outputs_pending;
}

int main(int argc, char *argv[]) {
    int num_inputs, num_outputs;
    Input *inputs;
    Output *outputs;
    if (parse_arguments(argc, argv, &num_inputs, &inputs, &num_outputs,
                        &outputs)) {
        perror("mkmimo");
        return 1;
    }

    printf("num_inputs: %d\n", num_inputs);
    printf("num_outputs: %d\n", num_outputs);

    int num_closed = 0, num_busy = 0;
    int output_schedule = 0;
    Record *record;
    while (num_closed < num_inputs || num_busy > 0) {
        // read some
        if (num_closed < num_inputs) {
            // TODO with back-pressure (unless all output is busy/blocked)
            int num_records =
                read_records_from(inputs, num_inputs, &num_closed);
            for (int i = 0; num_records > 0 && i < num_inputs; ++i) {
                Input *input = &inputs[i];
                while (num_records > 0 && has_next_record(input, &record)) {
                    route_record_to_an_output(record, outputs, num_outputs,
                                              &output_schedule);
                    --num_records;
                }
            }
        }
        // write some
        num_busy = write_pending_records_to(outputs, num_outputs);
    }

    // TODO cleanup: close all outputs and inputs

    return 0;
}
