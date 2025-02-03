#!/bin/bash

BIN=/home/midhul/memtis/memtis-userspace/bench_dir/silo/out-perf.masstree/benchmarks
BENCH_RUN="taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29 ${BIN}/dbtest --verbose --bench ycsb --num-threads 15 --scale-factor 400005 --ops-per-worker=100000000 --slow-exit"
BENCH_DRAM=""

#####
# Silo ~59500MB memory footprint
#####

if [[ "x${NVM_RATIO}" == "x1:32" ]]; then
    BENCH_DRAM="1803MB"
elif [[ "x${NVM_RATIO}" == "x1:16" ]]; then
    BENCH_DRAM="3500MB"
elif [[ "x${NVM_RATIO}" == "x1:8" ]]; then
    BENCH_DRAM="6600MB"
elif [[ "x${NVM_RATIO}" == "x1:4" ]]; then
    BENCH_DRAM="11900MB"
elif [[ "x${NVM_RATIO}" == "x1:2" ]]; then
    BENCH_DRAM="19800MB"
elif [[ "x${NVM_RATIO}" == "x1:1" ]]; then
    BENCH_DRAM="29750MB"
elif [[ "x${NVM_RATIO}" == "x1:0" ]]; then
    BENCH_DRAM="70000MB"
fi

export BENCH_RUN
export BENCH_DRAM
