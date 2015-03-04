#!/usr/bin/env bats

# create a temporary directory for tests
MKMIMO_TMPDIR=$(mktemp -d "$BATS_TMPDIR"/mkmimo-tests.XXXXXX)


@test "cat emulation (1 input, 1 output)" {
    result=$(seq 10 | mkmimo | diff -u - <(seq 10))
    [ -z "$result" ]
}

@test "split emulation (1 input, 10 output)" {
    numouts=10
    numlines=1000
    result=$(
        cd $MKMIMO_TMPDIR
        seq $numouts | split -n r/$numouts - out.
        seq $numlines | mkmimo out.*
        wc -l out.* | tail -n 1 | awk '{print $1}'
        rm -f out.*
    )
    [ $result -eq $numlines ]
}

@test "parallel cat (10 input, 1 output)" {
    numins=10
    numlines=1000
    result=$(
        cd $MKMIMO_TMPDIR
        # create named pipes
        rm -f input.*
        seq $numins | split -n r/$numins - input.
        for i in input.*; do
            rm -f $i
            mkfifo $i
        done
        # launch mkmimo and processes that generate inputs
        mkmimo input.* \> out &
        for i in input.*; do
            seq $numlines >$i &
        done
        wait
        # count all the lines
        wc -l out | tail -n 1 | awk '{print $1}'
        rm -f input.* out
    )
    [ $result -eq $(( $numlines * $numins )) ]
}

# clean up the temporary directory for tests
rm -rf "$MKMIMO_TMPDIR"
