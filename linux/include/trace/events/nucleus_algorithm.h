#undef TRACE_SYSTEM
#define TRACE_SYSTEM nucleus_algorithm

#if !defined(_TRACE_NUCLEUS_ALGORITHM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NUCLEUS_ALGORITHM_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(nucleus_algorithm,
   TP_PROTO(unsigned long to_split, unsigned long to_merge, unsigned long to_promote, unsigned long to_demote, unsigned long time_ms),
   TP_ARGS(to_split, to_merge, to_promote, to_demote, time_ms),
   TP_STRUCT__entry(
      __field(unsigned long, to_split)
      __field(unsigned long, to_merge)
      __field(unsigned long, to_promote)
      __field(unsigned long, to_demote)
      __field(unsigned long, time_ms)
   ),
   TP_fast_assign(
      __entry->to_split = to_split;
      __entry->to_merge = to_merge;
      __entry->to_promote = to_promote;
      __entry->to_demote = to_demote;
      __entry->time_ms = time_ms;
   ),
   TP_printk("[nucleus_algorithm] to_split=%lu, to_merge=%lu, to_promote=%lu, to_demote=%lu, time_ms=%lu", __entry->to_split, __entry->to_merge, __entry->to_promote, __entry->to_demote, __entry->time_ms)
);

#endif /* _TRACE_NUCLEUS_ALGORITHM_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
