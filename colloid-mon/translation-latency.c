#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/perf_event.h>
#include "nucleus_measurement.h"

#define CORE_MON_PERF 61

void thread_fun_poll_perf(struct work_struct *);
struct workqueue_struct *poll_perf_queue;
#ifdef SPINPOLL
struct work_struct poll_perf;
#else
DECLARE_DELAYED_WORK(poll_perf, thread_fun_poll_perf);
#endif

u64 walks_completed_bp, walks_completed_hp, dtlb_loads;
u64 walks_completed_bp_curr, walks_completed_hp_curr, dtlb_loads_curr;
u64 walks_completed_bp_prev, walks_completed_hp_prev, dtlb_loads_prev;

static struct perf_event *walks_completed_bp_event, *walks_completed_hp_event, *dtlb_loads_event;


static struct perf_event *create_perf_event(unsigned long config) {
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

    event = perf_event_create_kernel_counter(&pe_attr, 1, NULL, NULL, NULL);
    if (IS_ERR(event)) {
        err = PTR_ERR(event);
        pr_err("nucleus_mon: error creating perf event, error: %d\n", err);
        return NULL;
    }

    return event;
}

static void perf_init(void) {
    pr_info("nucleus_mon: perf init");
    walks_completed_bp_event = create_perf_event(0x0208);   // DTLB_LOAD_MISSES.WALK_COMPLETED_4K
    if (!walks_completed_bp_event) {
        pr_err("nucleus_mon: error creating walks_completed_bp_event\n");
        return;
    }
    walks_completed_hp_event = create_perf_event(0x0408);   // DTLB_LOAD_MISSES.WALK_COMPLETED_2M_4M
    if (!walks_completed_hp_event) {
        pr_err("nucleus_mon: error creating walks_completed_hp_event\n");
        return;
    }
    dtlb_loads_event = create_perf_event(0x81D0);  // MEM_INST_RETIRED.ALL_LOADS
    if (!dtlb_loads_event) {
        pr_err("nucleus_mon: error creating dtlb_loads_event\n");
        return;
    }
}

u64 sample_perf_event_counter(struct perf_event *event) {
    u64 event_val, *enabled, *running;
    if (!event)
        return -1;
    enabled = kmalloc(sizeof(u64), GFP_KERNEL);
    running = kmalloc(sizeof(u64), GFP_KERNEL);
    if (!enabled || !running) {
        pr_err("nucleus_mon: failed to allocate memory for enabled or running variables\n");
        kfree(enabled);
        kfree(running);
        perf_event_release_kernel(event);
        return -1;
    }
    *enabled = 0;
    *running = 0;
    event_val = perf_event_read_value(event, enabled, running);
    kfree(enabled);
    kfree(running);
    return event_val;
}

void thread_fun_poll_perf(struct work_struct *work) {
    // pr_info("nucleus thread_fun_poll_perf");
    int cpu = CORE_MON_PERF;
    #ifdef SPINPOLL
    u32 budget = WORKER_BUDGET;
    #else
    u32 budget = 1;
    #endif
    
    while (budget) {
        walks_completed_bp_curr = sample_perf_event_counter(walks_completed_bp_event);
        walks_completed_hp_curr = sample_perf_event_counter(walks_completed_hp_event);
        dtlb_loads_curr = sample_perf_event_counter(dtlb_loads_event);

        walks_completed_bp = walks_completed_bp_curr - walks_completed_bp_prev;
        walks_completed_hp = walks_completed_hp_curr - walks_completed_hp_prev;
        dtlb_loads = dtlb_loads_curr - dtlb_loads_prev;
        // pr_info("nucleus_mon: walks_completed_bp: %llu, walks_completed_hp: %llu, dtlb_loads: %llu", walks_completed_bp, walks_completed_hp, dtlb_loads);

        walks_completed_bp_prev = walks_completed_bp_curr;
        walks_completed_hp_prev = walks_completed_hp_curr;
        dtlb_loads_prev = dtlb_loads_curr;

        budget--;
    }
    if(!READ_ONCE(terminate_mon)){
        #ifdef SPINPOLL
        queue_work_on(cpu, poll_perf_queue, &poll_perf);
        #else
        queue_delayed_work_on(cpu, poll_perf_queue, &poll_perf, msecs_to_jiffies(SAMPLE_INTERVAL_MS));
        #endif
    }
    else{
        return;
    }
}

int nucleus_measurement_init(void)
{
    pr_info("nucleus_mon: measurement init");
    poll_perf_queue = alloc_workqueue("poll_perf_queue",  WQ_HIGHPRI | WQ_CPU_INTENSIVE, 0);
    if (!poll_perf_queue) {
        pr_err("nucleus_mon: failed to create Perf workqueue\n");
        return -ENOMEM;
    }

    walks_completed_bp_curr = 0;
    walks_completed_hp_curr = 0;
    dtlb_loads_curr = 0;
    walks_completed_bp_prev = 0;
    walks_completed_hp_prev = 0;
    dtlb_loads_prev = 0;

    #ifdef SPINPOLL
    INIT_WORK(&poll_perf, thread_fun_poll_perf);
    #else
    INIT_DELAYED_WORK(&poll_perf, thread_fun_poll_perf);
    #endif

    perf_init();

    #ifdef SPINPOLL
    queue_work_on(CORE_MON_PERF, poll_perf_queue, &poll_perf);
    #else
    queue_delayed_work_on(CORE_MON_PERF, poll_perf_queue, &poll_perf, msecs_to_jiffies(SAMPLE_INTERVAL_MS));
    #endif

    return 0;
}
 
void nucleus_measurement_exit(void)
{
    pr_info("nucleus_mon: measurement exit");

    perf_event_release_kernel(walks_completed_bp_event);
    perf_event_release_kernel(walks_completed_hp_event);
    perf_event_release_kernel(dtlb_loads_event);

    flush_workqueue(poll_perf_queue);
    destroy_workqueue(poll_perf_queue);
}
