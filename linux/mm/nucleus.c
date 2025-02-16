#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/mmzone.h>
#include <linux/mm_inline.h>
#include <linux/rmap.h>
#include <linux/htmm.h>

#include <linux/nucleus.h>

struct deferred_nucleus_request_queue nucleus_hugepages_deferred_queue = {
	.request_queue_lock = __SPIN_LOCK_UNLOCKED(nucleus_hugepages_deferred_queue.request_queue_lock),
	.request_queue = LIST_HEAD_INIT(nucleus_hugepages_deferred_queue.request_queue),
};
EXPORT_SYMBOL(nucleus_hugepages_deferred_queue);

// void init_deferred_nucleus_request_queue() {
// 	spin_lock_init(&nucleus_hugepages_deferred_queue.request_queue_lock);
// 	INIT_LIST_HEAD(&nucleus_hugepages_deferred_queue.request_queue);
// }

void nucleus_mm_init(struct mm_struct *mm)
{
    struct mem_cgroup *memcg = get_mem_cgroup_from_mm(mm);
	pr_info("nucleus: mm_init\n");

    if (!memcg || !memcg->htmm_enabled) {
		return;
    }

	pr_info("nucleus: hash_init\n");
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
	pr_info("nucleus: mm_exit\n");

    if (!memcg || !memcg->htmm_enabled) {
		return;
    }

	pr_info("nucleus: hash_del\n");
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
