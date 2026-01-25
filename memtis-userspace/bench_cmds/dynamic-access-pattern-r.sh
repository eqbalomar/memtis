#!/bin/bash

BIN=/home/omar/nucleus/apps
gups_cores=16
duration=1400
wss=128

BENCH_DRAM="64GB"

BENCH_RUN="${BIN}/dynamic-access-pattern-r ${gups_cores} ${duration} ${wss}"

export BENCH_RUN
export BENCH_DRAM
