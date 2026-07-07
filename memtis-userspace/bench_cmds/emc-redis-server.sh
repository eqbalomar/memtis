#!/bin/bash

REDIS_PATH=/home/omar/redis-7.4.0/src
MEMTIER_PATH=/home/omar/memtier_benchmark
OUTPUT_PATH=/home/omar/memtis/memtis-userspace/bench_cmds/redis_outputs

for i in $(seq 1 2 15)
do
    socket_path="/tmp/redis_$i.sock"
    taskset -c $i ${REDIS_PATH}/redis-server --save "" --appendonly no --port 0 --bind 127.0.0.1 --unixsocket $socket_path --unixsocketperm 755 &
done

wait
