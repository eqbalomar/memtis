#!/bin/bash
#
# Multi-tenant GUPS spawner: launches 2 independent gups-x0-access-mt-r
# instances, each pinned to its own 8 core set
BIN=/home/arjun/nucleus/apps_new
OUTPUT_DIR=/home/arjun/memtis/memtis-userspace/bench_cmds/gups_mt_outputs

DURATION_SEC=900

APP0_CORE_OFFSET=0
APP1_CORE_OFFSET=8

mkdir -p $OUTPUT_DIR
rm -f $OUTPUT_DIR/app0.log $OUTPUT_DIR/app1.log $OUTPUT_DIR/app0.pid $OUTPUT_DIR/app1.pid

taskset -c $APP0_CORES ${BIN}/gups-x0-access-mt-r $APP0_THREADS $DURATION_SEC $APP0_WSS_GB $APP0_NUM_BPS $APP0_CORE_OFFSET > $OUTPUT_DIR/app0.log 2>&1 &
echo $! > $OUTPUT_DIR/app0.pid
sleep ${APP_START_DELAY_S:-0}
taskset -c $APP1_CORES ${BIN}/gups-x0-access-mt-r $APP1_THREADS $DURATION_SEC $APP1_WSS_GB $APP1_NUM_BPS $APP1_CORE_OFFSET > $OUTPUT_DIR/app1.log 2>&1 &
echo $! > $OUTPUT_DIR/app1.pid

wait
