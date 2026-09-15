#!/bin/bash
#
# Multi-tenant GUPS spawner: launches 2 independent gups-x0-access-mt-r
# instances ("app0" and "app1"), each pinned to its own disjoint 8-core
# range, mirroring the loop+taskset+background+wait pattern already used by
# emc-redis-server.sh for its 8 redis-server instances. Unlike redis (which
# is measured by a separate client-side script), each GUPS instance's own
# stdout *is* the per-second throughput signal, so it is redirected here
# into its own log file rather than left to interleave.

BIN=/home/arjun/nucleus/apps
OUTPUT_DIR=/home/arjun/memtis/memtis-userspace/bench_cmds/gups_mt_outputs

THREADS_PER_APP=8
DURATION_SEC=1800
WSS_GB_PER_APP=32
NUM_BPS_ACCESSED=1

APP0_TASKSET="1,3,5,7,9,11,13,15"
APP0_CORE_OFFSET=0
APP1_TASKSET="17,19,21,23,25,27,29,31"
APP1_CORE_OFFSET=8

mkdir -p $OUTPUT_DIR
rm -f $OUTPUT_DIR/app0.log $OUTPUT_DIR/app1.log $OUTPUT_DIR/app0.pid $OUTPUT_DIR/app1.pid

taskset -c $APP0_TASKSET ${BIN}/gups-x0-access-mt-r $THREADS_PER_APP $DURATION_SEC $WSS_GB_PER_APP $NUM_BPS_ACCESSED $APP0_CORE_OFFSET > $OUTPUT_DIR/app0.log 2>&1 &
echo $! > $OUTPUT_DIR/app0.pid
taskset -c $APP1_TASKSET ${BIN}/gups-x0-access-mt-r $THREADS_PER_APP $DURATION_SEC $WSS_GB_PER_APP $NUM_BPS_ACCESSED $APP1_CORE_OFFSET > $OUTPUT_DIR/app1.log 2>&1 &
echo $! > $OUTPUT_DIR/app1.pid

wait
