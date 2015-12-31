#!/usr/bin/env bats
load test_helpers

@test "multi-input multi-output (17 inputs, 83 outputs)" {
    numins=17 numouts=83
    numlines=10000

    # assemble process substitution commands for input/output
    inputs=
    for i in $(seq $numins)
    do inputs+=" <(seq $((($i-1) * $numlines + 1)) $(($i * $numlines)))"
    done
    for i in $(seq $numouts)
    do outputs+=" >(cat >out.$i)"
    done
    rm -f out.*

    # run mkmimo
    eval "mkmimo $inputs \\> $outputs"

    # verify output is identical
    cmp <(eval "sort $inputs") <(sort out.*)
}
