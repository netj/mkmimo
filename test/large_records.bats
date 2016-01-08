#!/usr/bin/env bats
load test_helpers

@test "records larger than buffer size (may take up to 2s)" {
    timeout=2s
    numrecords=10 record_width=21701 deviation=5000
    echo "generating input with wide records ($record_width ± $deviation)"
    {
        random_records $record_width.$deviation $numrecords '\n'
        printf '\n'
    } | dd of=wide_input
    cmp wide_input <(timeout $timeout mkmimo <wide_input 2>/dev/null)
}
