#!/bin/bash

BIN=/home/omar/nucleus/apps_new
gups_cores=16
duration=1200
wss=64

BENCH_DRAM="32GB"

BENCH_RUN="${BIN}/gups-x0-access-diff-skew-r ${gups_cores} ${duration} ${wss}"

export BENCH_RUN
export BENCH_DRAM
