#!/bin/bash

BIN=/home/omar/gapbs

BENCH_RUN="taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 ${BIN}/pr -g 28"
BENCH_DRAM="12GB"

export BENCH_RUN
export BENCH_DRAM
