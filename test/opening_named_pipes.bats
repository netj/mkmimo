#!/usr/bin/env bats
load test_helpers

@test "opening named pipe outputs before others" {
    numins=9999
    rm -f i o
    mkfifo i o
    numouts=$(
        mkmimo i \> o &
        seq $numins >i &
        wc -l <o &
        wait
    )
    echo $numin  input  lines
    echo $numout output lines
    [[ $numouts -eq $numins ]]
}
