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
struct input;
typedef struct record {
    // the input the record originates from
    struct input *input;
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
typedef struct {
    Input *inputs;
    int num_inputs;
    int num_closed;
} Inputs;
typedef struct output {
    // output file descriptor
    int fd;
    // name of the file
    char *name;
    // queue of records to write
    Record *records[10];
} Output;
typedef struct {
    Output *outputs;
    int num_outputs;
    int num_busy;
    int last_written_output;
} Outputs;

static char NAME_FOR_STDIN[] = "-";
static char NAME_FOR_STDOUT[] = "-";

int parse_arguments(int argc, char *argv[], Inputs *inputs, Outputs *outputs) {
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
        inputs->num_inputs = 1;
        inputs->inputs = calloc(1, sizeof(struct input));
        struct input this = {
            .fd = 0, .name = NAME_FOR_STDIN,
        };
        inputs->inputs[0] = this;
    } else {
        inputs->num_inputs = num_in;
        inputs->inputs = calloc(num_in, sizeof(Input));
        for (int i = 0; i < num_in; ++i) {
            char *name = argv[i];
            int fd = open(name, O_RDONLY | O_NONBLOCK);
            if (fd < 0) return 1;
            Input this = {
                .fd = fd, .name = name,
            };
            inputs->inputs[i] = this;
        }
    }
    if (num_out == 0) {
        // default to stdout
        outputs->num_outputs = 1;
        outputs->outputs = calloc(1, sizeof(Output));
        Output this = {
            .fd = 1, .name = NAME_FOR_STDOUT,
        };
        outputs->outputs[0] = this;
    } else {
        outputs->num_outputs = num_out;
        outputs->outputs = calloc(num_out, sizeof(Output));
        for (int i = 0; i < num_out; ++i) {
            char *name = argv[base_idx_out + i];
            int fd = open(name, O_WRONLY | O_NONBLOCK | O_CREAT,
                          S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) return 2;
            Output this = {
                .fd = fd, .name = name,
            };
            outputs->outputs[i] = this;
        }
    }
    return 0;
}

int read_records_from(Inputs *inputs) {
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

int route_record_to_an_output(Record *record, Outputs *outputs) {
    // TODO find the next less busy output
    // TODO buffer given record to that output
    return 0;
}

int write_pending_records_to(Outputs *outputs) {
    // TODO write to each output its buffered records
    int num_outputs_pending = 0;
    return num_outputs_pending;
}

int main(int argc, char *argv[]) {
    Inputs inputs;
    Outputs outputs;
    if (parse_arguments(argc, argv, &inputs, &outputs)) {
        perror("mkmimo");
        return 1;
    }

    printf("num_inputs: %d\n", inputs.num_inputs);
    printf("num_outputs: %d\n", outputs.num_outputs);

    Record *record;
    while (inputs.num_closed < inputs.num_inputs || outputs.num_busy > 0) {
        // read some
        if (inputs.num_closed < inputs.num_inputs) {
            // TODO with back-pressure (unless all output is busy/blocked)
            int num_records =
                read_records_from(&inputs);
            for (int i = 0; num_records > 0 && i < inputs.num_inputs; ++i) {
                Input *input = &inputs.inputs[i];
                while (num_records > 0 && has_next_record(input, &record)) {
                    route_record_to_an_output(record, &outputs);
                    --num_records;
                }
            }
        }
        // write some
        write_pending_records_to(&outputs);
    }

    // TODO cleanup: close all outputs and inputs

    return 0;
}
