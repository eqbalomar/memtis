#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/mmzone.h>
#include <linux/mm_inline.h>
#include <linux/rmap.h>
#include <linux/htmm.h>

#include <linux/nucleus.h>

struct list_head nucleus_hugepages_deferred_list = LIST_HEAD_INIT(nucleus_hugepages_deferred_list);
EXPORT_SYMBOL(nucleus_hugepages_deferred_list);

void nucleus_mm_init(struct mm_struct *mm)
{
    struct mem_cgroup *memcg = get_mem_cgroup_from_mm(mm);

    if (!memcg || !memcg->htmm_enabled) {
		return;
    }

	hash_init(mm->nucleus_hugepages_hash);
}

struct nucleus_hugepage *get_nucleus_hugepage(struct mm_struct *mm, unsigned long hp_vaddr)
{
	struct nucleus_hugepage *hp;

	hash_for_each_possible(mm->nucleus_hugepages_hash, hp, hash, hp_vaddr) {
		if (hp->address == hp_vaddr)
			return hp;
	}

	return NULL;
}

void insert_to_nucleus_hugepages_hash(struct mm_struct *mm, unsigned long hp_vaddr, struct nucleus_hugepage *hp)
{
	hp->mm = mm;
	hp->address = hp_vaddr;
	hash_add(mm->nucleus_hugepages_hash, &hp->hash, hp_vaddr);
}
