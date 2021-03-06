#!/usr/bin/env coffee
# mkmimo -- Create a multiple inputs/multiple outputs
# > mkmimo [INPUT... \>] OUTPUT...
# Reads lines from available INPUTs and writes to available OUTPUTs without
# blocking.
#
# This utility is ideal for connecting a set of parallel processes, whose
# inputs and outputs are exposed via named pipes created with mkfifo(1), or
# with bash process substitution.
##
# Author: Jaeho Shin <netj@cs.stanford.edu>
# Created: 2015-03-03

fs = require "fs"
byline = require "byline"
args = process.argv[2..]

PUMP_IDLE_MS = 100 #ms

# find input and output paths from arguments
ioSepIdx = args.indexOf ">"
if ioSepIdx > 0
    inputPaths = args[0...ioSepIdx]
    outputPaths = args[(ioSepIdx+1)..]
else
    inputPaths = ["/dev/stdin"]
    outputPaths = args
if outputPaths.length == 0
    outputPaths = ["/dev/stdout"]

# open input and output paths
inputStreams =
    for path in inputPaths
        byline fs.createReadStream path
outputStreams =
    for path in outputPaths
        fs.createWriteStream path

# callbacks for maintaining stream states
canReadFrom = []
canWriteTo = (yes for output in outputStreams)
numInputsClosed = 0
numReadableInputs = 0
numWritableOutputs = outputStreams.length
canRead = (i) -> ->  canReadFrom[i] = yes; ++numReadableInputs
canWrite = (j) -> -> canWriteTo[j] = yes; ++numWritableOutputs
closedInput = (i) -> -> ++numInputsClosed
# and the main pumping function
j = -1
pump = ->
    if numWritableOutputs > 0 and numReadableInputs > 0
        for input,i in inputStreams when canReadFrom[i]
            line = input.read()
            if line?  # write to next available output
                for k in [0...outputStreams.length]
                    j = (j + 1) % outputStreams.length
                    continue unless canWriteTo[j]
                    output = outputStreams[j]
                    output.write line
                    unless output.write "\n"
                        # mark output as full
                        canWriteTo[j] = no
                        --numWritableOutputs
                    break
                break unless numWritableOutputs > 0
            else  # mark input as drained
                canReadFrom[i] = no
                --numReadableInputs
    # repeat pumping until all inputs are closed
    if numInputsClosed == inputStreams.length
        output.end() for output in outputStreams
    else if numWritableOutputs > 0 and numReadableInputs > 0
        setImmediate pump
    else
        setTimeout pump, PUMP_IDLE_MS

# read lines from inputs
for stream,i in inputStreams
    stream.on "readable", canRead i
    stream.on "end", closedInput i
    stream.on "close", closedInput i
for stream,j in outputStreams
    stream.on "drain", canWrite j
do pump
