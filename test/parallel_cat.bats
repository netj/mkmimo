#!/usr/bin/env bats
load test_helpers

@test "parallel cat (10 input, 1 output)" {
    numins=10
    numlines=1000
    # create named pipes
    rm -f input.*
    seq $numins | split -n r/$numins - input.
    for i in input.*; do
        rm -f $i
        mkfifo $i
    done
    # launch mkmimo and processes that generate inputs
    mkmimo input.* \> out &
    j=1
    for i in input.*; do
        seq $((($j-1) * $numlines + 1)) $(($j * $numlines)) >$i &
        let ++j
    done
    wait
    # compare all the lines
    cmp <(seq 1 $(($numins * $numlines))) <(sort -n out)
}
