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

#include <trace/events/nucleus.h>

struct deferred_nucleus_request_queue nucleus_split_queues[NUM_SPLIT_MIGRATE_THREADS];
EXPORT_SYMBOL(nucleus_split_queues);

struct deferred_nucleus_request_queue nucleus_migrate_queues[NUM_SPLIT_MIGRATE_THREADS];
EXPORT_SYMBOL(nucleus_migrate_queues);

DECLARE_WAIT_QUEUE_HEAD(nucleus_split_migrate_wait);
EXPORT_SYMBOL(nucleus_split_migrate_wait);

atomic_t nucleus_process_split_migrate[NUM_SPLIT_MIGRATE_THREADS] = ATOMIC_INIT(0);
EXPORT_SYMBOL(nucleus_process_split_migrate);

atomic_t nucleus_split_migrate_queues_initialized = ATOMIC_INIT(0);
EXPORT_SYMBOL(nucleus_split_migrate_queues_initialized);

#define NUCLEUS_SPLIT_MIGRATER_TIMEOUT 5000 // 5 seconds

static struct task_struct *knucleussplitmigraterd[NUM_SPLIT_MIGRATE_THREADS] = {NULL};

#define NUM_NUMA_NODES 2

static bool has_split_or_migrate_requests(int thread_id)
{
    if (atomic_read(&nucleus_process_split_migrate[thread_id]) > 0) {
        return true;
    }
    return false;
}

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

static unsigned long split_hugepages(int thread_id)
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
    unsigned long split = 0;
    int i, node_id, ret;
    bool skip_iso;
    for (i = 0; i < NUM_NUMA_NODES; i++) {
        INIT_LIST_HEAD(&split_lists[i]);
        lruvecs[i] = NULL;
    }

    spin_lock_irqsave(&nucleus_split_queues[thread_id].request_queue_lock, flags);
    list_for_each_entry_safe(req, req_tmp, &nucleus_split_queues[thread_id].request_queue, list) {
        LIST_HEAD(tmp);
        hp = req->hp;
        // pr_info("nucleus_split_migrater: split hp %lx\n", hp->address);
        hp_addr = hp->address << HPAGE_PMD_SHIFT;
        if (!hp->mm) {
            pr_warn("nucleus_split_migrater[%d]: split_hugepages: hp %lx mm not found\n", thread_id, hp->address);
            goto free_req;
        }
        mmap_read_lock(hp->mm);
        if (atomic_read(&hp->mm->mm_users) == 0) {
            // pr_info("nucleus_split_migrater: hp %lx mm users 0\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        pmd = mm_find_pmd(hp->mm, hp_addr);
        if (!pmd) {
            // pr_info("nucleus_split_migrater: hp %lx pmd not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        if (!pmd_trans_huge(*pmd)) {
            pr_warn("nucleus_split_migrater[%d]: split_hugepages: hp %lx not pmd_trans_huge\n", thread_id, hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        skip_iso = false;

        page = pmd_page(*pmd);
        if (!page) {
            // pr_info("nucleus_split_migrater: hp %lx page not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }

        mmap_read_unlock(hp->mm);

        lruvec = mem_cgroup_page_lruvec(page);
        node_id = page_to_nid(page);
        if (node_id >= NUM_NUMA_NODES) {
            // pr_info("nucleus_split_migrater: hp %lx invalid node id %d\n", hp->address, node_id);
            goto free_req;
        }
        if (lruvecs[node_id] == NULL) {
            lruvecs[node_id] = lruvec;
        }

        if (!PageLRU(page)) {
            // pr_info("nucleus_split_migrater: hp %lx not on LRU\n", hp->address);
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
            // pr_info("nucleus_split_migrater: skipping isolation for hp %lx\n", hp->address);
            if (page->lru.next != LIST_POISON1 || page->lru.prev != LIST_POISON2) {
                // pr_info("nucleus_split_migrater: hp %lx page lru not poisoned, cannot skip isolation\n", hp->address);
                goto free_req;
            }
            // pr_info("nucleus_split_migrater: skipping isolation for hp %lx, adding to tmp list\n", hp->address);
            list_add(&page->lru, &tmp);
        }

        lock_page(page);

        ret = split_huge_page_to_list(page, &tmp);
        if (ret == 0) {
            split++;
            list_splice(&tmp, &split_lists[node_id]);
        } else {
            check_failed_list(&tmp, &failed_list);
            pr_info("nucleus_split_migrater[%d]: split_huge_page_to_list failed for hp %lx, ret %d\n", thread_id, hp->address, ret);
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
            // pr_info("nucleus_split_migrater: putback split pages in node %d lruvec\n", i);
            putback_split_pages(&split_lists[i], lruvecs[i]);
        }
    }
    spin_unlock_irqrestore(&nucleus_split_queues[thread_id].request_queue_lock, flags);

    return split;
}

static __always_inline void update_lru_sizes(struct lruvec *lruvec, enum lru_list lru, unsigned long *nr_zone_taken)
{
    int zid;

    for (zid = 0; zid < MAX_NR_ZONES; zid++) {
        if (!nr_zone_taken[zid])
            continue;

        update_lru_size(lruvec, lru, zid, -nr_zone_taken[zid]);
    }
}

unsigned long add_file_pages_to_demotion_list(struct lruvec *lruvec, enum lru_list lru, struct list_head *demotion_list)
{
    struct page *page, *page_tmp;
    unsigned long nr_pages, nr_taken_file = 0, nr_zone_taken[MAX_NR_ZONES] = {0};
    struct list_head *lru_list = &lruvec->lists[lru];

    spin_lock_irq(&lruvec->lru_lock);
    list_for_each_entry_safe(page, page_tmp, lru_list, lru) {
        if (!__isolate_lru_page_prepare(page, 0)) {
            continue;
        }
        if (unlikely(!get_page_unless_zero(page))) {
            continue;
        }
        if (!TestClearPageLRU(page)) {
            put_page(page);
            continue;
        }
        list_move(&page->lru, demotion_list);
        nr_pages = compound_nr(page);
        nr_taken_file += nr_pages;
        nr_zone_taken[page_zonenum(page)] += nr_pages;
    }

    update_lru_sizes(lruvec, lru, nr_zone_taken);
    if (nr_taken_file > 0) {
        __mod_node_page_state(NODE_DATA(HTMM_CXL_LOCAL_NUMA), NR_ISOLATED_FILE, nr_taken_file);
    }
    spin_unlock_irq(&lruvec->lru_lock);
    return nr_taken_file;
}

static void migrate_hugepages_and_basepages(unsigned long *promoted, unsigned long *demoted, int thread_id)
{
    unsigned long flags;
    struct nucleus_migrate_request *req, *req_tmp;
    struct nucleus_hugepage *hp;
    struct nucleus_basepage *bp;
    struct page *page, *page_tmp;
    struct lruvec *lruvec;
    struct mem_cgroup *memcg = NULL, *memcg_tmp = NULL;
    pg_data_t *local_pgdat, *remote_pgdat;
    pmd_t *pmd;
    pte_t *pte;
    unsigned long hp_addr, bp_addr;
    int i, node_id, target_node;
    unsigned long nr_promoted, nr_to_promote = 0, total_promoted = 0;
    unsigned long nr_demoted, nr_to_demote = 0, total_demoted = 0;
    unsigned long compound_nr_page, max_to_migrate = 5000 * HPAGE_PMD_NR; // 5000 hugepages
    LIST_HEAD(promotion_list);
    LIST_HEAD(demotion_list);
    LIST_HEAD(failed_promotion_list);
    LIST_HEAD(failed_demotion_list);
    struct lruvec *lruvecs[NUM_NUMA_NODES] = {NULL};
    unsigned long nr_taken[NUM_NUMA_NODES] = {0}, cur_nr_taken[NUM_NUMA_NODES] = {0}, nr_taken_file = 0;

    lru_add_drain();

    // pr_info("nucleus_split_migrater[%d]: locking migrate queue\n", thread_id);
    spin_lock_irqsave(&nucleus_migrate_queues[thread_id].request_queue_lock, flags);
    // pr_info("nucleus_split_migrater[%d]: locked migrate queue\n", thread_id);

    list_for_each_entry_safe(req, req_tmp, &nucleus_migrate_queues[thread_id].request_queue, list) {
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
            // pr_info("nucleus_split_migrater: hp %lx mm not found\n", hp->address);
            goto free_req;
        }
        mmap_read_lock(hp->mm);
        if (atomic_read(&hp->mm->mm_users) == 0) {
            // pr_info("nucleus_split_migrater: hp %lx mm users 0\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        // pr_info("nucleus_split_migrater: locked mm for hp %lx\n", hp->address);
        pmd = mm_find_pmd(hp->mm, hp_addr);
        if (!pmd) {
            // pr_info("nucleus_split_migrater: hp %lx pmd not found\n", hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }
        if (pmd_trans_huge(*pmd)) {
            page = pmd_page(*pmd);
        } else if (!bp) {
            pr_warn("nucleus_split_migrater[%d]: migrate_hugepages_and_basepages pmd_trans_huge is not set for hp %lx migration\n", thread_id, hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        } else {
            bp_addr = hp_addr + (bp->offset << PAGE_SHIFT);
            pte = pte_offset_map(pmd, bp_addr);
            if (!pte || !pte_present(*pte)) {
                // pr_info("nucleus_split_migrater[%d]: hp %lx bp %u pte not found\n", thread_id, hp->address, bp->offset);
                pte_unmap(pte);
                mmap_read_unlock(hp->mm);
                goto free_req;
            }
            page = pte_page(*pte);
            pte_unmap(pte);
            // pr_info("nucleus_split_migrater[%d]: hp %lx bp %u pte unmapped\n", thread_id, hp->address, bp->offset);
        }
        if (!page) {
            // pr_info("nucleus_split_migrater[%d]: hp %lx page not found\n", thread_id, hp->address);
            mmap_read_unlock(hp->mm);
            goto free_req;
        }

        // pr_info("nucleus_split_migrater: unlocking mm for hp %lx\n", hp->address);
        mmap_read_unlock(hp->mm);
        // pr_info("nucleus_split_migrater: unlocked mm for hp %lx\n", hp->address);

        memcg_tmp = page_memcg(page);
        if (memcg == NULL) {
            if (memcg_tmp && memcg_tmp->htmm_enabled) {
                memcg = memcg_tmp;
            } else if (!memcg_tmp) {
                // pr_warn("nucleus_split_migrater[%d]: hp %lx memcg not set and not found\n", thread_id, hp->address);
                goto free_req;
            } else {
                pr_warn("nucleus_split_migrater[%d]: hp %lx memcg not set and not htmm enabled\n", thread_id, hp->address);
                goto free_req;
            }
        } else if (!memcg_tmp) {
            // pr_warn("nucleus_split_migrater[%d]: hp %lx memcg not found\n", thread_id, hp->address);
            goto free_req;
        } else if (!memcg_tmp->htmm_enabled) {
            pr_warn("nucleus_split_migrater[%d]: hp %lx memcg not htmm enabled\n", thread_id, hp->address);
            goto free_req;
        } else if (memcg_tmp != memcg) {
            pr_warn("nucleus_split_migrater[%d]: hp %lx memcg not matching previously set memcg\n", thread_id, hp->address);
            goto free_req;
        }
        lruvec = mem_cgroup_page_lruvec(page);
        node_id = page_to_nid(page);
        if (node_id >= NUM_NUMA_NODES) {
            // pr_info("nucleus_split_migrater[%d]: hp %lx invalid node id %d\n", thread_id, hp->address, node_id);
            goto free_req;
        }
        if (lruvecs[node_id] == NULL) {
            lruvecs[node_id] = lruvec;
        }

        // pr_info("nucleus_split_migrater[%d]: locking lruvec\n", thread_id);
        spin_lock_irq(&lruvec->lru_lock);
        // pr_info("nucleus_split_migrater[%d]: locked lruvec, isolating page\n", thread_id);
        if (!__isolate_lru_page_prepare(page, 0)) {
            spin_unlock_irq(&lruvec->lru_lock);
            goto free_req;
        }

        // pr_info("nucleus_split_migrater[%d]: isolated page, getting page\n", thread_id);
        if (unlikely(!get_page_unless_zero(page))) {
            spin_unlock_irq(&lruvec->lru_lock);
            goto free_req;
        }

        // pr_info("nucleus_split_migrater[%d]: got page, test clear lru\n", thread_id);
        if (!TestClearPageLRU(page)) {
            put_page(page);
            spin_unlock_irq(&lruvec->lru_lock); 
            goto free_req;
        }

        target_node = req->target_node;
        compound_nr_page = compound_nr(page);
        if (target_node == HTMM_CXL_LOCAL_NUMA) {
            list_move(&page->lru, &promotion_list);
        } else {
            list_move(&page->lru, &demotion_list);
        }
        // pr_info("nucleus_split_migrater[%d]: moved page to %d list\n", thread_id, target_node);
        update_lru_size(lruvec, page_lru(page), page_zonenum(page), -compound_nr_page);
        // pr_info("nucleus_split_migrater[%d]: updated lru size\n", thread_id);
        spin_unlock_irq(&lruvec->lru_lock);
        // pr_info("nucleus_split_migrater[%d]: unlocked lruvec\n", thread_id);
        nr_taken[node_id] += compound_nr_page;

free_req:
        atomic_dec(&hp->ref_count);
        list_del(&req->list);
        kfree(req);
    }

    // pr_info("nucleus_split_migrater[%d]: mod node page state\n", thread_id);
    for (i = 0; i < NUM_NUMA_NODES; i++) {
        if (lruvecs[i] && nr_taken[i] > 0) {
            spin_lock_irq(&lruvecs[i]->lru_lock);
            __mod_node_page_state(NODE_DATA(i), NR_ISOLATED_ANON, nr_taken[i]);
            spin_unlock_irq(&lruvecs[i]->lru_lock);
            cur_nr_taken[i] = nr_taken[i];
        }
    }

    local_pgdat = NODE_DATA(HTMM_CXL_LOCAL_NUMA);
    remote_pgdat = NODE_DATA(HTMM_CXL_REMOTE_NUMA);

    if (!memcg || !memcg->htmm_enabled) {
        goto failed_migrations;
    }
    if (!memcg->nodeinfo || !memcg->nodeinfo[HTMM_CXL_LOCAL_NUMA]) {
        goto failed_migrations;
    }

    pr_info("nucleus_split_migrater[%d]: demoting file pages\n", thread_id);
    lruvec = mem_cgroup_lruvec(memcg, local_pgdat);
    nr_taken_file = add_file_pages_to_demotion_list(lruvec, LRU_INACTIVE_FILE, &demotion_list);
    nr_taken_file += add_file_pages_to_demotion_list(lruvec, LRU_ACTIVE_FILE, &demotion_list);
    cur_nr_taken[HTMM_CXL_LOCAL_NUMA] += nr_taken_file;
    pr_info("nucleus_split_migrater[%d]: added %lu file pages to demotion list\n", thread_id, nr_taken_file);

    do {
        LIST_HEAD(cur_promotion_list);
        LIST_HEAD(cur_demotion_list);
        nr_to_promote = 0;
        nr_to_demote = 0;

        list_for_each_entry_safe(page, page_tmp, &demotion_list, lru) {
            if (nr_to_demote >= max_to_migrate) {
                break;
            }
            list_move(&page->lru, &cur_demotion_list);
            nr_to_demote += compound_nr(page);
        }

        nr_demoted = migrate_page_list_safe(&cur_demotion_list, local_pgdat, false);
        total_demoted += nr_demoted;
        cur_nr_taken[HTMM_CXL_LOCAL_NUMA] -= nr_demoted;
        if (!list_empty(&cur_demotion_list)) {
            list_splice_tail(&cur_demotion_list, &failed_demotion_list);
        }
        // pr_info("nucleus_split_migrater[%d]: nr_demoted %lu\n", thread_id, nr_demoted);

        list_for_each_entry_safe(page, page_tmp, &promotion_list, lru) {
            if (nr_to_promote >= max_to_migrate) {
                break;
            }
            list_move(&page->lru, &cur_promotion_list);
            nr_to_promote += compound_nr(page);
        }

        nr_promoted = migrate_page_list_safe(&cur_promotion_list, remote_pgdat, true);
        total_promoted += nr_promoted;
        cur_nr_taken[HTMM_CXL_REMOTE_NUMA] -= nr_promoted;
        if (!list_empty(&cur_promotion_list)) {
            list_splice_tail(&cur_promotion_list, &failed_promotion_list);
        }
        // pr_info("nucleus_split_migrater[%d]: nr_promoted %lu\n", thread_id, nr_promoted);
    } while (nr_demoted > 0 || nr_promoted > 0);

    list_splice_tail(&failed_demotion_list, &demotion_list);
    list_splice_tail(&failed_promotion_list, &promotion_list);

failed_migrations:
    for (i = 0; i < NUM_NUMA_NODES; i++) {
        if (lruvecs[i] && nr_taken[i] > 0) {
            spin_lock_irq(&lruvecs[i]->lru_lock);
            if (i == HTMM_CXL_LOCAL_NUMA) {
                move_pages_to_lru(lruvecs[i], &demotion_list);
            } else {
                move_pages_to_lru(lruvecs[i], &promotion_list);
            }
            __mod_node_page_state(NODE_DATA(i), NR_ISOLATED_ANON, -nr_taken[i]);
            spin_unlock_irq(&lruvecs[i]->lru_lock);
        }
        if (i == HTMM_CXL_LOCAL_NUMA && lruvecs[i] && nr_taken_file > 0) {
            // file pages are only demoted, so nr_taken_file is only updated for local node
            spin_lock_irq(&lruvecs[i]->lru_lock);
            __mod_node_page_state(NODE_DATA(i), NR_ISOLATED_FILE, -nr_taken_file);
            spin_unlock_irq(&lruvecs[i]->lru_lock);
        }
    }

    // pr_info("nucleus_split_migrater[%d]: unlocking migrate queue\n", thread_id);
    spin_unlock_irqrestore(&nucleus_migrate_queues[thread_id].request_queue_lock, flags);
    *promoted = total_promoted;
    *demoted = total_demoted;
    // pr_info("nucleus_split_migrater[%d]: unlocked migrate queue\n", thread_id);
}

static int nucleus_split_migrater(void *data)
{
    unsigned long split = 0, promoted = 0, demoted = 0, start_tsc, end_tsc, time_ms;
    int ret;
    int thread_id = (int)(unsigned long)data;
    pr_info("nucleus_split_migrater[%d]: started\n", thread_id);
    while (!kthread_should_stop()) {
        ret = wait_event_interruptible_timeout(nucleus_split_migrate_wait, has_split_or_migrate_requests(thread_id), msecs_to_jiffies(NUCLEUS_SPLIT_MIGRATER_TIMEOUT));
        if (ret == 0) {
            // pr_info("nucleus_split_migrater: timeout\n");
            continue;
        }

        pr_info("nucleus_split_migrater[%d]: processing split requests\n", thread_id);
        start_tsc = rdtscp();
		split = split_hugepages(thread_id);
        end_tsc = rdtscp();
        time_ms = (end_tsc - start_tsc) / cpu_khz;
        trace_nucleus_split(split, time_ms);
        pr_info("nucleus_split_migrater[%d]: processed split requests, split %lu pages\n", thread_id, split);

        pr_info("nucleus_split_migrater[%d]: processing migrate requests\n", thread_id);
        start_tsc = rdtscp();
		migrate_hugepages_and_basepages(&promoted, &demoted, thread_id);
        end_tsc = rdtscp();
        time_ms = (end_tsc - start_tsc) / cpu_khz;
        trace_nucleus_migrate(promoted, demoted, time_ms);
        pr_info("nucleus_split_migrater[%d]: processed migrate requests, promoted %lu pages, demoted %lu pages\n", thread_id, promoted, demoted);
        atomic_set(&nucleus_process_split_migrate[thread_id], 0);
    }
    pr_info("nucleus_split_migrater[%d]: stopped\n", thread_id);
    return 0;
}

int nucleus_split_migrater_init(void)
{
    int err = 0, i;
    const struct cpumask *cpumask = cpumask_of_node(HTMM_CXL_LOCAL_NUMA);
    pr_info("nucleus_split_migrater: init\n");
    for (i = 0; i < NUM_SPLIT_MIGRATE_THREADS; i++) {
        spin_lock_init(&nucleus_split_queues[i].request_queue_lock);
        INIT_LIST_HEAD(&nucleus_split_queues[i].request_queue);
        spin_lock_init(&nucleus_migrate_queues[i].request_queue_lock);
        INIT_LIST_HEAD(&nucleus_migrate_queues[i].request_queue);
        knucleussplitmigraterd[i] = kthread_run(nucleus_split_migrater, (void *)(unsigned long)i, "knucleussplitmigraterd");
        if (IS_ERR(knucleussplitmigraterd[i])) {
            pr_err("nucleus_split_migrater: failed to create kernel thread\n");
            err = PTR_ERR(knucleussplitmigraterd[i]);
            knucleussplitmigraterd[i] = NULL;
        } else {
            set_cpus_allowed_ptr(knucleussplitmigraterd[i], cpumask);
        }
    }
    atomic_set(&nucleus_split_migrate_queues_initialized, 1);
    return err;
}

void nucleus_split_migrater_exit(void)
{
    int i;
    atomic_set(&nucleus_split_migrate_queues_initialized, 0);
    for (i = 0; i < NUM_SPLIT_MIGRATE_THREADS; i++) {
        if (knucleussplitmigraterd[i]) {
	        kthread_stop(knucleussplitmigraterd[i]);
            knucleussplitmigraterd[i] = NULL;
        }
	}
    pr_info("nucleus_split_migrater: exit\n");
}
