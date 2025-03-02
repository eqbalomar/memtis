# Trace nucleus-mon

bpftrace -e "BEGIN {@start = nsecs;} interval:s:1 {
    printf(\"%ld %lu %lu %lu\n\", (nsecs-@start)/1e9, *kaddr(\"nucleus_all_loads\"), *kaddr(\"nucleus_loads_local\"), *kaddr(\"nucleus_loads_remote\"));
}"
