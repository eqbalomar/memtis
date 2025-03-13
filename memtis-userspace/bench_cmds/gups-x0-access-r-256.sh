#!/bin/bash

BIN=/home/omar/nucleus/apps
gups_cores=16
duration=1000
wss=128
num_bps_accessed=256

BENCH_DRAM="64GB"

BENCH_RUN="${BIN}/gups-x0-access-r ${gups_cores} ${duration} ${wss} ${num_bps_accessed}"

export BENCH_RUN
export BENCH_DRAM
