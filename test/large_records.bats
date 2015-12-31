#!/usr/bin/env bats
load test_helpers

@test "records larger than buffer size (may take up to 2s)" {
    timeout=2s
    numrecords=10 record_width=21701 deviation=5000
    echo "generating input with wide records ($record_width ± $deviation)"
    if type openssl; then random_bytes() { openssl rand $1; }
    elif type perl; then  random_bytes() { perl -e 'print map {chr(int(rand(255)))} 0..$ARGV[0]' $1; }
    else                  random_bytes() { for i in $(seq $1); do printf $"\x$(printf %x $(($RANDOM % 256)))"; done; }
    fi
    for i in $(seq $numrecords); do
        numbytes=$(( $record_width + $RANDOM % (2*$deviation) - $deviation ))
        random_bytes $numbytes | tr '\n' ' '
        echo >&2 " generated a record with $numbytes bytes"
        echo
    done >wide_input
    cmp <(cat <wide_input) <(timeout $timeout mkmimo <wide_input 2>/dev/null)
}
