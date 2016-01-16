mkmimo [![Build Status](https://travis-ci.org/netj/mkmimo.svg?branch=master)](https://travis-ci.org/netj/mkmimo)
======

Creates a multiple-input multiple-output pipe that can basically connect many parallel processes with named pipes or bash process substitution to stream lines of text without blocking.

### Getting Started

For documentation, read through the bats tests in the "/test" folder.

To run tests on Mac OS X, you need to install several packages including GNU coreutils and pv. They can be easily installed with Homebrew (http://brew.sh):

```bash 
brew install coreutils pv
```

You may need to configure your PATH environment in your .bash_profile:

```bash 
PATH="$(brew --prefix coreutils)/libexec/gnubin:$PATH"
```

### Running Tests

After everything is installed, you can run the tests using:

```bash 
make tests
```

To test selectively, run:

```bash
make test   ONLY+=/path/to/bats/files
```

To exclude certain tests:  

```bash
make test EXCEPT+=/path/to/bats/files
```
