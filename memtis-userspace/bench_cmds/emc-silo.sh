#!/bin/bash

BIN=/home/omar/memtis/memtis-userspace/bench_dir/silo/out-perf.masstree/benchmarks
BENCH_RUN="taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 ${BIN}/dbtest --verbose --bench ycsb --num-threads 16 --scale-factor 100000 --ops-per-worker=250000000 --slow-exit"
BENCH_DRAM="15200MB"

export BENCH_RUN
export BENCH_DRAM
