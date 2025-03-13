#!/bin/bash

BIN=/home/omar/nucleus/apps
gups_cores=16
duration=1000
wss=128

BENCH_DRAM="64GB"

BENCH_RUN="${BIN}/gups-skewed-r ${gups_cores} ${duration} ${wss}"

export BENCH_RUN
export BENCH_DRAM
