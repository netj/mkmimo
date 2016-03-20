#!/usr/bin/env bash
# less-efficient mkmimo in plain shell that should always work
set -euo pipefail
shopt -s nullglob

: ${MKMIMO_TMPDIR:=/tmp}
: ${MKMIMO_BUFFER_MAX_RECORDS:=10000}
: ${MKMIMO_BUFFER_MAX_POOLED:=10}
: ${THROTTLE_SLEEP_USEC:=1}

MKMIMO_SESSION=$(mktemp -d "${MKMIMO_TMPDIR:-/tmp}"/mkmimo.XXXXXXX)
trap 'rm -rf "$MKMIMO_SESSION"' EXIT
export MKMIMO_BUFFER_MAX_POOLED
export THROTTLE_SLEEP_SEC=$(bc <<<"oscale=7; $THROTTLE_SLEEP_USEC / 1000000")

# parse input/output arguments
inputs=() outputs=()
seenSeparator=false
for arg; do
    case $arg in
        '>') seenSeparator=true ;;
        *)
            if $seenSeparator
            then outputs+=("$arg")
            else inputs+=("$arg")
            fi
    esac
done
# treat arguments as outputs by default
$seenSeparator || [[ ${#inputs[@]} -eq 0 ]] || outputs=("${inputs[@]}") inputs=()
# default to stdin -- stdout if no arguments are given
[[  ${#inputs[@]} -gt 0 ]] ||  inputs+=(/dev/stdin)
[[ ${#outputs[@]} -gt 0 ]] || outputs+=(/dev/stdout)

# set up producers (sources)
inputProcs=() i=0
for input in "${inputs[@]}"; do
    # prepare its own buffer pool
    pool="$MKMIMO_SESSION/i-$i"
    mkdir -p "$pool"
    {
        export pool
        # produce buffers from sources into its own pool
        split --suffix-length=0 --lines=$MKMIMO_BUFFER_MAX_RECORDS --filter='
            cd "$pool"
            cat >."$FILE"
            mv -f ."$FILE" "$FILE"
            while set -- *; [[ $# -gt $MKMIMO_BUFFER_MAX_POOLED ]]; do sleep $THROTTLE_SLEEP_SEC; done  # this back pressures
        ' -
        # mark as done
        touch "$MKMIMO_SESSION"/eof-$i
    } <"$input" &
    inputProcs+=($!)
    let ++i
done

# set up consumers (sinks)
outputProcs=() j=0
numInputs=${#inputs[@]}
for output in "${outputs[@]}"; do
    {
        cd "$MKMIMO_SESSION"
        # grab one from any input
        while :; do
            gotAnyBuffer=false
            for pool in i-*; do
                set -- "$pool"/*
                gotBuffer=false
                for FILE; do
                    mv -f "$FILE" o-"$j" 2>/dev/null || continue
                    gotBuffer=true
                    gotAnyBuffer=true
                    break
                done
                if $gotBuffer; then
                    cat o-"$j"
                fi
            done
            # if didn't get any buffer
            if ! $gotAnyBuffer; then
                set -- eof-*
                # remove empty pools for finished inputs
                for marker; do rmdir i-${marker#eof-} 2>/dev/null || true; done
                # check if all inputs are done
                if [[ $# -eq $numInputs ]]; then
                    set -- i-*
                    # terminate if no pools remain
                    [[ $# -gt 0 ]] || break
                fi
            fi
        done
    } >"$output" &
    let ++j
done

wait
