# Trace colloid-mon
addr_occ_local=$(cat /proc/kallsyms | grep smoothed_occ_local | awk '{print "0x"$1}')
addr_occ_remote=$(cat /proc/kallsyms | grep smoothed_occ_remote | awk '{print "0x"$1}')
addr_inserts_local=$(cat /proc/kallsyms | grep smoothed_inserts_local | awk '{print "0x"$1}')
addr_inserts_remote=$(cat /proc/kallsyms | grep smoothed_inserts_remote | awk '{print "0x"$1}')
addr_p_lo=$(cat /proc/kallsyms | grep p_lo | grep colloid | awk '{print "0x"$1}')
addr_p_hi=$(cat /proc/kallsyms | grep p_hi | grep colloid | awk '{print "0x"$1}')

bpftrace -e "BEGIN {@start = nsecs;} interval:s:1 {printf(\"%ld, colloid_local_lat_gt_remote: %d, local_occ: %lu, remote_occ: %lu, local_inserts: %lu, remote_inserts: %lu, p_lo: %lu, p_hi: %lu, delta_p=%lu, dynlimit=%lu\n\", (nsecs-@start)/1e9, *kaddr(\"colloid_local_lat_gt_remote\"), *($addr_occ_local), *($addr_occ_remote), *($addr_inserts_local), *($addr_inserts_remote), *($addr_p_lo), *($addr_p_hi), *kaddr(\"colloid_delta_p\"), *kaddr(\"colloid_dynlimit\"));}"