#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/delay.h>
#include <linux/htmm.h>
#include <linux/khugepaged.h>
#include <linux/nucleus.h>

struct deferred_nucleus_request_queue nucleus_merge_queue = {
	.request_queue_lock = __SPIN_LOCK_UNLOCKED(nucleus_merge_queue.request_queue_lock),
	.request_queue = LIST_HEAD_INIT(nucleus_merge_queue.request_queue),
};
EXPORT_SYMBOL(nucleus_merge_queue);

DECLARE_WAIT_QUEUE_HEAD(nucleus_merge_wait);
EXPORT_SYMBOL(nucleus_merge_wait);

atomic_t nucleus_process_merge = ATOMIC_INIT(0);
EXPORT_SYMBOL(nucleus_process_merge);

#define NUCLEUS_MERGER_TIMEOUT 5000 // 5 seconds

static struct task_struct *knucleusmergerd = NULL;

static bool has_merge_requests(void)
{
    if (atomic_read(&nucleus_process_merge) > 0) {
        return true;
    }
    return false;
}

static int nucleus_merger(void *data)
{
	unsigned long flags, hp_addr;
    struct nucleus_merge_request *req, *req_tmp;
    struct nucleus_hugepage *hp;
    struct page *hpage;
	int target_node, ret;
    unsigned long merged = 0;

    while (!kthread_should_stop()) {
        ret = wait_event_interruptible_timeout(nucleus_merge_wait, has_merge_requests(), msecs_to_jiffies(NUCLEUS_MERGER_TIMEOUT));
        if (ret == 0) {
            // pr_info("nucleus_merger: timeout\n");
            continue;
        }
		pr_info("nucleus_merger: processing merge requests\n");
        merged = 0;
		spin_lock_irqsave(&nucleus_merge_queue.request_queue_lock, flags);
		list_for_each_entry_safe(req, req_tmp, &nucleus_merge_queue.request_queue, list) {
			hp = req->hp;
			target_node = req->target_node;
			// pr_info("nucleus_merger: merge hp %lx in node %d\n", hp->address, target_node);

            hpage = NULL;
            hp_addr = hp->address << HPAGE_PMD_SHIFT;
            if (!hp->mm) {
                pr_info("nucleus_merger: hp %lx mm not found\n", hp->address);
                goto free_req;
            }
            mmap_read_lock(hp->mm);
            merged += collapse_huge_page(hp->mm, hp_addr, &hpage, target_node, 0, 0);
            // collapse_huge_page will release mm lock

free_req:
			atomic_dec(&hp->ref_count);
			list_del(&req->list);
			kfree(req);
		}
		spin_unlock_irqrestore(&nucleus_merge_queue.request_queue_lock, flags);
		pr_info("nucleus_merger: processed merge requests, merged %lu pages\n", merged);
        atomic_set(&nucleus_process_merge, 0);
    }
    return 0;
}

int nucleus_merger_init(void)
{
    int err = 0;
    const struct cpumask *cpumask = cpumask_of_node(HTMM_CXL_LOCAL_NUMA);;
    pr_info("nucleus_merger: init\n");
    knucleusmergerd = kthread_run(nucleus_merger, NULL, "knucleusmergerd");
    if (IS_ERR(knucleusmergerd)) {
        pr_err("nucleus_merger: failed to create kernel thread\n");
        err = PTR_ERR(knucleusmergerd);
        knucleusmergerd = NULL;
    } else {
        set_cpus_allowed_ptr(knucleusmergerd, cpumask);
    }
    return err;
}

void nucleus_merger_exit(void)
{
    if (knucleusmergerd) {
	    kthread_stop(knucleusmergerd);
        knucleusmergerd = NULL;
	}
    pr_info("nucleus_merger: exit\n");
}
