#ifndef _LINUX_NUCLEUS_H
#define _LINUX_NUCLEUS_H

#include <linux/list.h>
#include <linux/mm_types.h>

#define ACCESS_FREQ_PRECISION 10000ULL
#define CAPACITY_THRES 95

struct nucleus_hugepage {
	atomic_t ref_count;

	// Initialized by sampling thread on first access
	struct mm_struct *mm;
	unsigned long address;	// hp-aligned virtual address
	struct nucleus_basepage *bp_list;
	struct hlist_node hash; // hlist_node for adding to hash table

	// Updated by sampling thread on cooling
	unsigned int cooling_clock;
	
	// Fields used within algorithm
	struct list_head list;	// list head for adding to list of all hugepages
	unsigned long algo_access_freq;
	unsigned long algo_access_freq_to_move_in;
	unsigned long algo_num_to_move_in;
	unsigned long algo_num_to_move_in_initial;

	// Output of algorithm
	bool merge_in_hp;
};

struct nucleus_basepage {
	// Initialized by sampling thread on first access
	struct nucleus_hugepage *hp;
	unsigned int offset;

	// Updated by sampling thread on each access
	unsigned long access_freq;

	// Fields used within algorithm
	struct list_head list;	// list head for adding to list of all basepages
	unsigned long prev_access_freq;	// copy of access_freq from previous iteration of algorithm
	unsigned long algo_access_freq;	// smoothed access_freq for algorithm

	// Output of algorithm
	bool place_in_def;
};

struct nucleus_add_request {
	struct nucleus_hugepage *hp;
	struct list_head list;
};

struct deferred_nucleus_request_queue {
	spinlock_t request_queue_lock;
	struct list_head request_queue;
};

struct nucleus_split_request {
	struct nucleus_hugepage *hp;
	struct list_head list;
};

struct nucleus_merge_request {
	struct nucleus_hugepage *hp;
	int target_node;
	struct list_head list;
};

enum migrate_request_type {
    NUCLEUS_HUGEPAGE = 0,
	NUCLEUS_BASEPAGE = 1
};

struct nucleus_migrate_request {
	enum migrate_request_type type;
	union {
		struct nucleus_hugepage *hp;
		struct nucleus_basepage *bp;
	};
	int target_node;
	struct list_head list;
};

static inline __attribute__((always_inline)) unsigned long rdtscp(void)
{
   unsigned long a, d, c;

   __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));

   return (a | (d << 32));
}

/* nucleus.c */

// void init_nucleus_add_queue(void);
void nucleus_init_def_tier_size(void);
void nucleus_mm_init(struct mm_struct *mm);
void nucleus_mm_exit(struct mm_struct *mm);
void create_nucleus_hugepage(struct vm_area_struct *vma, unsigned long address);
void nucleus_update_access_freq_and_perform_cooling(struct mem_cgroup *memcg, struct mm_struct *mm, unsigned long address);

/* nucleus_merger.c */
int nucleus_merger_init(void);
void nucleus_merger_exit(void);

/* nucleus_split_migrater.c */
int nucleus_split_migrater_init(void);
void nucleus_split_migrater_exit(void);
unsigned long add_file_pages_to_demotion_list(struct lruvec *lruvec, enum lru_list lru, struct list_head *demotion_list, unsigned long nr_to_demote_file);

#endif /* _LINUX_NUCLEUS_H */
