#!/usr/bin/env bats
# Test if mkmimo shows reasonable throughput on corner cases
load test_helpers

num_producer_fast=20

@test "a single slow consumer should not hurt overall throughput (takes 5s)" {
    type pv &>/dev/null || skip "pv unavailable"
    expected_throughput=100000000  # 100MB/s
    nsecs=5
    nbytes=$(sum_dd_bytes \
        mkmimo_throughput \
            timeout=${nsecs}s \
            num_producer_fast=$num_producer_fast num_producer_slow=0 \
            num_consumer_fast=0 num_consumer_slow=1 \
            producer_slow_throughput=ignored \
            consumer_slow_throughput=${expected_throughput} \
            #
    )
    throughput=$(bc <<<"$nbytes / $nsecs")
    throughputSatisfactionPercentExpr="100 * $throughput / $expected_throughput"
    echo $throughputSatisfactionPercentExpr
    throughputSatisfactionPercent=$(bc <<<"$throughputSatisfactionPercentExpr")
    [[ $throughputSatisfactionPercent -ge 80 ]]
}

@test "slow consumer should not hurt fast consumer's throughput (takes 5-20s)" {
    type pv &>/dev/null || skip "pv unavailable"
    let expected_throughput=$(sum_dd_bytes \
        sh -c 'timeout 1s dd if=/dev/zero 2>/dev/null | dd of=/dev/null'
    )
    slow_throughput=16k
    nsecs=4
    nbytes=$(sum_dd_bytes \
        timeout $(($nsecs * 4)) \
        mkmimo_throughput \
            timeout=${nsecs}s \
            num_producer_fast=$num_producer_fast num_producer_slow=0 \
            num_consumer_fast=1 num_consumer_slow=1 \
            producer_slow_throughput=ignored \
            consumer_fast_throughput=${expected_throughput} \
            consumer_slow_throughput=${slow_throughput} \
            #
    )
    throughput=$(bc <<<"$nbytes / $nsecs")
    throughputSatisfactionPercentExpr="100 * $throughput / $expected_throughput"
    echo $throughputSatisfactionPercentExpr
    throughputSatisfactionPercent=$(bc <<<"$throughputSatisfactionPercentExpr")
    [[ $throughputSatisfactionPercent -ge 50 ]]
}
