#!/bin/bash
#
# BENCH_NAME entry point for the 2-app multi-tenant GUPS experiment.
# Mirrors emc-redis-uniform.sh: BENCH_RUN just points at the wrapper script
# that fans out into the real (multiple) app processes.

BIN=/home/arjun/memtis/memtis-userspace/bench_cmds

BENCH_RUN="bash ${BIN}/gups-mt-2app-server.sh"
BENCH_DRAM="32GB"

export BENCH_RUN
export BENCH_DRAM
