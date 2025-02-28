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

static struct task_struct *knucleusmergerd = NULL;

static int nucleus_merger(void *data)
{
	unsigned long flags, hp_addr;
    struct nucleus_merge_request *req, *req_tmp;
    struct nucleus_hugepage *hp;
    struct page *hpage;
	int target_node;

    while (!kthread_should_stop()) {
		pr_info("nucleus_merger: processing merge requests\n");
		spin_lock_irqsave(&nucleus_merge_queue.request_queue_lock, flags);
		list_for_each_entry_safe(req, req_tmp, &nucleus_merge_queue.request_queue, list) {
			hp = req->hp;
			target_node = req->target_node;
			// pr_info("nucleus_merger: merge hp %lx in node %d\n", hp->address, target_node);

            hpage = NULL;
            hp_addr = hp->address << HPAGE_PMD_SHIFT;
            mmap_read_lock(hp->mm);
            collapse_huge_page(hp->mm, hp_addr, &hpage, target_node, 0, 0);
            // collapse_huge_page will release mm lock

			atomic_dec(&hp->ref_count);
			list_del(&req->list);
			kfree(req);
		}
		spin_unlock_irqrestore(&nucleus_merge_queue.request_queue_lock, flags);
		pr_info("nucleus_merger: processed merge requests\n");

        msleep_interruptible(5000);
    }
    return 0;
}

int nucleus_merger_init(void)
{
    int err = 0;
    pr_info("nucleus_merger: init\n");
    knucleusmergerd = kthread_run(nucleus_merger, NULL, "knucleusmergerd");
    if (IS_ERR(knucleusmergerd)) {
        pr_err("nucleus_merger: failed to create kernel thread\n");
        err = PTR_ERR(knucleusmergerd);
        knucleusmergerd = NULL;
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
