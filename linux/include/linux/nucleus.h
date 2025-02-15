#ifndef _LINUX_NUCLEUS_H
#define _LINUX_NUCLEUS_H

#include <linux/list.h>
#include <linux/mm_types.h>

struct nucleus_hugepage {
	// Initialized by sampling thread on first access
	struct mm_struct *mm;
	unsigned long address;	// virtual address
	struct nucleus_basepage *bp_list;
	struct hlist_node hash; // hlist_node for adding to hash table
	
	// Fields used within algorithm
	struct list_head list;	// list head for adding to list of all hugepages
	unsigned int algo_access_freq;
	unsigned int algo_access_freq_to_move_in;
	unsigned int algo_num_to_move_in;

	// Output of algorithm
	bool merge_in_hp;
};

struct nucleus_basepage {
	// Initialized by sampling thread on first access
	struct nucleus_hugepage *hp;

	// Updated by sampling thread on each access/cooling
	unsigned int access_freq;
	unsigned int cooling_clk;

	// Fields used within algorithm
	struct list_head list;	// list head for adding to list of all basepages
	unsigned int algo_access_freq;

	// Output of algorithm
	bool place_in_def;
};


enum nucleus_request_type {
	NUCLEUS_ADD_HUGEPAGE,
	NUCLEUS_REMOVE_HUGEPAGE,
};

struct deferred_nucleus_request {
	struct nucleus_hugepage *hp;
	struct list_head list;
	enum nucleus_request_type type;
};

struct deferred_nucleus_request_queue {
	spinlock_t request_queue_lock;
	struct list_head request_queue;
};

extern struct deferred_nucleus_request_queue nucleus_hugepages_deferred_queue;

void init_deferred_nucleus_request_queue(void);
void nucleus_mm_init(struct mm_struct *mm);
void nucleus_mm_exit(struct mm_struct *mm);
struct nucleus_hugepage *get_nucleus_hugepage(struct mm_struct *mm, unsigned long hp_vaddr);
void insert_to_nucleus_hugepages_hash(struct mm_struct *mm, unsigned long hp_vaddr, struct nucleus_hugepage *hp);

#endif /* _LINUX_NUCLEUS_H */
