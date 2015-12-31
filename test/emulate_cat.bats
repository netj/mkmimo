#!/usr/bin/env bats
load test_helpers

@test "cat emulation (1 input, 1 output)" {
    numlines=1000

    # run mkmimo and verify its output
    seq $numlines | mkmimo | cmp - <(seq $numlines)
}
