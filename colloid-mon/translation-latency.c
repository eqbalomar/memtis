#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/perf_event.h>
#include <linux/htmm.h>
#include "nucleus_measurement.h"

#define CORE_MON_PERF 61

void thread_fun_poll_perf(struct work_struct *);
struct workqueue_struct *poll_perf_queue;
struct work_struct poll_perf;

extern int app_num_cores;

static u64 *event_val_curr[N_NUCLEUS_EVENTS];
static u64 *event_val_prev[N_NUCLEUS_EVENTS];
static u64 *event_val_diff[N_NUCLEUS_EVENTS];

static u64 event_val_total[N_NUCLEUS_EVENTS];

static u64 prev_tsc = 0;
static u64 curr_tsc = 0;

u64 walk_completed;
u64 smoothed_walk_completed;

u64 prev_loads;
u64 curr_loads;
u64 all_loads;
u64 smoothed_all_loads;

u64 prev_loads_local;
u64 curr_loads_local;
u64 loads_local;
u64 smoothed_loads_local;

u64 prev_loads_remote;
u64 curr_loads_remote;
u64 loads_remote;
u64 smoothed_loads_remote;

extern int terminate_mon;
extern unsigned long nucleus_all_loads;
extern unsigned long nucleus_loads_local;
extern unsigned long nucleus_loads_remote;
extern unsigned long smoothed_lat_local;

extern unsigned long smoothed_t_lat_hp;
extern unsigned long smoothed_t_lat_bp;

static struct perf_event **nucleus_mon_events[N_NUCLEUS_EVENTS];

static unsigned long get_perf_event_config(enum nucleus_events e) {
    switch (e) {
        case WALK_COMPLETED_EVENT:
            return WALK_COMPLETED;
        default:
            return N_NUCLEUS_EVENTS;
    }
}

static struct perf_event *create_perf_event(unsigned long config, int core) {
    struct perf_event_attr pe_attr;
    struct perf_event *event;
    int err;

    memset(&pe_attr, 0, sizeof(struct perf_event_attr));
    pe_attr.type = PERF_TYPE_RAW;
    pe_attr.size = sizeof(struct perf_event_attr);
    pe_attr.config = config;
    pe_attr.disabled = 0;
    pe_attr.exclude_kernel = 1;
    pe_attr.exclude_hv = 1;

    event = perf_event_create_kernel_counter(&pe_attr, core, NULL, NULL, NULL);
    if (IS_ERR(event)) {
        err = PTR_ERR(event);
        pr_err("nucleus_mon: error creating perf event for core %d, error: %d\n", core, err);
        return NULL;
    }

    return event;
}

static void perf_init(void) {
    int event, core, perf_event_config;
    int cores[32] = {1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,37,39,41,43,45,47,49,51,53,55,57,59,61,63};

    pr_info("nucleus_mon: perf init");

    for (event = 0; event < N_NUCLEUS_EVENTS; event++) {
        perf_event_config = get_perf_event_config(event);
        if (perf_event_config == N_NUCLEUS_EVENTS) {
            pr_err("nucleus_mon: error getting perf event config for event %d\n", event);
            continue;
        }
        for (core = 0; core < app_num_cores; core++) {
            nucleus_mon_events[event][core] = create_perf_event(perf_event_config, cores[core]);
            if (!nucleus_mon_events[event][core]) {
                pr_err("nucleus_mon: error creating perf event for event %d, core %d\n", event, core);
                return;
            }
        }
    }
}

static u64 sample_perf_event_counter(struct perf_event *event) {
    u64 event_val, enabled = 0, running = 0;
    if (!event) {
        return -1;
    }
    event_val = perf_event_read_value(event, &enabled, &running);
    // pr_info("nucleus_mon: event_val %llu enabled %llu running %llu", event_val, enabled, running);
    return event_val;
}

void thread_fun_poll_perf(struct work_struct *work) {
    // pr_info("nucleus thread_fun_poll_perf");
    int event, core;
    u32 budget = WORKER_BUDGET;
    u64 t_lat_bp, t_lat_hp;
    
    while (budget) {
        curr_tsc = rdtscp();
        if (curr_tsc < prev_tsc + SAMPLE_INTERVAL_MS * cpu_khz) {
            budget--;
            continue;
        }
        prev_tsc = curr_tsc;

        curr_loads = READ_ONCE(nucleus_all_loads);
        WRITE_ONCE(all_loads, curr_loads - prev_loads);
        WRITE_ONCE(smoothed_all_loads, (all_loads + ((1<<EWMA_EXP_PERF) - 1)*smoothed_all_loads)>>EWMA_EXP_PERF);
        prev_loads = curr_loads;

        curr_loads_local = READ_ONCE(nucleus_loads_local);
        WRITE_ONCE(loads_local, curr_loads_local - prev_loads_local);
        WRITE_ONCE(smoothed_loads_local, (loads_local + ((1<<EWMA_EXP_PERF) - 1)*smoothed_loads_local)>>EWMA_EXP_PERF);
        prev_loads_local = curr_loads_local;

        curr_loads_remote = READ_ONCE(nucleus_loads_remote);
        WRITE_ONCE(loads_remote, curr_loads_remote - prev_loads_remote);
        WRITE_ONCE(smoothed_loads_remote, (loads_remote + ((1<<EWMA_EXP_PERF) - 1)*smoothed_loads_remote)>>EWMA_EXP_PERF);
        prev_loads_remote = curr_loads_remote;

        for (event = 0; event < N_NUCLEUS_EVENTS; event++) {
            event_val_total[event] = 0;
            for (core = 0; core < app_num_cores; core++) {
                event_val_curr[event][core] = sample_perf_event_counter(nucleus_mon_events[event][core]);
                event_val_diff[event][core] = event_val_curr[event][core] - event_val_prev[event][core];

                event_val_total[event] += event_val_diff[event][core];

                event_val_prev[event][core] = event_val_curr[event][core];
            }
            // pr_info("nucleus_mon: event %d, total %llu", event, event_val_total[event]);
            if (event == WALK_COMPLETED_EVENT) {
                WRITE_ONCE(walk_completed, event_val_total[event]);
            }
        }

        WRITE_ONCE(smoothed_walk_completed, (walk_completed + ((1<<EWMA_EXP_PERF) - 1)*smoothed_walk_completed)>>EWMA_EXP_PERF);

        t_lat_hp = 3 * smoothed_lat_local;
        if (smoothed_all_loads > 0 && smoothed_walk_completed < smoothed_all_loads) {
            t_lat_hp = (smoothed_walk_completed * 3 * smoothed_lat_local) / smoothed_all_loads;
        }
        WRITE_ONCE(smoothed_t_lat_hp, t_lat_hp);

        t_lat_bp = 4 * smoothed_lat_local;
        if (smoothed_all_loads > 0 && smoothed_walk_completed < smoothed_all_loads) {
            t_lat_bp = (smoothed_walk_completed * 4 * smoothed_lat_local) / smoothed_all_loads;
        }
        WRITE_ONCE(smoothed_t_lat_bp, t_lat_bp);

        budget--;
    }
    if (!READ_ONCE(terminate_mon)) {
        queue_work_on(CORE_MON_PERF, poll_perf_queue, &poll_perf);
    } else {
        return;
    }
}

int nucleus_measurement_init(void)
{
    int event;
    pr_info("nucleus_mon: measurement init");
    poll_perf_queue = alloc_workqueue("poll_perf_queue",  WQ_HIGHPRI | WQ_CPU_INTENSIVE, 0);
    if (!poll_perf_queue) {
        pr_err("nucleus_mon: failed to create Perf workqueue\n");
        return -ENOMEM;
    }

    for (event = 0; event < N_NUCLEUS_EVENTS; event++) {
        nucleus_mon_events[event] = kzalloc(sizeof(struct perf_event *) * app_num_cores, GFP_KERNEL);
        event_val_curr[event] = kzalloc(sizeof(u64) * app_num_cores, GFP_KERNEL);
        event_val_prev[event] = kzalloc(sizeof(u64) * app_num_cores, GFP_KERNEL);
        event_val_diff[event] = kzalloc(sizeof(u64) * app_num_cores, GFP_KERNEL);
        if (!nucleus_mon_events[event] || !event_val_curr[event] || !event_val_prev[event] || !event_val_diff[event]) {
            pr_err("nucleus_mon: failed to allocate memory for event %d\n", event);
            return -ENOMEM;
        }
        event_val_total[event] = 0;
    }

    INIT_WORK(&poll_perf, thread_fun_poll_perf);

    perf_init();

    queue_work_on(CORE_MON_PERF, poll_perf_queue, &poll_perf);

    return 0;
}
 
void nucleus_measurement_exit(void)
{
    int event, core;
    pr_info("nucleus_mon: measurement exit");

    for (event = 0; event < N_NUCLEUS_EVENTS; event++) {
        for (core = 0; core < app_num_cores; core++) {
            perf_event_release_kernel(nucleus_mon_events[event][core]);
        }
        kfree(nucleus_mon_events[event]);
        kfree(event_val_curr[event]);
        kfree(event_val_prev[event]);
        kfree(event_val_diff[event]);
    }

    flush_workqueue(poll_perf_queue);
    destroy_workqueue(poll_perf_queue);
}
