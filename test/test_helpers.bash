# test_helpers for mkmimo

# put the built mkmimo at the front of PATH
PATH="$(dirname "$BASH_SOURCE")"/..:"$PATH"
# as well as test utilities
PATH="$(dirname "$BASH_SOURCE")"/util:"$PATH"

# common setup and teardown across test cases
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

# sums up all `N bytes` printed by dd(1) from executing given command
sum_dd_bytes() {
    {
        "$@" 2>&1 | grep '^[0-9][0-9]* bytes \(.*\) copied, ' | awk '{print $1}' | tr '\n' +
        echo 0
    } | bc
}
