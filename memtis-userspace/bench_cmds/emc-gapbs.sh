#!/bin/bash

BIN=/home/omar/gapbs

BENCH_RUN="taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 ${BIN}/pr -u 25 -n 128"
BENCH_DRAM="1300MB"

export BENCH_RUN
export BENCH_DRAM
