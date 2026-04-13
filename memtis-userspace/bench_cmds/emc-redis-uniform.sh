#!/bin/bash

BIN=/home/omar/memtis/memtis-userspace/bench_cmds/

NUM_KEYS=1000000
NUM_OPS=100000000

BENCH_RUN="bash ${BIN}/emc-redis-uniform-util.sh $NUM_KEYS $NUM_OPS"
BENCH_DRAM="4100MB"

export BENCH_RUN
export BENCH_DRAM
