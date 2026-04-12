#!/bin/bash

REDIS_PATH=/home/omar/redis-7.4.0/src
MEMTIER_PATH=/home/omar/memtier_benchmark

num_keys=$1
num_ops=$2

${REDIS_PATH}/redis-server --save "" --appendonly no --port 0 --bind 127.0.0.1 --unixsocket /tmp/redis.sock --unixsocketperm 755 &

sleep 2

${MEMTIER_PATH}/memtier_benchmark -S /tmp/redis.sock --threads=1 --clients=1 --ratio=1:0 --distinct-client-seed -d 384 -R --key-pattern=P:P --key-maximum=$num_keys --pipeline=32 --hide-histogram -n allkeys

sleep 2

${MEMTIER_PATH}/memtier_benchmark -S /tmp/redis.sock --ratio=0:1 --threads=2 --clients=1 --distinct-client-seed -d 384 --key-pattern=Z:Z --key-zipf-exp=0.99 --key-maximum=$num_keys --pipeline=128 --hide-histogram -n $num_ops

sleep 2

killall redis-server
