# Trace nucleus-mon
addr_lat_local=$(cat /proc/kallsyms | grep smoothed_lat_local | awk '{print "0x"$1}')
addr_lat_remote=$(cat /proc/kallsyms | grep smoothed_lat_remote | awk '{print "0x"$1}')
addr_occ_local=$(cat /proc/kallsyms | grep smoothed_occ_local | awk '{print "0x"$1}')
addr_occ_remote=$(cat /proc/kallsyms | grep smoothed_occ_remote | awk '{print "0x"$1}')
addr_inserts_local=$(cat /proc/kallsyms | grep smoothed_inserts_local | awk '{print "0x"$1}')
addr_inserts_remote=$(cat /proc/kallsyms | grep smoothed_inserts_remote | awk '{print "0x"$1}')
# addr_p_lo=$(cat /proc/kallsyms | grep p_lo | grep colloid | awk '{print "0x"$1}')
# addr_p_hi=$(cat /proc/kallsyms | grep p_hi | grep colloid | awk '{print "0x"$1}')

addr_t_lat_hp=$(cat /proc/kallsyms | grep smoothed_t_lat_hp | awk '{print "0x"$1}')
addr_t_lat_bp=$(cat /proc/kallsyms | grep smoothed_t_lat_bp | awk '{print "0x"$1}')
addr_walk_completed=$(cat /proc/kallsyms | grep smoothed_walk_completed | awk '{print "0x"$1}')
addr_walk_completed_inst=$(cat /proc/kallsyms | grep walk_completed | grep -v smoothed | awk '{print "0x"$1}')
addr_all_loads=$(cat /proc/kallsyms | grep smoothed_all_loads | grep nucleus_mon | awk '{print "0x"$1}')
addr_all_loads_inst=$(cat /proc/kallsyms | grep all_loads | grep -v smoothed | grep nucleus_mon | awk '{print "0x"$1}')

# bpftrace -e "BEGIN {@start = nsecs;} interval:s:1 {printf(\"%ld, colloid_local_lat_gt_remote: %d, local_occ: %lu, remote_occ: %lu, local_inserts: %lu, remote_inserts: %lu, p_lo: %lu, p_hi: %lu, delta_p=%lu, dynlimit=%lu\n\", (nsecs-@start)/1e9, *kaddr(\"colloid_local_lat_gt_remote\"), *($addr_occ_local), *($addr_occ_remote), *($addr_inserts_local), *($addr_inserts_remote), *($addr_p_lo), *($addr_p_hi), *kaddr(\"colloid_delta_p\"), *kaddr(\"colloid_dynlimit\"));}"
bpftrace -e "BEGIN {@start = nsecs;} interval:s:1 {
    printf(\"%ld, local_lat: %lu.%lu, remote_lat: %lu.%lu, local_occ: %lu, remote_occ: %lu, local_inserts: %lu, remote_inserts: %lu\n\", (nsecs-@start)/1e9, *($addr_lat_local)/10000, *($addr_lat_local)%10000, *($addr_lat_remote)/10000, *($addr_lat_remote)%10000, *($addr_occ_local), *($addr_occ_remote), *($addr_inserts_local), *($addr_inserts_remote));
    printf(\"%ld, hp_t_lat: %lu.%lu, bp_t_lat: %lu.%lu, smoothed_walk_completed: %lu, smoothed_all_loads: %lu, walk_completed: %lu, all_loads: %lu\n\", (nsecs-@start)/1e9, *($addr_t_lat_hp)/10000, *($addr_t_lat_hp)%10000, *($addr_t_lat_bp)/10000, *($addr_t_lat_bp)%10000, *($addr_walk_completed)*100, *($addr_all_loads)*100, *($addr_walk_completed_inst)*100, *($addr_all_loads_inst)*100);
}"
