#!/bin/bash

bench_cmd_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
workspace_dir=${workspace_dir:-"$(cd "$bench_cmd_dir/../../.." && pwd)"}
BIN=${nucleus_path:-"$workspace_dir/nucleus"}/apps
gups_cores=16
duration=1200
wss=128

BENCH_DRAM="64GB"

BENCH_RUN="${BIN}/gups-skewed-r ${gups_cores} ${duration} ${wss}"

export BENCH_RUN
export BENCH_DRAM
