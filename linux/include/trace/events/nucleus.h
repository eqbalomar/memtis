#undef TRACE_SYSTEM
#define TRACE_SYSTEM nucleus

#if !defined(_TRACE_NUCLEUS_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NUCLEUS_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(nucleus_split_in_memtis,
        TP_PROTO(unsigned long ehr, unsigned long rhr, unsigned long nr_records, unsigned int memcg_sum_util, unsigned int memcg_num_util, unsigned int avg_accesses_hp, unsigned int memcg_nr_split),
        TP_ARGS(ehr, rhr, nr_records, memcg_sum_util, memcg_num_util, avg_accesses_hp, memcg_nr_split),
        TP_STRUCT__entry(
           __field(unsigned long, ehr)
           __field(unsigned long, rhr)
           __field(unsigned long, nr_records)
           __field(unsigned int, memcg_sum_util)
           __field(unsigned int, memcg_num_util)
           __field(unsigned int, avg_accesses_hp)
           __field(unsigned int, memcg_nr_split)
        ),
        TP_fast_assign(
           __entry->ehr = ehr;
           __entry->rhr = rhr;
           __entry->nr_records = nr_records;
           __entry->memcg_sum_util = memcg_sum_util;
           __entry->memcg_num_util = memcg_num_util;
           __entry->avg_accesses_hp = avg_accesses_hp;
           __entry->memcg_nr_split = memcg_nr_split;
        ),

	    TP_printk("[nucleus] ehr=%lu,rhr=%lu,nr_records=%lu,memcg_sum_util=%u,memcg_num_util=%u,avg_accesses_hp=%u,memcg_nr_split=%u",
	        __entry->ehr, __entry->rhr, __entry->nr_records, __entry->memcg_sum_util, __entry->memcg_num_util, __entry->avg_accesses_hp, __entry->memcg_nr_split)
);

TRACE_EVENT(nucleus_stopsplit_in_memtis,
        TP_PROTO(unsigned long prev_dram_sampled, unsigned long temp_rhr),
        TP_ARGS(prev_dram_sampled, temp_rhr),
        TP_STRUCT__entry(
           __field(unsigned long, prev_dram_sampled)
           __field(unsigned long, temp_rhr)
        ),
        TP_fast_assign(
           __entry->prev_dram_sampled = prev_dram_sampled;
           __entry->temp_rhr = temp_rhr;
        ),

	    TP_printk("[nucleus] prev_dram_sampled=%lu,temp_rhr=%lu",
	        __entry->prev_dram_sampled, __entry->temp_rhr)
);

TRACE_EVENT(nucleus_split,
   TP_PROTO(unsigned long nr_split, unsigned long time_ms),
   TP_ARGS(nr_split, time_ms),
   TP_STRUCT__entry(
      __field(unsigned long, nr_split)
      __field(unsigned long, time_ms)
   ),
   TP_fast_assign(
      __entry->nr_split = nr_split;
      __entry->time_ms = time_ms;
   ),
   TP_printk("[nucleus] nr_split=%lu, time_ms=%lu", __entry->nr_split, __entry->time_ms)
);

TRACE_EVENT(nucleus_merge,
   TP_PROTO(unsigned long nr_merged, unsigned long time_ms),
   TP_ARGS(nr_merged, time_ms),
   TP_STRUCT__entry(
      __field(unsigned long, nr_merged)
      __field(unsigned long, time_ms)
   ),
   TP_fast_assign(
      __entry->nr_merged = nr_merged;
      __entry->time_ms = time_ms;
   ),
   TP_printk("[nucleus] nr_merged=%lu, time_ms=%lu", __entry->nr_merged, __entry->time_ms)
);

TRACE_EVENT(nucleus_migrate,
   TP_PROTO(unsigned long nr_promoted, unsigned long nr_demoted, unsigned long time_ms),
   TP_ARGS(nr_promoted, nr_demoted, time_ms),
   TP_STRUCT__entry(
      __field(unsigned long, nr_promoted)
      __field(unsigned long, nr_demoted)
      __field(unsigned long, time_ms)
   ),
   TP_fast_assign(
      __entry->nr_promoted = nr_promoted;
      __entry->nr_demoted = nr_demoted;
      __entry->time_ms = time_ms;
   ),
   TP_printk("[nucleus] nr_promoted=%lu, nr_demoted=%lu, time_ms=%lu", __entry->nr_promoted, __entry->nr_demoted, __entry->time_ms)
);

#endif /* _TRACE_NUCLEUS_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
