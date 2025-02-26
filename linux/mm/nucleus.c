#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/mmzone.h>
#include <linux/mm_inline.h>
#include <linux/rmap.h>
#include <linux/htmm.h>

#include <linux/nucleus.h>

unsigned long nucleus_def_tier_size = 0;
EXPORT_SYMBOL(nucleus_def_tier_size);

unsigned long smoothed_lat_local;
EXPORT_SYMBOL(smoothed_lat_local);

unsigned long smoothed_lat_remote;
EXPORT_SYMBOL(smoothed_lat_remote);

unsigned long smoothed_t_lat_hp;
EXPORT_SYMBOL(smoothed_t_lat_hp);

unsigned long smoothed_t_lat_bp;
EXPORT_SYMBOL(smoothed_t_lat_bp);

struct deferred_nucleus_request_queue nucleus_hugepages_deferred_queue = {
	.request_queue_lock = __SPIN_LOCK_UNLOCKED(nucleus_hugepages_deferred_queue.request_queue_lock),
	.request_queue = LIST_HEAD_INIT(nucleus_hugepages_deferred_queue.request_queue),
};
EXPORT_SYMBOL(nucleus_hugepages_deferred_queue);

// void init_deferred_nucleus_request_queue() {
// 	spin_lock_init(&nucleus_hugepages_deferred_queue.request_queue_lock);
// 	INIT_LIST_HEAD(&nucleus_hugepages_deferred_queue.request_queue);
// }

void nucleus_init_def_tier_size()
{
	struct pglist_data *pgdat;
	struct mem_cgroup_per_node *pn;
	int nid = HTMM_CXL_LOCAL_NUMA;

	pgdat = NODE_DATA(nid);
	pn = next_memcg_cand(pgdat);
	if (!pn) {
		return;
	}
	WRITE_ONCE(nucleus_def_tier_size, pn->max_nr_base_pages);
}

void nucleus_mm_init(struct mm_struct *mm)
{
	// pr_info("nucleus: mm_init\n");
    if (!mm || !mm->htmm_enabled) {
		return;
    }

	// pr_info("nucleus: hash_init for mm %p, htmm_enabled %d\n", mm, mm->htmm_enabled);
	hash_init(mm->nucleus_hugepages_hash);
}

void nucleus_mm_exit(struct mm_struct *mm)
{
	struct nucleus_hugepage *hp;
	struct hlist_node *tmp;
	int bkt;
	// pr_info("nucleus: mm_exit\n");

	if (!mm || !mm->htmm_enabled) {
		return;
	}

	// pr_info("nucleus: hash_del for mm %p, htmm_enabled %d, is_empty: %d\n", mm, mm->htmm_enabled, hash_empty(mm->nucleus_hugepages_hash));
	if (!hash_empty(mm->nucleus_hugepages_hash)) {
		// pr_info("nucleus: hash_del for mm %p, htmm_enabled %d, is_empty: %d\n", mm, mm->htmm_enabled, hash_empty(mm->nucleus_hugepages_hash));
		hash_for_each_safe(mm->nucleus_hugepages_hash, bkt, tmp, hp, hash) {
			// pr_info("nucleus: hash_del hp %lx\n", hp->address);
			hash_del(&hp->hash);
			atomic_dec(&hp->ref_count);
		}
	}
}

static struct nucleus_hugepage *get_nucleus_hugepage(struct mm_struct *mm, unsigned long hp_vaddr)
{
	struct nucleus_hugepage *hp;

	hash_for_each_possible(mm->nucleus_hugepages_hash, hp, hash, hp_vaddr) {
		if (hp->address == hp_vaddr) {
			return hp;
		}
	}

	return NULL;
}

static void insert_to_nucleus_hugepages_hash(struct mm_struct *mm, unsigned long hp_vaddr, struct nucleus_hugepage *hp)
{
	hp->mm = mm;
	hp->address = hp_vaddr;
	hash_add(mm->nucleus_hugepages_hash, &hp->hash, hp_vaddr);
}

static struct nucleus_hugepage *get_or_create_nucleus_hugepage(struct mm_struct *mm, unsigned long hp_vaddr)
{
	int i;
	unsigned long flags;
	struct nucleus_hugepage *hp = get_nucleus_hugepage(mm, hp_vaddr);
	struct nucleus_basepage *bp;
	struct deferred_nucleus_request *req;
	if (!hp) {
		hp = kzalloc(sizeof(struct nucleus_hugepage), GFP_KERNEL);
		if (!hp) {
			pr_err("nucleus: failed to allocate memory for hp\n");
			return NULL;
		}
		// pr_info("nucleus: created hp %lx\n", hp_vaddr);
		INIT_LIST_HEAD(&hp->list);
		hp->bp_list = vzalloc(HPAGE_PMD_NR * sizeof(struct nucleus_basepage));
		if (!hp->bp_list) {
			pr_err("nucleus: failed to allocate memory for bp_list\n");
			return NULL;
		}
		for (i = 0; i < HPAGE_PMD_NR; i++) {
			bp = &hp->bp_list[i];
			bp->hp = hp;
			bp->access_freq = 0;
			INIT_LIST_HEAD(&bp->list);
		}
		insert_to_nucleus_hugepages_hash(mm, hp_vaddr, hp);
		atomic_set(&hp->ref_count, 1);

		req = kzalloc(sizeof(struct deferred_nucleus_request), GFP_KERNEL);
		if (!req) {
			pr_err("nucleus: failed to allocate memory for req\n");
			return NULL;
		}
		req->hp = hp;
		spin_lock_irqsave(&nucleus_hugepages_deferred_queue.request_queue_lock, flags);
		list_add_tail(&req->list, &nucleus_hugepages_deferred_queue.request_queue);
		spin_unlock_irqrestore(&nucleus_hugepages_deferred_queue.request_queue_lock, flags);
	}
	return hp;
}

static void perform_cooling(struct mem_cgroup *memcg, struct nucleus_hugepage *hp)
{
    int i, diff;
    unsigned int memcg_cclock;
	unsigned long access_freq;

    spin_lock(&memcg->access_lock);
    /* check cooling */
    memcg_cclock = READ_ONCE(memcg->cooling_clock);
    spin_unlock(&memcg->access_lock);

    if (memcg_cclock > hp->cooling_clock) {
	    diff = memcg_cclock - hp->cooling_clock;
		// pr_info("nucleus: perform cooling for hp %lx, diff %d\n", hp->address, diff);

	    /* perform cooling */
	    for (i = 0; i < HPAGE_PMD_NR; i++) {
			access_freq = READ_ONCE(hp->bp_list[i].access_freq);
			/* halves access counts of basepage diff times */
			access_freq >>= diff;
			WRITE_ONCE(hp->bp_list[i].access_freq, access_freq);
		}
    }

	WRITE_ONCE(hp->cooling_clock, memcg_cclock);
}

void nucleus_update_access_freq_and_perform_cooling(struct mem_cgroup *memcg, struct mm_struct *mm, unsigned long address)
{
	unsigned long hp_vaddr;
	unsigned long bp_offset;
	struct nucleus_hugepage *hp;
	struct nucleus_basepage *bp;

	if (!mm || !mm->htmm_enabled) {
		return;
	}

	hp_vaddr = address >> HPAGE_PMD_SHIFT;
	bp_offset = (address & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	hp = get_or_create_nucleus_hugepage(mm, hp_vaddr);

	if (!hp) {
		pr_err("nucleus: failed to get or create hp\n");
		return;
	}

	perform_cooling(memcg, hp);

	bp = &hp->bp_list[bp_offset];
	WRITE_ONCE(bp->access_freq, READ_ONCE(bp->access_freq) + 1);
	// pr_info("nucleus: hp %lx, bp %lu, access_freq %u\n", hp_vaddr, bp_offset, bp->access_freq);
}
