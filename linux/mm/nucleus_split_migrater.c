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

struct deferred_nucleus_request_queue nucleus_migrate_queue = {
    .request_queue_lock = __SPIN_LOCK_UNLOCKED(nucleus_migrate_queue.request_queue_lock),
    .request_queue = LIST_HEAD_INIT(nucleus_migrate_queue.request_queue),
};
EXPORT_SYMBOL(nucleus_migrate_queue);

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
        if (!hp->mm) {
            pr_info("nucleus_split_migrater: hp %lx mm not found\n", hp->address);
            goto free_req;
        }
        mmap_read_lock(hp->mm);
        pmd = mm_find_pmd(hp->mm, hp_addr);
        if (!pmd) {
            pr_info("nucleus_split_migrater: hp %lx pmd not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        if (!pmd_trans_huge(*pmd)) {
            pr_info("nucleus_split_migrater: hp %lx not pmd_trans_huge\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        skip_iso = false;

        page = pmd_page(*pmd);
        if (!page) {
            pr_info("nucleus_split_migrater: hp %lx page not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }

        mmap_read_unlock(hp->mm);

        lruvec = mem_cgroup_page_lruvec(page);
        node_id = page_to_nid(page);
        if (node_id >= NUM_NUMA_NODES) {
            pr_info("nucleus_split_migrater: hp %lx invalid node id %d\n", hp->address, node_id);
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

static void migrate_hugepages_and_basepages(unsigned int *promoted, unsigned int *demoted)
{
    unsigned long flags;
    LIST_HEAD(promotion_list);
    LIST_HEAD(demotion_list);
    struct lruvec *lruvecs[NUM_NUMA_NODES] = {NULL};
    struct nucleus_migrate_request *req, *req_tmp;
    struct nucleus_hugepage *hp;
    struct nucleus_basepage *bp;
    struct page *page;
    struct lruvec *lruvec;
    pg_data_t *local_pgdat, *remote_pgdat;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long hp_addr, bp_addr;
    unsigned int nr_taken[NUM_NUMA_NODES] = {0};
    int i, node_id, target_node;

    lru_add_drain();

    // pr_info("nucleus_split_migrater: locking migrate queue\n");
    spin_lock_irqsave(&nucleus_migrate_queue.request_queue_lock, flags);
    // pr_info("nucleus_split_migrater: locked migrate queue\n");
    list_for_each_entry_safe(req, req_tmp, &nucleus_migrate_queue.request_queue, list) {
        if (req->type == NUCLEUS_HUGEPAGE) {
            hp = req->hp;
            // pr_info("nucleus_split_migrater: migrate hp %lx\n", hp->address);
        } else {
            bp = req->bp;
            hp = bp->hp;
            // pr_info("nucleus_split_migrater: migrate hp %lx bp %u\n", hp->address, bp->offset);
        }
        hp_addr = hp->address << HPAGE_PMD_SHIFT;
        if (!hp->mm) {
            pr_info("nucleus_split_migrater: hp %lx mm not found\n", hp->address);
            goto free_req;
        }
        mmap_read_lock(hp->mm);
        // pr_info("nucleus_split_migrater: locked mm for hp %lx\n", hp->address);
        pmd = mm_find_pmd(hp->mm, hp_addr);
        if (!pmd) {
            pr_info("nucleus_split_migrater: hp %lx pmd not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        if (pmd_trans_huge(*pmd)) {
            page = pmd_page(*pmd);
        } else {
            bp_addr = hp_addr + (bp->offset << PAGE_SHIFT);
            pte = pte_offset_map(pmd, bp_addr);
            if (!pte) {
                pr_info("nucleus_split_migrater: hp %lx bp %u pte not found\n", hp->address, bp->offset);
                pte_unmap(pte);
                mmap_read_unlock(hp->mm);
                goto free_req;
            }
            page = pte_page(*pte);
            pte_unmap(pte);
            // pr_info("nucleus_split_migrater: hp %lx bp %u pte unmapped\n", hp->address, bp->offset);
        }
        if (!page) {
            pr_info("nucleus_split_migrater: hp %lx page not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }

        // pr_info("nucleus_split_migrater: unlocking mm for hp %lx\n", hp->address);
        mmap_read_unlock(hp->mm);
        // pr_info("nucleus_split_migrater: unlocked mm for hp %lx\n", hp->address);

        lruvec = mem_cgroup_page_lruvec(page);
        node_id = page_to_nid(page);
        if (node_id >= NUM_NUMA_NODES) {
            pr_info("nucleus_split_migrater: hp %lx invalid node id %d\n", hp->address, node_id);
            goto free_req;
        }
        if (lruvecs[node_id] == NULL) {
            lruvecs[node_id] = lruvec;
        }

        // pr_info("nucleus_split_migrater: locking lruvec\n");
        spin_lock_irq(&lruvec->lru_lock);
        // pr_info("nucleus_split_migrater: locked lruvec, isolating page\n");
        if (!__isolate_lru_page_prepare(page, 0)) {
            spin_unlock_irq(&lruvec->lru_lock);
            goto free_req;
        }

        // pr_info("nucleus_split_migrater: isolated page, getting page\n");
        if (unlikely(!get_page_unless_zero(page))) {
            spin_unlock_irq(&lruvec->lru_lock);
            goto free_req;
        }

        // pr_info("nucleus_split_migrater: got page, test clear lru\n");
        if (!TestClearPageLRU(page)) {
            put_page(page);
            spin_unlock_irq(&lruvec->lru_lock); 
            goto free_req;
        }

        target_node = req->target_node;
        if (target_node == HTMM_CXL_LOCAL_NUMA) {
            list_move(&page->lru, &promotion_list);
        } else {
            list_move(&page->lru, &demotion_list);
        }
        // pr_info("nucleus_split_migrater: moved page to %d list\n", target_node);
        update_lru_size(lruvec, page_lru(page), page_zonenum(page), -compound_nr(page));
        // pr_info("nucleus_split_migrater: updated lru size\n");
        spin_unlock_irq(&lruvec->lru_lock);
        // pr_info("nucleus_split_migrater: unlocked lruvec\n");
        nr_taken[node_id]++;

free_req:
        atomic_dec(&hp->ref_count);
        list_del(&req->list);
        kfree(req);
    }

    // pr_info("nucleus_split_migrater: mod node page state\n");
    for (i = 0; i < NUM_NUMA_NODES; i++) {
        if (lruvecs[i] && nr_taken > 0) {
            spin_lock_irq(&lruvecs[i]->lru_lock);
            __mod_node_page_state(NODE_DATA(i), NR_ISOLATED_ANON, nr_taken[i]);
            spin_unlock_irq(&lruvecs[i]->lru_lock);
        }
    }

    // pr_info("nucleus_split_migrater: demoting pages\n");
    local_pgdat = NODE_DATA(HTMM_CXL_LOCAL_NUMA);
    *demoted = migrate_page_list_safe(&demotion_list, local_pgdat, false);
    if (!list_empty(&demotion_list)) {
        // pr_info("nucleus_split_migrater: moving remaining demotion list to lruvec\n");
        lruvec = lruvecs[HTMM_CXL_LOCAL_NUMA];
        spin_lock_irq(&lruvec->lru_lock);
		move_pages_to_lru(lruvec, &demotion_list);
		__mod_node_page_state(local_pgdat, NR_ISOLATED_ANON, -nr_taken[HTMM_CXL_LOCAL_NUMA]);
		spin_unlock_irq(&lruvec->lru_lock);
        // pr_info("nucleus_split_migrater: moved remaining demotion list to lruvec\n");
    }

    // pr_info("nucleus_split_migrater: promoting pages\n");
    remote_pgdat = NODE_DATA(HTMM_CXL_REMOTE_NUMA);
    *promoted = migrate_page_list_safe(&promotion_list, remote_pgdat, true);
    if (!list_empty(&promotion_list)) {
        // pr_info("nucleus_split_migrater: moving remaining promotion list to lruvec\n");
        lruvec = lruvecs[HTMM_CXL_REMOTE_NUMA];
        spin_lock_irq(&lruvec->lru_lock);
        move_pages_to_lru(lruvec, &promotion_list);
        __mod_node_page_state(remote_pgdat, NR_ISOLATED_ANON, -nr_taken[HTMM_CXL_REMOTE_NUMA]);
        spin_unlock_irq(&lruvec->lru_lock);
        // pr_info("nucleus_split_migrater: moved remaining promotion list to lruvec\n");
    }

    // pr_info("nucleus_split_migrater: unlocking migrate queue\n");
    spin_unlock_irqrestore(&nucleus_migrate_queue.request_queue_lock, flags);
    // pr_info("nucleus_split_migrater: unlocked migrate queue\n");
}

static int nucleus_split_migrater(void *data)
{
    unsigned int split = 0, promoted = 0, demoted = 0;
    while (!kthread_should_stop()) {
        if (!spin_trylock(&nucleus_split_queue.request_queue_lock)) {
            pr_info("nucleus_split_migrater: split queue locked\n");
            goto next_iteration;
        }
        if (!spin_trylock(&nucleus_migrate_queue.request_queue_lock)) {
            pr_info("nucleus_split_migrater: migrate queue locked\n");
            goto next_iteration_unlock_split;
        }
        if (list_empty(&nucleus_split_queue.request_queue) && list_empty(&nucleus_migrate_queue.request_queue)) {
            pr_info("nucleus_split_migrater: both split and migrate queues empty\n");
            goto next_iteration_unlock_migrate;
        }
        spin_unlock(&nucleus_migrate_queue.request_queue_lock);
        spin_unlock(&nucleus_split_queue.request_queue_lock);

        pr_info("nucleus_split_migrater: processing split requests\n");
		split = split_hugepages();
        pr_info("nucleus_split_migrater: processed split requests, split %u pages\n", split);

        pr_info("nucleus_split_migrater: processing migrate requests\n");
		migrate_hugepages_and_basepages(&promoted, &demoted);
        pr_info("nucleus_split_migrater: processed migrate requests, promoted %u pages, demoted %u pages\n", promoted, demoted);

next_iteration_unlock_migrate:
        spin_unlock(&nucleus_migrate_queue.request_queue_lock);
next_iteration_unlock_split:
        spin_unlock(&nucleus_split_queue.request_queue_lock);
next_iteration:
        msleep_interruptible(5000);
    }
    return 0;
}

int nucleus_split_migrater_init(void)
{
    int err = 0;
    const struct cpumask *cpumask = cpumask_of_node(HTMM_CXL_LOCAL_NUMA);;
    pr_info("nucleus_split_migrater: init\n");
    knucleussplitmigraterd = kthread_run(nucleus_split_migrater, NULL, "knucleussplitmigraterd");
    if (IS_ERR(knucleussplitmigraterd)) {
        pr_err("nucleus_split_migrater: failed to create kernel thread\n");
        err = PTR_ERR(knucleussplitmigraterd);
        knucleussplitmigraterd = NULL;
    } else {
        set_cpus_allowed_ptr(knucleussplitmigraterd, cpumask);
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
