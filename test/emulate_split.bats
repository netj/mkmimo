#!/usr/bin/env bats
load test_helpers

@test "split emulation (1 input, 10 output)" {
    numouts=10
    numlines=1000

    # create output files
    seq $numouts | split -n r/$numouts - out.

    # run mkmimo
    seq $numlines | mkmimo out.*

    # verify output
    echo 'hiiiii'
    cmp -b <(seq $numlines) <(sort -n out.*)
}

