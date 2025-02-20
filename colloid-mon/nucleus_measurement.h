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

extern int terminate_mon;
int nucleus_measurement_init(void);
void nucleus_measurement_exit(void);

#endif
