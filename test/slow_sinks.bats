#!/usr/bin/env bats
load test_helpers

@test "chained to a slower named pipe sink (may take up to 10s)" {
    export numin=99999 timeout=10s
    rm -f i o
    mkfifo i o
    timeout $timeout bash -c '
        # produce lines
        seq $numin >i &
        # consume lines
        (
            # take first few lines
            for i in $(seq ${RANDOM:0:3}); do read; echo $REPLY; done
            # put a slight pause
            sleep 1
            # then consume the rest
            cat
        ) <o >ls &
        # use mkmimo to form the data flow
        mkmimo <i | mkmimo \> o &
        # cat <i >o &     # <-- this works fine
        # mkmimo <i >o &  # <-- as well as this
        # mkmimo i \> o & # <-- and this
        wait
    ' || true  # ignore timeout to show where it hung
    numout=$(wc -l <ls)
    # show number of lines
    echo $numin  input  lines
    echo $numout output lines consumed slowly
    [[ $numout -eq $numin ]]
}
