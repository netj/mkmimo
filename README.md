mkmimo [![Build Status](https://travis-ci.org/netj/mkmimo.svg?branch=master)](https://travis-ci.org/netj/mkmimo)
======

Creates a multiple-input multiple-output pipe that can basically connect many parallel processes with named pipes or bash process substitution to stream lines of text without blocking.

## Usage

### cat emulation
```bash
n=1000000
cmp -b <(seq $n | cat) <(seq $n | mkmimo)
```

### GNU split, but running in parallel
```bash
n=1000000 m=10
split -n r/$m <(seq $m) out.
seq $n | mkmimo out.*
cmp -b <(seq $n) <(sort -n out.*)
```

### Many inputs, many outputs
```bash
numins=17 numouts=83
numlines=1000000

inputs=
for i in $(seq $numins)
do inputs+=" <(seq $((($i-1) * $numlines + 1)) $(($i * $numlines)))"
done
for i in $(seq $numouts)
do outputs+=" >(cat >out.$i)"
done
rm -f out.*

eval "mkmimo $inputs \\> $outputs"

cmp <(eval "sort $inputs") <(sort out.*)
```

For more examples, see the [.bats test files in the "/test" folder](test).


## Runtime Parameters (Environment Variables)

### Common parameters

* `MKMIMO_IMPL` determines which implementation to use.
    Possible values are:

    * `multithreaded`
    * `nonblocking`

* `BLOCKSIZE` is the initial size of each buffer in bytes.
    It defaults to `4096` (4KiB).

### Multi-threaded implementation

This implementation keeps one thread per given input/output stream.
There are two shared pools of buffers: empty and filled.
Each input thread takes an empty buffer from the pool and fills it with the data read from its input stream, and the filled buffer is placed into the other pool.
Each output thread takes a filled buffer from that pool and writes the data to its output stream, then returns the buffer back to the empty pool.
They repeat their job until all input has been read, buffered, then written to an output.

This implementation is used when `MKMIMO_IMPL=multithreaded`, and the following environment variables are parsed:

* `MULTIBUFFERING` that is the multiplicative factor for [multiple buffering](https://en.wikipedia.org/wiki/Multiple_buffering).
    `MULTIBUFFERING=2` is double-buffering, `MULTIBUFFERING=3` is triple-buffering, `MULTIBUFFERING=4` is quad, and so on.
    It defaults to `2`, double-buffering.


### Non-blocking I/O implementation

This implementation keeps a single thread that uses `poll(2)` system call to find input/output streams that can be read/written and performs non-blocking I/O exchanging buffers between them until all data has been transferred.
On OS X 10.11 (El Capitan), the way this implementation calls `poll(2)` is known to have a [kernel panic issue](https://github.com/HazyResearch/deepdive/issues/522).

This implementation is used when `MKMIMO_IMPL=nonblocking`, and the following environment variables are parsed:

* `THROTTLE_SLEEP_MSEC` is the number of milliseconds to sleep between `poll(2)` system calls when no I/O streams were readable/writable.
    It defaults to `0`, meaning it won't sleep and rather busy wait for maximum throughput at the expense of processor time.

* `POLL_TIMEOUT_MSEC` is the number of milliseconds to wait until any I/O activity is picked up by the `poll(2)` system call.
    On Mac, it defaults to 1000 or one second, because `poll(2)` does not pick up close events timely.
    It defaults to `-1` on other OSes, which means `poll(2)` should wait indefinitely.

----

## Development Guide

### Build/Test Dependencies

The most accurate up-to-date details are always in the [Travis CI configuration](.travis.yml).

To run tests on Mac OS X, you need to install several packages including GNU coreutils and pv. They can be easily installed with [Homebrew](http://brew.sh):

```bash 
brew install coreutils pv
```

You may need to configure your PATH environment in your `.bash_profile`:

```bash 
PATH="$(brew --prefix coreutils)/libexec/gnubin:$PATH"
```

### Building

```bash
make  # or make mkmimo
```

#### Bash implementation

To try the proof-of-concept written in Bash, use:
```bash
make MKMIMO_IMPL=bash mkmimo
```

### Running Tests

After everything is installed, you can run the tests using:

```bash 
make test
```

To test selectively, run:

```bash
make test   ONLY+=/path/to/bats/files
```

To exclude certain tests:  

```bash
make test EXCEPT+=/path/to/bats/files
```

To enumerate all tests:

```bash
make test-list
```

### Formatting Code

To format all code:

```bash
make format
```

To specify which `clang-format` to use:

```bash
make format CLANG_FORMAT=clang-format-3.7
```
