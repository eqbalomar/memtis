#!/bin/bash

bench_cmd_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
workspace_dir=${workspace_dir:-"$(cd "$bench_cmd_dir/../../.." && pwd)"}
BIN=${nucleus_path:-"$workspace_dir/nucleus"}/apps_new
gups_cores=16
duration=1200
wss=64
num_bps_accessed=1

BENCH_DRAM="16GB"

BENCH_RUN="${BIN}/gups-x0-access-r ${gups_cores} ${duration} ${wss} ${num_bps_accessed}"

export BENCH_RUN
export BENCH_DRAM
