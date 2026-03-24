#undef TRACE_SYSTEM
#define TRACE_SYSTEM nucleus_access_freq

#if !defined(_TRACE_NUCLEUS_ACCESS_FREQ_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_NUCLEUS_ACCESS_FREQ_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(nucleus_hp_access_freqs,
   TP_PROTO(int iteration, unsigned long hp_addr, unsigned int *bp_index, unsigned long *bp_freq, unsigned long *bp_nr_access),
   TP_ARGS(iteration, hp_addr, bp_index, bp_freq, bp_nr_access),
   TP_STRUCT__entry(
      __field(int, iteration)
      __field(unsigned long, hp_addr)
      __array(unsigned int, bp_index, 16)
      __array(unsigned long, bp_freq, 16)
      __array(unsigned long, bp_nr_access, 16)
   ),
   TP_fast_assign(
      __entry->iteration = iteration;
      __entry->hp_addr = hp_addr;
      memcpy(__entry->bp_index, bp_index, sizeof(unsigned int) * 16);
      memcpy(__entry->bp_freq, bp_freq, sizeof(unsigned long) * 16);
      memcpy(__entry->bp_nr_access, bp_nr_access, sizeof(unsigned long) * 16);
   ),
   TP_printk("[nucleus] iteration=%d, hp_addr=%lu, bp_index=%s, bp_freq=%s, bp_nr_access=%s",
      __entry->iteration,
      __entry->hp_addr,
      __print_array(__entry->bp_index, 16, sizeof(unsigned int)),
      __print_array(__entry->bp_freq, 16, sizeof(unsigned long)),
      __print_array(__entry->bp_nr_access, 16, sizeof(unsigned long))
   )
);

TRACE_EVENT(nucleus_bp_access_freq,
   TP_PROTO(int iteration, unsigned long bp_addr, unsigned long bp_freq, unsigned long bp_nr_access),
   TP_ARGS(iteration, bp_addr, bp_freq, bp_nr_access),
   TP_STRUCT__entry(
      __field(int, iteration)
      __field(unsigned long, bp_addr)
      __field(unsigned long, bp_freq)
      __field(unsigned long, bp_nr_access)
   ),
   TP_fast_assign(
      __entry->iteration = iteration;
      __entry->bp_addr = bp_addr;
      __entry->bp_freq = bp_freq;
      __entry->bp_nr_access = bp_nr_access;
   ),
   TP_printk("[nucleus] iteration=%d, bp_addr=%lu, bp_freq=%lu, bp_nr_access=%lu",
      __entry->iteration,
      __entry->bp_addr,
      __entry->bp_freq,
      __entry->bp_nr_access
   )
);

#endif /* _TRACE_NUCLEUS_ACCESS_FREQ_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
