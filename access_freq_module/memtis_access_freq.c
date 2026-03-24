#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/htmm.h>
#include <linux/list.h>
#include <linux/vmalloc.h>

#define CREATE_TRACE_POINTS

#include <trace/events/nucleus_access_freq.h>

int pid = -1;
module_param(pid, int, 0);
static struct task_struct *kaccessfreqd = NULL;

static int iteration = 0;

static void process_lruvec(struct mem_cgroup *memcg, pg_data_t *pgdat, struct mm_struct *mm, int node_id) {
    struct lruvec *lruvec = mem_cgroup_lruvec(memcg, pgdat);
    enum lru_list lru;
    struct list_head *lru_list;
    unsigned long bp_addr, hp_addr, accesses, nr_accesses;
    struct page *page;
    pginfo_t *pginfo;
    int i, idx, offset;
    unsigned int bp_index[HPAGE_PMD_NR];
    unsigned long bp_freq[HPAGE_PMD_NR];
    unsigned long bp_nr_access[HPAGE_PMD_NR];

    pr_info("process_lruvec: processing node %d\n", node_id);

    for_each_lru(lru) {
        lru_list = &lruvec->lists[lru];
        pr_info("process_lruvec: processing LRU %d for node %d\n", lru, node_id);
        list_for_each_entry(page, lru_list, lru) {
            bp_addr = page_to_pfn(page) << PAGE_SHIFT;
            // pr_info("process_lruvec: processing page at address 0x%lx for node %d\n", bp_addr, node_id);
            if (PageTransHuge(page)) {
                hp_addr = bp_addr >> HPAGE_PMD_SHIFT;
                // pr_info("process_lruvec: processing HP at address 0x%lx for node %d\n", bp_addr, node_id);
                
                for (i = 0; i < HPAGE_PMD_NR; i++) {
                    idx = 4 + i / 4;
                    offset = i % 4;
                    pginfo = &(page[idx].compound_pginfo[offset]);
                    accesses = pginfo->total_accesses;
                    nr_accesses = pginfo->nr_accesses;
                    bp_index[i] = i;
                    bp_freq[i] = accesses;
                    bp_nr_access[i] = nr_accesses;
                    // if (i == 0) {
                    //     pr_info("process_lruvec: HP page at address 0x%lx has %lu accesses for node %d\n", bp_addr, accesses, node_id);
                    // }
                }
                for (i = 0; i < HPAGE_PMD_NR; i+=16) {
                    trace_nucleus_hp_access_freqs(iteration, hp_addr, bp_index+i, bp_freq+i, bp_nr_access+i);
                }
            } else {
                pte_t *pte, ptent;
                spinlock_t *ptl;
                struct page *pte_page;

                pgd_t *pgd;
                p4d_t *p4d;
                pud_t *pud;
                pmd_t *pmd;

                pgd = pgd_offset(mm, bp_addr);
                if (!pgd_present(*pgd)) {
                    // pr_err("process_lruvec: pgd not present for address 0x%lx\n", bp_addr);
                    // break;
                    continue;
                }
                
                p4d = p4d_offset(pgd, bp_addr);
                if (!p4d_present(*p4d)) {
                    // pr_err("process_lruvec: p4d not present for address 0x%lx\n", bp_addr);
                    // break;
                    continue;
                }
                
                pud = pud_offset(p4d, bp_addr);
                if (!pud_present(*pud)) {
                    // pr_err("process_lruvec: pud not present for address 0x%lx\n", bp_addr);
                    // break;
                    continue;
                }

                pmd = pmd_offset(pud, bp_addr);
                if (!pmd_present(*pmd)) {
                    // pr_err("process_lruvec: pmd not present for address 0x%lx\n", bp_addr);
                    // break;
                    continue;
                }

                pte = pte_offset_map_lock(mm, pmd, bp_addr, &ptl);
                ptent = *pte;
                if (!pte_present(ptent)) {
                    // pr_err("process_lruvec: pte not present for address 0x%lx\n", bp_addr);
                    pte_unmap_unlock(pte, ptl);
                    // break;
                    goto pte_unlock;
                }

                pte_page = virt_to_page((unsigned long)pte);
                if (!PageHtmm(pte_page)) {
                    // pr_err("process_lruvec: page not htmm for address 0x%lx\n", bp_addr);
                    pte_unmap_unlock(pte, ptl);
                    // break;
                    goto pte_unlock;
                }

                pginfo = get_pginfo_from_pte(pte);
                if (!pginfo) {
                    // pr_err("process_lruvec: failed to get pginfo for address 0x%lx\n", bp_addr);
                    pte_unmap_unlock(pte, ptl);
                    // break;
                    goto pte_unlock;
                }

                accesses = pginfo->total_accesses;
                nr_accesses = pginfo->nr_accesses;
                trace_nucleus_bp_access_freq(iteration, bp_addr, accesses, nr_accesses);
pte_unlock:
                pte_unmap_unlock(pte, ptl);
            }

        }
    }
}

static int memtis_access_freq(void *data) {
    pg_data_t *local_pgdat, *remote_pgdat;
    struct mem_cgroup *memcg;
    struct file *f;
    struct pid *pid_struct;
    struct task_struct *p;
    struct mm_struct *mm;

    int i = 0;
    while (!kthread_should_stop()) {
        local_pgdat = NODE_DATA(HTMM_CXL_LOCAL_NUMA);
        remote_pgdat = NODE_DATA(HTMM_CXL_REMOTE_NUMA);
        if (!local_pgdat) {
            pr_err("memtis_access_freq: failed to get local pgdat\n");
            goto next_iteration;
        }
        if (!remote_pgdat) {
            pr_err("memtis_access_freq: failed to get remote pgdat\n");
            goto next_iteration;
        }

        if (pid < 0) {
            pr_err("memtis_access_freq: pid not set %d\n", pid);
            goto next_iteration;
        }

        pid_struct = find_get_pid(pid);
        p = pid_struct ? pid_task(pid_struct, PIDTYPE_PID) : NULL;
        if (!p) {
            pr_err("memtis_access_freq: failed to get task_struct for pid %d\n", pid);
            goto next_iteration;
        }
        mm = p->mm;
        if (!mm) {
            pr_err("memtis_access_freq: failed to get mm_struct for pid %d\n", pid);
            goto put_task;
        }

        mmap_read_lock(mm);
        
        memcg = get_mem_cgroup_from_mm(mm);
        if (!memcg || !memcg->htmm_enabled) {
            pr_err("memtis_access_freq: failed to get memcg or htmm not enabled\n");
            goto mmap_unlock;
        }

        pr_info("memtis_access_freq: processing pid %d for local NUMA node\n", pid);
        process_lruvec(memcg, local_pgdat, mm, HTMM_CXL_LOCAL_NUMA);
        pr_info("memtis_access_freq: processing pid %d for remote NUMA node\n", pid);
        process_lruvec(memcg, remote_pgdat, mm, HTMM_CXL_REMOTE_NUMA);
        pr_info("memtis_access_freq: finished processing pid %d\n", pid);

mmap_unlock:
    mmap_read_unlock(mm);
put_task:
    put_pid(pid_struct);

next_iteration:
        iteration++;
        msleep_interruptible(30000);
    }
    return 0;
}

static int memtis_access_freq_init(void)
{
    int err = 0;
    const struct cpumask *cpumask = cpumask_of_node(HTMM_CXL_LOCAL_NUMA);
    pr_info("memtis_access_freq_init: init\n");
    kaccessfreqd = kthread_run(memtis_access_freq, NULL, "kaccessfreq");
    if (IS_ERR(kaccessfreqd)) {
        pr_err("memtis_access_freq_init: failed to create kernel thread\n");
        err = PTR_ERR(kaccessfreqd);
        kaccessfreqd = NULL;
    } else {
        set_cpus_allowed_ptr(kaccessfreqd, cpumask);
    }
    return err;
}

static void memtis_access_freq_exit(void)
{
    if (kaccessfreqd) {
        kthread_stop(kaccessfreqd);
        kaccessfreqd = NULL;
	}
}
 
module_init(memtis_access_freq_init);
module_exit(memtis_access_freq_exit);
MODULE_AUTHOR("Omar");
MODULE_LICENSE("GPL");
