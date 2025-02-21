#ifndef NUCLEUS_MEASUREMENT_H
#define NUCLEUS_MEASUREMENT_H

// #define SPINPOLL // TODO: configure this
#define SAMPLE_INTERVAL_MS 10 // Only used if SPINPOLL is not set
#ifdef SPINPOLL
#define EWMA_EXP 5
#else
#define EWMA_EXP 4
#endif

#define LOCAL_NUMA 1
#define WORKER_BUDGET 1000000

#define WALK_COMPLETED_BP 0x0208    // DTLB_LOAD_MISSES.WALK_COMPLETED_4K
#define WALK_COMPLETED_HP 0x0408    // DTLB_LOAD_MISSES.WALK_COMPLETED_2M_4M
#define DTLB_LOADS 0x81D0           // MEM_INST_RETIRED.ALL_LOADS

enum nucleus_events {
    WALK_COMPLETED_BP_EVENT = 0,
    WALK_COMPLETED_HP_EVENT = 1,
    DTLB_LOADS_EVENT = 2,
    N_NUCLEUS_EVENTS
};

static inline __attribute__((always_inline)) unsigned long rdtscp(void)
{
   unsigned long a, d, c;

   __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));

   return (a | (d << 32));
}

extern int terminate_mon;
int nucleus_measurement_init(void);
void nucleus_measurement_exit(void);

#endif
