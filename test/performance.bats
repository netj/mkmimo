#!/usr/bin/env bats
load test_helpers

@test "speed comparable to cat (takes 10s)" {
    numlines=100000000000000000 # a ridiculuously large number
    numsecs=5
    generate_input() {
        timeout ${numsecs}s seq $numlines
    }

    # run cat
    numlines_by_cat=$(generate_input | cat | wc -l)
    # run mkmimo (without any throttling down)
    numlines_by_mkmimo=$(generate_input | THROTTLE_SLEEP_MSEC=0 mkmimo 2>/dev/null | wc -l)

    echo "numlines_by_cat    = $numlines_by_cat"
    echo "numlines_by_mkmimo = $numlines_by_mkmimo"

    # mkmimo should be faster
    [ $numlines_by_mkmimo -gt $numlines_by_cat ] ||
    # or no slower than cat's performance by 20%
    [ $(bc <<<"$numlines_by_cat < 1.2 * $numlines_by_mkmimo") = 1 ] ||
    # or this was a DEBUG build
    allow_DEBUG_build_to_skip
}
