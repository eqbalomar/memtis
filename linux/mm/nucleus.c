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

#define NUCLEUS_HUGEPAGES_HASH_BITS 10
// TODO: Create per-process hash tables
DEFINE_HASHTABLE(nucleus_hugepages_hash, NUCLEUS_HUGEPAGES_HASH_BITS);


struct nucleus_hugepage *get_nucleus_hugepage(unsigned long hp_vaddr)
{
	struct nucleus_hugepage *hp;

	hash_for_each_possible(nucleus_hugepages_hash, hp, hash, hp_vaddr) {
		if (hp->address == hp_vaddr)
			return hp;
	}

	return NULL;
}

void insert_to_nucleus_hugepages_hash(struct nucleus_hugepage *hp, unsigned long hp_vaddr)
{
	hp->address = hp_vaddr;
	hash_add(nucleus_hugepages_hash, &hp->hash, hp_vaddr);
}
