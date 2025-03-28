#!/bin/bash
BENCH_BIN=/home/omar/memtis/memtis-userspace/bench_dir/liblinear-multicore-2.48

# anon footprint 79640MB
# file footprint 21581MB

BENCH_RUN="taskset -c 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31 ${BENCH_BIN}/train -s 6 -m 16 ${BENCH_BIN}/datasets/kdd12"
# Liblinear requires a dataset file (kdd12)
# Please refer to memtis-userspace/bench_dir/README.md for downloading this dataset
BENCH_DRAM="23000MB"

# if [[ "x${NVM_RATIO}" == "x1:16" ]]; then
#     BENCH_DRAM="4150MB"
# elif [[ "x${NVM_RATIO}" == "x1:8" ]]; then
#     BENCH_DRAM="8000MB"
# elif [[ "x${NVM_RATIO}" == "x1:4" ]]; then
#     BENCH_DRAM="14128MB"
# elif [[ "x${NVM_RATIO}" == "x1:2" ]]; then
#     BENCH_DRAM="23000MB"
# elif [[ "x${NVM_RATIO}" == "x1:1" ]]; then
#     BENCH_DRAM="35320MB"
# elif [[ "x${NVM_RATIO}" == "x1:0" ]]; then
#     BENCH_DRAM="80000MB"
# fi


export BENCH_RUN
export BENCH_DRAM
