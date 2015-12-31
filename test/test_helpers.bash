# test_helpers for mkmimo

# put the built mkmimo at the front of PATH
PATH="$(dirname "$BASH_SOURCE")"/..:"$PATH"

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

