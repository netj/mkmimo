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

@test "speed comparable to cat (takes 10s)" {
    numlines=100000000000000000 # a ridiculuously large number
    numsecs=5
    generate_input() {
        timeout ${numsecs}s seq $numlines
    }

    # run cat
    numlines_by_cat=$(generate_input | cat | wc -l)
    # run mkmimo (without any throttling down)
    numlines_by_mkmimo=$(generate_input | THROTTLE_SLEEP_MSEC=0 mkmimo | wc -l)

    echo "numlines_by_cat    = $numlines_by_cat"
    echo "numlines_by_mkmimo = $numlines_by_mkmimo"

    # mkmimo should be faster
    [ $numlines_by_mkmimo -gt $numlines_by_cat ] ||
    # or no slower than cat's performance by 20%
    [ $(bc <<<"$numlines_by_cat < 1.2 * $numlines_by_mkmimo") = 1 ]
}
