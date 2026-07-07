#!/bin/bash

REDIS_PATH=/home/omar/redis-7.4.0/src
MEMTIER_PATH=/home/omar/memtier_benchmark
OUTPUT_PATH=/home/omar/memtis/memtis-userspace/bench_cmds/redis_outputs

num_keys=4000000
num_ops=100000000

sleep 10

pids_memtier_set=()
for i in $(seq 17 2 31)
do
    socket_path="/tmp/redis_$((i-16)).sock"
    numactl --membind 1 --physcpubind $i ${MEMTIER_PATH}/memtier_benchmark -S $socket_path --threads=1 --clients=1 --ratio=1:0 --distinct-client-seed -d 384 -R --key-pattern=P:P --key-minimum=1 --key-maximum=$num_keys --key-prefix="key-" --pipeline=128 --hide-histogram -n allkeys > $OUTPUT_PATH/redis-set-$((i-16)).log 2>&1 &
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
    numactl --membind 1 --physcpubind $i,$((i+16)) ${MEMTIER_PATH}/memtier_benchmark -S $socket_path --ratio=0:1 --threads=2 --clients=1 --randomize --distinct-client-seed -d 384 --key-pattern=R:R --key-minimum=1 --key-maximum=$num_keys --key-prefix="key-" --pipeline=128 --hide-histogram -n $num_ops > $OUTPUT_PATH/redis-get-$((i-16)).log 2>&1 &
    pids_memtier_get+=($!)
done

for pid in "${pids_memtier_get[@]}"
do
    wait $pid
done

sleep 2

killall redis-server
