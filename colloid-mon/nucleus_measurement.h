#ifndef NUCLEUS_MEASUREMENT_H
#define NUCLEUS_MEASUREMENT_H

// #define MEASURE_FOR_MEMTIS

#define SPINPOLL // TODO: configure this
#define SAMPLE_INTERVAL_MS 10
#ifdef SPINPOLL
#define EWMA_EXP 11
#else
#define EWMA_EXP 4
#endif

#define EWMA_EXP_PERF 11

#define LOCAL_NUMA 1
#define WORKER_BUDGET 1000000

#define CHA_FREQ 2400000000ULL
#define LATENCY_PRECISION 10000ULL
#define N_SEC 1000000000ULL

#define WALK_COMPLETED 0x0E08       // DTLB_LOAD_MISSES.WALK_COMPLETED
#define WALK_COMPLETED_BP 0x0208    // DTLB_LOAD_MISSES.WALK_COMPLETED_4K
#define WALK_COMPLETED_HP 0x0408    // DTLB_LOAD_MISSES.WALK_COMPLETED_2M_4M
// #define DTLB_LOADS 0x81D0           // MEM_INST_RETIRED.ALL_LOADS
#define MEM_LOAD_RETIRED_L3_MISS 0x20D1     // MEM_LOAD_RETIRED.L3_MISS
// #define LONGEST_LAT_CACHE_MISS 0x412E     // LONGEST_LAT_CACHE.MISS

enum nucleus_events {
    WALK_COMPLETED_EVENT = 0,
    // WALK_COMPLETED_EVENT_HP = 0,
    // WALK_COMPLETED_EVENT_BP = 1,
    L3_MISS_EVENT = 1,
    N_NUCLEUS_EVENTS
};

static inline __attribute__((always_inline)) unsigned long rdtscp(void)
{
   unsigned long a, d, c;

   __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));

   return (a | (d << 32));
}

int nucleus_measurement_init(void);
void nucleus_measurement_exit(void);

#endif
