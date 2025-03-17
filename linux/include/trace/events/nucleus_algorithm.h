#undef TRACE_SYSTEM
#define TRACE_SYSTEM nucleus_algorithm

#if !defined(_TRACE_NUCLEUS_ALGORITHM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NUCLEUS_ALGORITHM_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(nucleus_algorithm,
   TP_PROTO(unsigned long loads_local, unsigned long loads_remote, unsigned long to_split, unsigned long to_merge, unsigned long to_promote, unsigned long to_demote,
      unsigned long init_time_ms, unsigned long pack_bp_time_ms, unsigned long sort_hp_time_ms, unsigned long process_hp_time_ms, unsigned long enqueue_req_time_ms, unsigned long cleanup_time_ms),
   TP_ARGS(loads_local, loads_remote, to_split, to_merge, to_promote, to_demote, init_time_ms, pack_bp_time_ms, sort_hp_time_ms, process_hp_time_ms, enqueue_req_time_ms, cleanup_time_ms),
   TP_STRUCT__entry(
      __field(unsigned long, loads_local)
      __field(unsigned long, loads_remote)
      __field(unsigned long, to_split)
      __field(unsigned long, to_merge)
      __field(unsigned long, to_promote)
      __field(unsigned long, to_demote)
      __field(unsigned long, init_time_ms)
      __field(unsigned long, pack_bp_time_ms)
      __field(unsigned long, sort_hp_time_ms)
      __field(unsigned long, process_hp_time_ms)
      __field(unsigned long, enqueue_req_time_ms)
      __field(unsigned long, cleanup_time_ms)
   ),
   TP_fast_assign(
      __entry->loads_local = loads_local;
      __entry->loads_remote = loads_remote;
      __entry->to_split = to_split;
      __entry->to_merge = to_merge;
      __entry->to_promote = to_promote;
      __entry->to_demote = to_demote;
      __entry->init_time_ms = init_time_ms;
      __entry->pack_bp_time_ms = pack_bp_time_ms;
      __entry->sort_hp_time_ms = sort_hp_time_ms;
      __entry->process_hp_time_ms = process_hp_time_ms;
      __entry->enqueue_req_time_ms = enqueue_req_time_ms;
      __entry->cleanup_time_ms = cleanup_time_ms;
   ),
   TP_printk("[nucleus_algorithm] loads_local=%lu, loads_remote=%lu, to_split=%lu, to_merge=%lu, to_promote=%lu, to_demote=%lu, init_time_ms=%lu, pack_bp_time_ms=%lu, sort_hp_time_ms=%lu, process_hp_time_ms=%lu, enqueue_req_time_ms=%lu, cleanup_time_ms=%lu",
      __entry->loads_local, __entry->loads_remote, __entry->to_split, __entry->to_merge, __entry->to_promote, __entry->to_demote,
      __entry->init_time_ms, __entry->pack_bp_time_ms, __entry->sort_hp_time_ms, __entry->process_hp_time_ms, __entry->enqueue_req_time_ms, __entry->cleanup_time_ms)
);

TRACE_EVENT(nucleus_access_freqs,
   TP_PROTO(unsigned long *hp_index, unsigned long *hp_freq),
   TP_ARGS(hp_index, hp_freq),
   TP_STRUCT__entry(
      __array(unsigned long, hp_index, 8)
      __array(unsigned long, hp_freq, 8)
   ),
   TP_fast_assign(
      memcpy(__entry->hp_index, hp_index, sizeof(unsigned long) * 8);
      memcpy(__entry->hp_freq, hp_freq, sizeof(unsigned long) * 8);
   ),
   TP_printk("[nucleus_algorithm] hp_index=%s hp_freq=%s",
      __print_array(__entry->hp_index, 8, sizeof(unsigned long)),
      __print_array(__entry->hp_freq, 8, sizeof(unsigned long)))
);

#endif /* _TRACE_NUCLEUS_ALGORITHM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
