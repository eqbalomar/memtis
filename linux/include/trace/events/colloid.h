#undef TRACE_SYSTEM
#define TRACE_SYSTEM colloid

#if !defined(_TRACE_COLLOID_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_COLLOID_H

#include <linux/types.h>
#include <linux/tracepoint.h>

TRACE_EVENT(colloid_migrate,
        TP_PROTO(unsigned long nr_to_scan, unsigned long migrate_limit, unsigned long delta_p, unsigned long overall_accesses, int promotion, unsigned long nr_migrated),
        TP_ARGS(nr_to_scan, migrate_limit, delta_p, overall_accesses, promotion, nr_migrated),
        TP_STRUCT__entry(
           __field(unsigned long, nr_to_scan)
           __field(unsigned long, migrate_limit)
           __field(unsigned long, delta_p)
           __field(unsigned long, overall_accesses)
           __field(int, promotion)
           __field(unsigned long, nr_migrated)
        ),
        TP_fast_assign(
           __entry->nr_to_scan = nr_to_scan;
           __entry->migrate_limit = migrate_limit;
           __entry->delta_p = delta_p;
           __entry->overall_accesses = overall_accesses;
           __entry->promotion = promotion;
           __entry->nr_migrated = nr_migrated;
        ),

	TP_printk("[colloid migration] nr_to_scan=%lu,migrate_limit=%lu,delta_p=%lu,overall_accesses=%lu,promotion=%d,nr_migrated=%lu",
	    __entry->nr_to_scan, __entry->migrate_limit, __entry->delta_p, __entry->overall_accesses, __entry->promotion, __entry->nr_migrated)
);

#endif /* _TRACE_COLLOID_H */

/* This part must be outside protection */
#include <trace/define_trace.h>