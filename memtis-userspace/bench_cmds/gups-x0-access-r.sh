#!/bin/bash

BIN=/home/omar/nucleus/apps
gups_cores=1
duration=1000
wss=16

BENCH_DRAM="1GB"

BENCH_RUN="${BIN}/gups-x0-access-r ${gups_cores} ${duration} ${wss}"

export BENCH_RUN
export BENCH_DRAM
