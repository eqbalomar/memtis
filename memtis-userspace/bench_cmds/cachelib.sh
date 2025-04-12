#!/bin/bash

BIN=/home/omar/CacheLib/opt/cachelib/bin
WORKLOAD=/home/omar/colloid/workloads
BENCH_RUN="taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 ${BIN}/cachebench --json_test_config ${WORKLOAD}/cachelib/kvcache_reg/config2.json --progress 1"
BENCH_DRAM="18000MB"

# if [[ "x${NVM_RATIO}" == "x1:32" ]]; then
#     BENCH_DRAM="1803MB"
# elif [[ "x${NVM_RATIO}" == "x1:16" ]]; then
#     BENCH_DRAM="3500MB"
# elif [[ "x${NVM_RATIO}" == "x1:8" ]]; then
#     BENCH_DRAM="6600MB"
# elif [[ "x${NVM_RATIO}" == "x1:4" ]]; then
#     BENCH_DRAM="11900MB"
# elif [[ "x${NVM_RATIO}" == "x1:2" ]]; then
#     BENCH_DRAM="19800MB"
# elif [[ "x${NVM_RATIO}" == "x1:1" ]]; then
#     BENCH_DRAM="29750MB"
# elif [[ "x${NVM_RATIO}" == "x1:0" ]]; then
#     BENCH_DRAM="70000MB"
# fi

export BENCH_RUN
export BENCH_DRAM
