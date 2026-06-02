#!/bin/bash

BIN=/home/omar/memtis/memtis-userspace/bench_cmds/

NUM_KEYS=4000000
NUM_OPS=100000000

BENCH_RUN="bash ${BIN}/emc-redis-uniform-util.sh $NUM_KEYS $NUM_OPS"
BENCH_DRAM="4100MB"
# BENCH_DRAM="1300MB" # for 64B object size
# BENCH_DRAM="45MB" # for 40k keys, 384B object size
# BENCH_DRAM="1MB" # for 4k keys, 384B object size

export BENCH_RUN
export BENCH_DRAM
