#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/mm.h>
#include <linux/htmm.h>
#include <linux/delay.h>
#include "nucleus_measurement.h"

#ifdef MEASURE_FOR_MEMTIS
#define COLLOID_PRECISION 1000000000UL
int colloid_local_lat_gt_remote;
int colloid_nid_of_interest;
unsigned long colloid_delta_p;
unsigned long colloid_dynlimit;
#else
extern int colloid_local_lat_gt_remote;
extern int colloid_nid_of_interest;
extern unsigned long colloid_delta_p;
extern unsigned long colloid_dynlimit;
#endif

int app_num_cores = 1;
module_param(app_num_cores, int, 0);

#define CORE_MON 63
#define LOG_SIZE 10000
#define MIN_LOCAL_LAT 70
#define MIN_REMOTE_LAT 140
#define OCC_PRECISION 1000000UL
#define COLLOID_DELTA_PERCENT 5UL
#define COLLOID_EPSILON_PERCENT 1UL
#define MIGRATION_QUANTUM_MS 500

// CHA counters are MSR-based.  
//   The starting MSR address is 0x0E00 + 0x10*CHA
//   	Offset 0 is Unit Control -- mostly un-needed
//   	Offsets 1-4 are the Counter PerfEvtSel registers
//   	Offset 5 is Filter0	-- selects state for LLC lookup event (and TID, if enabled by bit 19 of PerfEvtSel)
//   	Offset 6 is Filter1 -- lots of filter bits, including opcode -- default if unused should be 0x03b, or 0x------33 if using opcode matching
//   	Offset 7 is Unit Status
//   	Offsets 8,9,A,B are the Counter count registers
#define CHA_MSR_PMON_BASE 0x0E00L
#define CHA_MSR_PMON_CTL_BASE 0x0E01L
#define CHA_MSR_PMON_FILTER0_BASE 0x0E05L
// #define CHA_MSR_PMON_FILTER1_BASE 0x0E06L // No FULERT1 on Icelake
#define CHA_MSR_PMON_STATUS_BASE 0x0E07L
#define CHA_MSR_PMON_CTR_BASE 0x0E08L

#define NUM_CHA_BOXES 18 // There are 32 CHA boxes in icelake server. After the first 18 boxes, the couter offsets change.
#define NUM_CHA_COUNTERS 4

#define NUM_CHA_SLICES 32

u64 smoothed_occ_local, smoothed_inserts_local;
u64 smoothed_occ_remote, smoothed_inserts_remote;
u64 p_lo, p_hi;
u64 nucleus_local_llc_misses;
u64 nucleus_remote_llc_misses;

u64 nucleus_local_llc_hits;
u64 nucleus_remote_llc_hits;

static u64 prev_tsc = 0;
static u64 curr_tsc = 0;

#ifdef MEASURE_FOR_MEMTIS
unsigned long smoothed_lat_local;
unsigned long smoothed_lat_remote;
#else
extern unsigned long smoothed_lat_local;
extern unsigned long smoothed_lat_remote;
#endif

void thread_fun_poll_cha(struct work_struct *);
struct workqueue_struct *poll_cha_queue;
#ifdef SPINPOLL
struct work_struct poll_cha;
#else
DECLARE_DELAYED_WORK(poll_cha, thread_fun_poll_cha);
#endif

u64 cur_ctr_tsc[NUM_CHA_BOXES][NUM_CHA_COUNTERS], prev_ctr_tsc[NUM_CHA_BOXES][NUM_CHA_COUNTERS];
u64 cur_ctr_val[NUM_CHA_BOXES][NUM_CHA_COUNTERS], prev_ctr_val[NUM_CHA_BOXES][NUM_CHA_COUNTERS];
int terminate_mon;

struct log_entry {
    u64 tsc;
    u64 occ_local;
    u64 inserts_local;
    u64 occ_remote;
    u64 inserts_remote;
};

struct log_entry log_buffer[LOG_SIZE];
int log_idx;

static void poll_cha_init(void) {
    int cha, ret;
    u32 msr_num;
    u64 msr_val;
    for(cha = 0; cha < NUM_CHA_BOXES; cha++) {
        msr_num = CHA_MSR_PMON_FILTER0_BASE + (0xE * cha); // Filter0
        msr_val = 0x00000000; // default; no filtering
        ret = wrmsr_on_cpu(CORE_MON, msr_num, msr_val & 0xFFFFFFFF, msr_val >> 32);
        if(ret != 0) {
            printk(KERN_ERR "wrmsr FILTER0 failed\n");
            return;
        }

        // msr_num = CHA_MSR_PMON_FILTER1_BASE + (0xE * cha); // Filter1
        // msr_val = (cha%2 == 0)?(0x40432):(0x40431); // Filter DRd of local/remote on even/odd CHA boxes
        // ret = wrmsr_on_cpu(CORE_MON, msr_num, msr_val & 0xFFFFFFFF, msr_val >> 32);
        // if(ret != 0) {
        //     printk(KERN_ERR "wrmsr FILTER1 failed\n");
        //     return;
        // }

        msr_num = CHA_MSR_PMON_CTL_BASE + (0xE * cha) + 0; // counter 0
        msr_val = (cha%2==0)?(0x00c8168600400136):(0x00c8170600400136); // TOR Occupancy, DRd, Miss, local/remote on even/odd CHA boxes
        ret = wrmsr_on_cpu(CORE_MON, msr_num, msr_val & 0xFFFFFFFF, msr_val >> 32);
        if(ret != 0) {
            printk(KERN_ERR "wrmsr COUNTER 0 failed\n");
            return;
        }

        msr_num = CHA_MSR_PMON_CTL_BASE + (0xE * cha) + 1; // counter 1
        msr_val = (cha%2==0)?(0x00c8168600400135):(0x00c8170600400135); // TOR Inserts, DRd, Miss, local/remote on even/odd CHA boxes
        ret = wrmsr_on_cpu(CORE_MON, msr_num, msr_val & 0xFFFFFFFF, msr_val >> 32);
        if(ret != 0) {
            printk(KERN_ERR "wrmsr COUNTER 1 failed\n");
            return;
        }

        msr_num = CHA_MSR_PMON_CTL_BASE + (0xE * cha) + 2; // counter 2
        msr_val = (cha%2==0)?(0x00c8168500400135):(0x00c8170500400135); // TOR Inserts, DRd, Hit, local/remote on even/odd CHA boxes
        ret = wrmsr_on_cpu(CORE_MON, msr_num, msr_val & 0xFFFFFFFF, msr_val >> 32);
        if(ret != 0) {
            printk(KERN_ERR "wrmsr COUNTER 2 failed\n");
            return;
        }
    }
    
}

static inline void sample_cha_ctr(int cha, int ctr) {
    u32 msr_num, msr_high, msr_low;
    msr_num = CHA_MSR_PMON_CTR_BASE + (0xE * cha) + ctr;    
    rdmsr_on_cpu(CORE_MON, msr_num, &msr_low, &msr_high);
    prev_ctr_val[cha][ctr] = cur_ctr_val[cha][ctr];
    cur_ctr_val[cha][ctr] = (((u64)msr_high) << 32) | msr_low;
    prev_ctr_tsc[cha][ctr] = cur_ctr_tsc[cha][ctr];
    cur_ctr_tsc[cha][ctr] = rdtscp();
}

static void dump_log(void) {
    int i;
    pr_info("Dumping colloid mon log");
    for(i = 0; i < LOG_SIZE; i++) {
        printk("%llu %llu %llu %llu %llu\n", log_buffer[i].tsc, log_buffer[i].occ_local, log_buffer[i].inserts_local, log_buffer[i].occ_remote, log_buffer[i].inserts_remote);
    }
}

void thread_fun_poll_cha(struct work_struct *work) {
    int cpu = CORE_MON;
    #ifdef SPINPOLL
    u32 budget = WORKER_BUDGET;
    #else
    u32 budget = 1;
    #endif
    u64 cur_occ, cur_inserts, cur_llc_misses, cur_llc_hits;
    u64 cur_lat_local, cur_lat_remote;
    u64 abs_diff, cur_p, target_p, dlimit;
    
    while (budget) {
        curr_tsc = rdtscp();
        if (curr_tsc < prev_tsc + SAMPLE_INTERVAL_MS * cpu_khz) {
            budget--;
            continue;
        }
        prev_tsc = curr_tsc;

        // Sample counters and update state
        // TODO: For starters using CHA0 for local and CHA1 for remote
        sample_cha_ctr(0, 0); // CHA0 occupancy
        sample_cha_ctr(0, 1); // CHA0 inserts
        sample_cha_ctr(0, 2); // CHA0 hits
        sample_cha_ctr(1, 0);
        sample_cha_ctr(1, 1);
        sample_cha_ctr(1, 2);

        cur_occ = cur_ctr_val[0][0] - prev_ctr_val[0][0];
        cur_inserts = cur_ctr_val[0][1] - prev_ctr_val[0][1];
        cur_llc_misses = cur_inserts * NUM_CHA_SLICES;
        cur_llc_hits = cur_ctr_val[0][2] - prev_ctr_val[0][2];
        WRITE_ONCE(nucleus_local_llc_misses, cur_llc_misses);
        WRITE_ONCE(nucleus_local_llc_hits, cur_llc_hits * NUM_CHA_SLICES);
        WRITE_ONCE(smoothed_occ_local, (cur_occ + ((1<<EWMA_EXP) - 1)*smoothed_occ_local)>>EWMA_EXP);
        WRITE_ONCE(smoothed_inserts_local, (cur_inserts + ((1<<EWMA_EXP) - 1)*smoothed_inserts_local)>>EWMA_EXP);
        cur_lat_local = MIN_LOCAL_LAT * LATENCY_PRECISION;
        if (smoothed_inserts_local > 0) {
            cur_lat_local = (smoothed_occ_local * LATENCY_PRECISION) / smoothed_inserts_local;
            cur_lat_local = (cur_lat_local * N_SEC) / CHA_FREQ;
        }
        WRITE_ONCE(smoothed_lat_local, cur_lat_local);
        // log_buffer[log_idx].tsc = cur_ctr_tsc[0][0];
        // log_buffer[log_idx].occ_local = cur_occ;
        // log_buffer[log_idx].inserts_local = cur_inserts;

        cur_occ = cur_ctr_val[1][0] - prev_ctr_val[1][0];
        cur_inserts = cur_ctr_val[1][1] - prev_ctr_val[1][1];
        cur_llc_misses = cur_inserts * NUM_CHA_SLICES;
        cur_llc_hits = cur_ctr_val[1][2] - prev_ctr_val[1][2];
        WRITE_ONCE(nucleus_remote_llc_misses, cur_llc_misses);
        WRITE_ONCE(nucleus_remote_llc_hits, cur_llc_hits * NUM_CHA_SLICES);
        WRITE_ONCE(smoothed_occ_remote, (cur_occ + ((1<<EWMA_EXP) - 1)*smoothed_occ_remote)>>EWMA_EXP);
        WRITE_ONCE(smoothed_inserts_remote, (cur_inserts + ((1<<EWMA_EXP) - 1)*smoothed_inserts_remote)>>EWMA_EXP);
        cur_lat_remote = MIN_REMOTE_LAT * LATENCY_PRECISION;
        if (smoothed_inserts_remote > 0) {
            cur_lat_remote = (smoothed_occ_remote * LATENCY_PRECISION) / smoothed_inserts_remote;
            cur_lat_remote = (cur_lat_remote * N_SEC) / CHA_FREQ;
        }
        WRITE_ONCE(smoothed_lat_remote, cur_lat_remote);
        // log_buffer[log_idx].occ_remote = cur_occ;
        // log_buffer[log_idx].inserts_remote = cur_inserts;
        

        // TODO: Handle the case when rates of either tier can be zero
        WRITE_ONCE(colloid_local_lat_gt_remote, (smoothed_occ_local*smoothed_inserts_remote > smoothed_occ_remote*smoothed_inserts_local));
        
        // Update delta_p
        if(!colloid_local_lat_gt_remote) {
            abs_diff = (smoothed_occ_remote*smoothed_inserts_local - smoothed_occ_local*smoothed_inserts_remote);
        } else {
            abs_diff = (smoothed_occ_local*smoothed_inserts_remote - smoothed_occ_remote*smoothed_inserts_local);
        }

        if(abs_diff <= (COLLOID_DELTA_PERCENT*smoothed_occ_local*smoothed_inserts_remote)/100) {
            WRITE_ONCE(colloid_delta_p, 0UL);
        } else {
            cur_p = (smoothed_inserts_local*COLLOID_PRECISION)/(smoothed_inserts_local+smoothed_inserts_remote);
            if(!colloid_local_lat_gt_remote) {
                p_lo = cur_p;
                if(p_hi <= p_lo) {
                    p_hi = 1UL*COLLOID_PRECISION;
                }
            } else {
                p_hi = cur_p;
                if(p_lo >= p_hi) {
                    p_lo = 0UL*COLLOID_PRECISION;
                }
            }

            if(p_hi - p_lo < (COLLOID_EPSILON_PERCENT * COLLOID_PRECISION)/100) {
                if(!colloid_local_lat_gt_remote) {
                    p_hi = 1UL*COLLOID_PRECISION;
                } else {
                    p_lo = 0UL*COLLOID_PRECISION;
                }
            }
            
            target_p = (p_lo + p_hi)/2;
            WRITE_ONCE(colloid_delta_p, (target_p >= cur_p)?(target_p-cur_p):(cur_p-target_p));
        }


        // Update dynlimit
        dlimit = colloid_delta_p * (smoothed_inserts_local + smoothed_inserts_remote) * NUM_CHA_BOXES;
        dlimit = dlimit / (COLLOID_PRECISION * 64UL);
        dlimit = dlimit * (MIGRATION_QUANTUM_MS/SAMPLE_INTERVAL_MS);
        WRITE_ONCE(colloid_dynlimit, dlimit);

        // log_idx = (log_idx+1)%LOG_SIZE;

        budget--;
    }
    if(!READ_ONCE(terminate_mon)){
        #ifdef SPINPOLL
        queue_work_on(cpu, poll_cha_queue, &poll_cha);
        #else
        queue_delayed_work_on(cpu, poll_cha_queue, &poll_cha, msecs_to_jiffies(SAMPLE_INTERVAL_MS));
        #endif
    }
    else{
        return;
    }
}

static void init_mon_state(void) {
    int cha, ctr;
    for(cha = 0; cha < NUM_CHA_BOXES; cha++) {
        for(ctr = 0; ctr < NUM_CHA_COUNTERS; ctr++) {
            cur_ctr_tsc[cha][ctr] = 0;
            cur_ctr_val[cha][ctr] = 0;
            sample_cha_ctr(cha, ctr);
        }
    }
    log_idx = 0;

    p_lo = 0UL*COLLOID_PRECISION;
    p_hi = 1UL*COLLOID_PRECISION;
}

static int colloidmon_init(void)
{
    poll_cha_queue = alloc_workqueue("poll_cha_queue",  WQ_HIGHPRI | WQ_CPU_INTENSIVE, 0);
    if (!poll_cha_queue) {
        printk(KERN_ERR "Failed to create CHA workqueue\n");
        return -ENOMEM;
    }

    #ifdef SPINPOLL
    INIT_WORK(&poll_cha, thread_fun_poll_cha);
    #else
    INIT_DELAYED_WORK(&poll_cha, thread_fun_poll_cha);
    #endif
    poll_cha_init();
    pr_info("Programmed counters");
    // Initialize state
    init_mon_state();
    WRITE_ONCE(terminate_mon, 0);
    #ifdef SPINPOLL
    queue_work_on(CORE_MON, poll_cha_queue, &poll_cha);
    #else
    queue_delayed_work_on(CORE_MON, poll_cha_queue, &poll_cha, msecs_to_jiffies(SAMPLE_INTERVAL_MS));
    #endif

    WRITE_ONCE(colloid_nid_of_interest, LOCAL_NUMA);

    nucleus_measurement_init();

    int i;
    for(i = 0; i < 5; i++) {
        msleep(1000);
        printk("%llu %llu\n", READ_ONCE(smoothed_occ_local), READ_ONCE(smoothed_occ_remote));
    }
    
    return 0;
}
 
static void colloidmon_exit(void)
{
    WRITE_ONCE(terminate_mon, 1);
    msleep(5000);
    flush_workqueue(poll_cha_queue);
    destroy_workqueue(poll_cha_queue);

    nucleus_measurement_exit();

    // dump_log();

    pr_info("colloidmon exit");
}
 
module_init(colloidmon_init);
module_exit(colloidmon_exit);
MODULE_AUTHOR("Midhul");
MODULE_LICENSE("GPL");
