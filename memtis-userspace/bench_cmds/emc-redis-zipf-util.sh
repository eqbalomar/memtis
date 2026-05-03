#!/bin/bash

REDIS_PATH=/home/omar/redis-7.4.0/src
MEMTIER_PATH=/home/omar/memtier_benchmark
OUTPUT_PATH=/home/omar/memtis/memtis-userspace/bench_cmds/redis_outputs

num_keys=$1
num_ops=$2

for i in $(seq 1 2 15)
do
    socket_path="/tmp/redis_$i.sock"
    taskset -c $i ${REDIS_PATH}/redis-server --save "" --appendonly no --port 0 --bind 127.0.0.1 --unixsocket $socket_path --unixsocketperm 755 &
done

sleep 2

pids_memtier_set=()
for i in $(seq 17 2 31)
do
    socket_path="/tmp/redis_$((i-16)).sock"
    taskset -c $i ${MEMTIER_PATH}/memtier_benchmark -S $socket_path --threads=1 --clients=1 --ratio=1:0 --distinct-client-seed -d 384 -R --key-pattern=P:P --key-minimum=1 --key-maximum=$num_keys --pipeline=128 --hide-histogram -n allkeys > $OUTPUT_PATH/redis-set-$((i-16)).log 2>&1 &
    pids_memtier_set+=($!)
done

for pid in "${pids_memtier_set[@]}"
do
    wait $pid
done

sleep 2

pids_memtier_get=()
for i in $(seq 17 2 31)
do
    socket_path="/tmp/redis_$((i-16)).sock"
    taskset -c $i ${MEMTIER_PATH}/memtier_benchmark -S $socket_path --ratio=0:1 --threads=1 --clients=1 --distinct-client-seed -d 384 --key-pattern=Z:Z --key-zipf-exp=0.99 --key-minimum=1 --key-maximum=$num_keys --pipeline=128 --hide-histogram -n $num_ops > $OUTPUT_PATH/redis-get-$((i-16)).log 2>&1 &
    pids_memtier_get+=($!)
done

for pid in "${pids_memtier_get[@]}"
do
    wait $pid
done

sleep 2

killall redis-server
