#!/bin/bash

BIN=/home/arjun/nucleus/apps
gups_cores=16
duration=300
wss=64
num_bps_accessed=1

BENCH_DRAM="32GB"

BENCH_RUN="${BIN}/gups-x0-access-r ${gups_cores} ${duration} ${wss} ${num_bps_accessed}"

export BENCH_RUN
export BENCH_DRAM
