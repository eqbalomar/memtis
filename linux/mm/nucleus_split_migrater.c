#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/huge_mm.h>
#include <linux/mm_inline.h>
#include <linux/migrate.h>
#include <linux/htmm.h>
#include <linux/khugepaged.h>
#include <linux/nucleus.h>

struct deferred_nucleus_request_queue nucleus_split_queue = {
	.request_queue_lock = __SPIN_LOCK_UNLOCKED(nucleus_split_queue.request_queue_lock),
	.request_queue = LIST_HEAD_INIT(nucleus_split_queue.request_queue),
};
EXPORT_SYMBOL(nucleus_split_queue);

static struct task_struct *knucleussplitmigraterd = NULL;

#define NUM_NUMA_NODES 2

static pmd_t *mm_find_pmd(struct mm_struct *mm, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, address);
	if (!pgd_present(*pgd))
		goto out;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		goto out;

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		goto out;

	pmd = pmd_offset(pud, address);

	if (!pmd_present(*pmd))
		pmd = NULL;
out:
	return pmd;
}


static void check_failed_list(struct list_head *tmp, struct list_head *failed_list)
{
    struct page *page;
    while (!list_empty(tmp)) {
        page = lru_to_page(tmp);
        list_move(&page->lru, failed_list);

        if (!PageTransHuge(page)) {
            VM_WARN_ON(1);
        }

        if (PageLRU(page)) {
            if (!TestClearPageLRU(page)) {
                VM_WARN_ON(1);
            }
        }
    }
}

static unsigned int split_hugepages(void)
{
    unsigned long flags;
    LIST_HEAD(failed_list);
    struct list_head split_lists[NUM_NUMA_NODES];
    struct lruvec *lruvecs[NUM_NUMA_NODES];
    struct nucleus_split_request *req, *req_tmp;
    struct nucleus_hugepage *hp;
    struct page *page;
    struct lruvec *lruvec;
    pmd_t *pmd;
    unsigned long hp_addr;
    unsigned int split = 0;
    int i, node_id;
    bool skip_iso;
    for (i = 0; i < NUM_NUMA_NODES; i++) {
        INIT_LIST_HEAD(&split_lists[i]);
        lruvecs[i] = NULL;
    }

    spin_lock_irqsave(&nucleus_split_queue.request_queue_lock, flags);
    list_for_each_entry_safe(req, req_tmp, &nucleus_split_queue.request_queue, list) {
        LIST_HEAD(tmp);
        hp = req->hp;
        // pr_info("nucleus_split_migrater: split hp %lx\n", hp->address);
        hp_addr = hp->address << HPAGE_PMD_SHIFT;
        mmap_read_lock(hp->mm);
        pmd = mm_find_pmd(hp->mm, hp_addr);
        if (!pmd) {
            pr_info("nucleus_algorithm: hp %lx pmd not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        if (!pmd_trans_huge(*pmd)) {
            pr_info("nucleus_algorithm: hp %lx not pmd_trans_huge\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        skip_iso = false;

        page = pmd_page(*pmd);
        if (!page) {
            pr_info("nucleus_algorithm: hp %lx page not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }

        mmap_read_unlock(hp->mm);

        lruvec = mem_cgroup_page_lruvec(page);
        node_id = page_to_nid(page);
        if (node_id >= NUM_NUMA_NODES) {
            pr_info("nucleus_algorithm: hp %lx invalid node id %d\n", hp->address, node_id);
            goto free_req;
        }
        if (lruvecs[node_id] == NULL) {
            lruvecs[node_id] = lruvec;
        }

        if (!PageLRU(page)) {
            skip_iso = true;
            goto skip_isolation;
        }

        spin_lock_irq(&lruvec->lru_lock);
        if (!__isolate_lru_page_prepare(page, 0)) {
            spin_unlock_irq(&lruvec->lru_lock);
            goto free_req;
        }

        if (unlikely(!get_page_unless_zero(page))) {
            spin_unlock_irq(&lruvec->lru_lock);
            goto free_req;
        }

        if (!TestClearPageLRU(page)) {
            put_page(page);
            spin_unlock_irq(&lruvec->lru_lock); 
            goto free_req;
        }
        
        list_move(&page->lru, &tmp);
        update_lru_size(lruvec, page_lru(page), page_zonenum(page), -thp_nr_pages(page));
        spin_unlock_irq(&lruvec->lru_lock);

skip_isolation:
        if (skip_iso) {
            if (page->lru.next != LIST_POISON1 || page->lru.prev != LIST_POISON2) {
                goto free_req;
            }
            list_add(&page->lru, &tmp);
        }

        lock_page(page);

        if (!split_huge_page_to_list(page, &tmp)) {
            split++;
            list_splice(&tmp, &split_lists[node_id]);
        } else {
            check_failed_list(&tmp, &failed_list);
        }

        unlock_page(page);

free_req:
        atomic_dec(&hp->ref_count);
        list_del(&req->list);
        kfree(req);
    }

    putback_movable_pages(&failed_list);

    for (i = 0; i < NUM_NUMA_NODES; i++) {
        if (lruvecs[i] && !list_empty(&split_lists[i])) {
            pr_info("nucleus_split_migrater: putback split pages in node %d lruvec\n", i);
            putback_split_pages(&split_lists[i], lruvecs[i]);
        }
    }
    spin_unlock_irqrestore(&nucleus_split_queue.request_queue_lock, flags);

    return split;
}

static int nucleus_split_migrater(void *data)
{
    unsigned int split = 0;
    while (!kthread_should_stop()) {
		pr_info("nucleus_split_migrater: processing split requests\n");
		split = split_hugepages();
        pr_info("nucleus_split_migrater: processed split requests, split %u pages\n", split);

        msleep_interruptible(5000);
    }
    return 0;
}

int nucleus_split_migrater_init(void)
{
    int err = 0;
    pr_info("nucleus_split_migrater: init\n");
    knucleussplitmigraterd = kthread_run(nucleus_split_migrater, NULL, "knucleussplitmigraterd");
    if (IS_ERR(knucleussplitmigraterd)) {
        pr_err("nucleus_split_migrater: failed to create kernel thread\n");
        err = PTR_ERR(knucleussplitmigraterd);
        knucleussplitmigraterd = NULL;
    }
    return err;
}

void nucleus_split_migrater_exit(void)
{
    if (knucleussplitmigraterd) {
	    kthread_stop(knucleussplitmigraterd);
        knucleussplitmigraterd = NULL;
	}
    pr_info("nucleus_split_migrater: exit\n");
}
