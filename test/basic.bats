#!/usr/bin/env bats

setup() {
    # create a temporary directory for tests
    MKMIMO_TMPDIR=$(mktemp -d "$BATS_TMPDIR"/mkmimo-tests.XXXXXX)
    pushd "$MKMIMO_TMPDIR" >/dev/null
}

teardown() {
    popd >/dev/null
    # clean up the temporary directory for tests
    rm -rf "$MKMIMO_TMPDIR"
}

@test "cat emulation (1 input, 1 output)" {
    numlines=1000

    # run mkmimo and verify its output
    seq $numlines | mkmimo | cmp - <(seq $numlines)
}

@test "split emulation (1 input, 10 output)" {
    numouts=10
    numlines=1000

    # create output files
    seq $numouts | split -n r/$numouts - out.

    # run mkmimo
    seq $numlines | mkmimo out.*

    # verify output
    cmp <(seq $numlines) <(sort -n out.*)
}

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
