#!/bin/bash

BIN=/home/omar/memtis/memtis-userspace/bench_cmds/

BENCH_RUN="bash ${BIN}/emc-redis-server.sh"
BENCH_DRAM="16384MB"

export BENCH_RUN
export BENCH_DRAM
