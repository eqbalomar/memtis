#!/bin/bash

BIN=/home/omar/nucleus/apps
gups_cores=1
duration=1200
wss=128
num_bps_accessed=8

BENCH_DRAM="64GB"

BENCH_RUN="${BIN}/seq-x0-access-r ${gups_cores} ${duration} ${wss} ${num_bps_accessed}"

export BENCH_RUN
export BENCH_DRAM
