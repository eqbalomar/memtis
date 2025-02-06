#ifndef _LINUX_NUCLEUS_H
#define _LINUX_NUCLEUS_H

#include <linux/list.h>


struct nucleus_hugepage {
	unsigned long access_freq;
	struct nucleus_basepage *list_per_hp;
	unsigned long access_freq_to_move_in;
	unsigned long num_to_move_in;
	bool merge_in_hp;
};

struct nucleus_basepage {
	unsigned long access_freq;
	struct list_head list_all_bp;
	struct nucleus_hugepage *hp;
	struct page *page;
	bool place_in_def;
};

void create_nucleus_input_lists(void);


#endif /* _LINUX_NUCLEUS_H */
