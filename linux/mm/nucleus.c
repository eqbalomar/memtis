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
    struct mem_cgroup *memcg = get_mem_cgroup_from_mm(mm);
	// pr_info("nucleus: mm_init\n");

    if (!memcg || !memcg->htmm_enabled) {
		return;
    }

	// pr_info("nucleus: hash_init for mm %p, htmm_enabled %d\n", mm, mm->htmm_enabled);
	hash_init(mm->nucleus_hugepages_hash);
}

void nucleus_mm_exit(struct mm_struct *mm)
{
    struct mem_cgroup *memcg = get_mem_cgroup_from_mm(mm);
	struct nucleus_hugepage *hp;
	struct hlist_node *tmp;
	struct deferred_nucleus_request *req;
	int bkt;
	unsigned long flags;
	// pr_info("nucleus: mm_exit\n");

    if (!memcg || !memcg->htmm_enabled) {
		return;
    }

	// pr_info("nucleus: hash_del for mm %p, htmm_enabled %d, is_emtpy: %d\n", mm, mm->htmm_enabled, hash_empty(mm->nucleus_hugepages_hash));
	hash_for_each_safe(mm->nucleus_hugepages_hash, bkt, tmp, hp, hash) {
		pr_info("nucleus: hash_del hp %lx\n", hp->address);
		hash_del(&hp->hash);
		req = kzalloc(sizeof(struct deferred_nucleus_request), GFP_KERNEL);
		if (!req) {
			pr_err("nucleus: failed to allocate memory for req\n");
			return;
		}
		req->hp = hp;
		req->type = NUCLEUS_REMOVE_HUGEPAGE;
		spin_lock_irqsave(&nucleus_hugepages_deferred_queue.request_queue_lock, flags);
		list_add_tail(&req->list, &nucleus_hugepages_deferred_queue.request_queue);
		spin_unlock_irqrestore(&nucleus_hugepages_deferred_queue.request_queue_lock, flags);
	}
}

struct nucleus_hugepage *get_nucleus_hugepage(struct mm_struct *mm, unsigned long hp_vaddr)
{
	struct nucleus_hugepage *hp;

	hash_for_each_possible(mm->nucleus_hugepages_hash, hp, hash, hp_vaddr) {
		if (hp->address == hp_vaddr) {
			return hp;
		}
	}

	return NULL;
}

void insert_to_nucleus_hugepages_hash(struct mm_struct *mm, unsigned long hp_vaddr, struct nucleus_hugepage *hp)
{
	hp->mm = mm;
	hp->address = hp_vaddr;
	hash_add(mm->nucleus_hugepages_hash, &hp->hash, hp_vaddr);
}

void nucleus_update_access_freq_and_perform_cooling(struct mem_cgroup *memcg, struct mm_struct *mm, unsigned long address)
{
	unsigned long hp_vaddr = address >> HPAGE_PMD_SHIFT;
	unsigned long bp_offset = (address & ~HPAGE_PMD_MASK) >> PAGE_SHIFT;
	int i;
	unsigned long flags;
	struct nucleus_hugepage *hp = get_nucleus_hugepage(mm, hp_vaddr);
	struct nucleus_basepage *bp;
	struct deferred_nucleus_request *req;
	if (!hp) {
		hp = kzalloc(sizeof(struct nucleus_hugepage), GFP_KERNEL);
		if (!hp) {
			pr_err("nucleus: failed to allocate memory for hp\n");
			return;
		}
		pr_info("nucleus: created hp %lx\n", hp_vaddr);
		INIT_LIST_HEAD(&hp->list);
		insert_to_nucleus_hugepages_hash(mm, hp_vaddr, hp);
		hp->bp_list = vzalloc(HPAGE_PMD_NR * sizeof(struct nucleus_basepage));
		if (!hp->bp_list) {
			pr_err("nucleus: failed to allocate memory for bp_list\n");
			return;
		}
		for (i = 0; i < HPAGE_PMD_NR; i++) {
			bp = &hp->bp_list[i];
			bp->hp = hp;
			bp->access_freq = 0;
			INIT_LIST_HEAD(&bp->list);
		}
		req = kzalloc(sizeof(struct deferred_nucleus_request), GFP_KERNEL);
		if (!req) {
			pr_err("nucleus: failed to allocate memory for req\n");
			return;
		}
		req->hp = hp;
		req->type = NUCLEUS_ADD_HUGEPAGE;
		spin_lock_irqsave(&nucleus_hugepages_deferred_queue.request_queue_lock, flags);
		list_add_tail(&req->list, &nucleus_hugepages_deferred_queue.request_queue);
		spin_unlock_irqrestore(&nucleus_hugepages_deferred_queue.request_queue_lock, flags);
	}

	// TODO: perform cooling

	bp = &hp->bp_list[bp_offset];
	WRITE_ONCE(bp->access_freq, READ_ONCE(bp->access_freq) + 1);
	pr_info("nucleus: hp %lx, bp %lu, access_freq %u\n", hp_vaddr, bp_offset, bp->access_freq);
}
