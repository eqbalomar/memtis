#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/delay.h>
#include <linux/htmm.h>
#include <linux/khugepaged.h>
#include <linux/nucleus.h>

#include <trace/events/nucleus.h>

struct deferred_nucleus_request_queue nucleus_merge_queues[NUM_MERGE_THREADS];
EXPORT_SYMBOL(nucleus_merge_queues);

DECLARE_WAIT_QUEUE_HEAD(nucleus_merge_wait);
EXPORT_SYMBOL(nucleus_merge_wait);

atomic_t nucleus_process_merge[NUM_MERGE_THREADS] = ATOMIC_INIT(0);
EXPORT_SYMBOL(nucleus_process_merge);

atomic_t nucleus_merge_queues_initialized = ATOMIC_INIT(0);
EXPORT_SYMBOL(nucleus_merge_queues_initialized);

#define NUCLEUS_MERGER_TIMEOUT 5000 // 5 seconds

static struct task_struct *knucleusmergerd[NUM_MERGE_THREADS] = {NULL};

static bool has_merge_requests(int thread_id)
{
    if (atomic_read(&nucleus_process_merge[thread_id]) > 0) {
        return true;
    }
    return false;
}

static void check_and_demote_file_pages(int thread_id) {
    unsigned long nr_taken_file = 0, nr_demoted_file = 0;
    struct mem_cgroup *memcg;
    struct mem_cgroup_per_node *pn;
    struct lruvec *lruvec = NULL;
    pg_data_t *local_pgdat;
    LIST_HEAD(demotion_list);

    local_pgdat = NODE_DATA(HTMM_CXL_LOCAL_NUMA);

    pn = next_memcg_cand(local_pgdat);
	if (!pn) {
		return;
	}

    memcg = pn->memcg;
    if (!memcg || !memcg->htmm_enabled) {
        return;
    }
    if (!memcg->nodeinfo || !memcg->nodeinfo[HTMM_CXL_LOCAL_NUMA]) {
        return;
    }
    pr_info("nucleus_merger[%d]: demoting file pages\n", thread_id);
    lruvec = mem_cgroup_lruvec(memcg, local_pgdat);
    nr_taken_file = add_file_pages_to_demotion_list(lruvec, LRU_INACTIVE_FILE, &demotion_list);
    nr_taken_file += add_file_pages_to_demotion_list(lruvec, LRU_ACTIVE_FILE, &demotion_list);
    pr_info("nucleus_merger[%d]: added %lu file pages to demotion list\n", thread_id, nr_taken_file);
    if (!nr_taken_file) {
        return;
    }

    nr_demoted_file = migrate_page_list_safe(&demotion_list, local_pgdat, false);

    if (lruvec && nr_taken_file > 0) {
        spin_lock_irq(&lruvec->lru_lock);
        if (!list_empty(&demotion_list)) {
            move_pages_to_lru(lruvec, &demotion_list);
        }
        __mod_node_page_state(local_pgdat, NR_ISOLATED_FILE, -nr_taken_file);
        spin_unlock_irq(&lruvec->lru_lock);
    }

    pr_info("nucleus_merger[%d]: demoted %lu file pages\n", thread_id, nr_demoted_file);
}

static int nucleus_merger(void *data)
{
	unsigned long flags, hp_addr;
    struct nucleus_merge_request *req, *req_tmp;
    struct nucleus_hugepage *hp;
    struct page *hpage;
	int target_node, ret;
    unsigned long merged = 0, start_tsc, end_tsc, time_ms;
    int thread_id = (int)(unsigned long)data;
    pr_info("nucleus_merger[%d]: started\n", thread_id);

    while (!kthread_should_stop()) {
        ret = wait_event_interruptible_timeout(nucleus_merge_wait, has_merge_requests(thread_id), msecs_to_jiffies(NUCLEUS_MERGER_TIMEOUT));
        if (ret == 0) {
            // pr_info("nucleus_merger: timeout\n");
            continue;
        }
		pr_info("nucleus_merger[%d]: processing merge requests\n", thread_id);
        start_tsc = rdtscp();
        merged = 0;

        check_and_demote_file_pages(thread_id);

		spin_lock_irqsave(&nucleus_merge_queues[thread_id].request_queue_lock, flags);
		list_for_each_entry_safe(req, req_tmp, &nucleus_merge_queues[thread_id].request_queue, list) {
			hp = req->hp;
			target_node = req->target_node;
			// pr_info("nucleus_merger: merge hp %lx in node %d\n", hp->address, target_node);

            hpage = NULL;
            hp_addr = hp->address << HPAGE_PMD_SHIFT;
            if (!hp->mm) {
                // pr_info("nucleus_merger: hp %lx mm not found\n", hp->address);
                goto free_req;
            }
            mmap_read_lock(hp->mm);
            if (atomic_read(&hp->mm->mm_users) == 0) {
                // pr_info("nucleus_merger: hp %lx mm users 0\n", hp->address);
                mmap_read_unlock(hp->mm);
                goto free_req;
            }
            merged += collapse_huge_page(hp->mm, hp_addr, &hpage, target_node, 0, 0);
            if (!IS_ERR_OR_NULL(hpage)) {
                put_page(hpage);
            }
            // collapse_huge_page will release mm lock

free_req:
			atomic_dec(&hp->ref_count);
			list_del(&req->list);
			kfree(req);
		}
		spin_unlock_irqrestore(&nucleus_merge_queues[thread_id].request_queue_lock, flags);
        end_tsc = rdtscp();
        time_ms = (end_tsc - start_tsc) / cpu_khz;
        trace_nucleus_merge(merged, time_ms);
		pr_info("nucleus_merger[%d]: processed merge requests, merged %lu pages\n", thread_id, merged);
        atomic_set(&nucleus_process_merge[thread_id], 0);
    }
    pr_info("nucleus_merger[%d]: stopped\n", thread_id);
    return 0;
}

int nucleus_merger_init(void)
{
    int err = 0, i;
    const struct cpumask *cpumask_local = cpumask_of_node(HTMM_CXL_LOCAL_NUMA);
    pr_info("nucleus_merger: init\n");
    for (i = 0; i < NUM_MERGE_THREADS; i++) {
        spin_lock_init(&nucleus_merge_queues[i].request_queue_lock);
        INIT_LIST_HEAD(&nucleus_merge_queues[i].request_queue);
        knucleusmergerd[i] = kthread_run(nucleus_merger, (void *)(unsigned long)i, "knucleusmergerd");
        if (IS_ERR(knucleusmergerd[i])) {
            pr_err("nucleus_merger: failed to create kernel thread\n");
            err = PTR_ERR(knucleusmergerd[i]);
            knucleusmergerd[i] = NULL;
        } else {
            set_cpus_allowed_ptr(knucleusmergerd[i], cpumask_local);
        }
    }
    atomic_set(&nucleus_merge_queues_initialized, 1);
    return err;
}

void nucleus_merger_exit(void)
{
    int i;
    atomic_set(&nucleus_merge_queues_initialized, 0);
    for (i = 0; i < NUM_MERGE_THREADS; i++) {
        if (knucleusmergerd[i]) {
            kthread_stop(knucleusmergerd[i]);
            knucleusmergerd[i] = NULL;
        }
    }
    pr_info("nucleus_merger: exit\n");
}
