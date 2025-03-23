#!/bin/bash

TARGET=$1

rm -f ${TARGET}/memory_stat.txt
rm -f ${TARGET}/hotness_stat.txt
rm -f ${TARGET}/pgmig.txt


while :
do
    cat /sys/fs/cgroup/htmm/memory.stat | grep -e anon_thp -e anon >> ${TARGET}/memory_stat.txt
    cat /sys/fs/cgroup/htmm/memory.hotness_stat >> ${TARGET}/hotness_stat.txt
    cat /proc/vmstat | grep pgmigrate_su >> ${TARGET}/pgmig.txt
    sleep 1
done
