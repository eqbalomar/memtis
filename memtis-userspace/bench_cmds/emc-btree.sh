#!/bin/bash

BIN=/home/omar/vmitosis-workloads/bin
BENCH_RUN="taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 ${BIN}/bench_btree_mt"
BENCH_DRAM="21200MB"

export BENCH_RUN
export BENCH_DRAM
