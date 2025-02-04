#include <linux/list.h>
#include <linux/memcontrol.h>
#include <linux/mempolicy.h>
#include <linux/mmzone.h>
#include <linux/mm_inline.h>
#include <linux/rmap.h>
#include <linux/htmm.h>

#include <linux/nucleus.h>

struct list_head nucleus_hugepages_list = LIST_HEAD_INIT(nucleus_hugepages_list);
struct list_head nucleus_basepages_list = LIST_HEAD_INIT(nucleus_basepages_list);

EXPORT_SYMBOL(nucleus_hugepages_list);
EXPORT_SYMBOL(nucleus_basepages_list);

static void add_to_nucleus_lists(struct list_head* page_list, struct list_head* lru_list_tmp) {
	int i, idx, offset, count=0;
	while (!list_empty(page_list)) {
		struct page *page;
		pr_info("get page %d\n", count);
		page = lru_to_page(page_list);
		pr_info("delete page %d from list\n", count);
		list_del(&page->lru);

		pr_info("check for hugepage\n");
		if (PageTransHuge(page)) {
			pr_info("hugepage\n");
			struct nucleus_hugepage *hp = kzalloc(sizeof(struct nucleus_hugepage), GFP_KERNEL);
			struct page *meta = get_meta_page(page);
			INIT_LIST_HEAD(&hp->bp_list);
			hp->merge_in_hp = false;
			hp->access_freq = meta->total_accesses;
			hp->access_freq_to_move_in = 0;
			hp->num_to_move_in = 0;
			pr_info("add hugepage to all hp list\n");
			list_add_tail(&hp->list_all_hp, &nucleus_hugepages_list);
			pr_info("done add hugepage to all hp list\n");
			for (i = 0; i < HPAGE_PMD_NR; i++) {
				struct nucleus_basepage *bp = kzalloc(sizeof(struct nucleus_basepage), GFP_KERNEL);
				bp->place_in_def = false;
				idx = 4 + i / 4;
				offset = i % 4;
				bp->access_freq = page[idx].compound_pginfo[offset].total_accesses;
				bp->page = nth_page(page, i);
				bp->hp = hp;
				pr_info("add basepage %d to all bp list\n", i);
				list_add_tail(&bp->list_all_bp, &nucleus_basepages_list);
				pr_info("add basepage %d to per hp list\n", i);
				list_add_tail(&bp->list_per_hp, &hp->bp_list);
				pr_info("done add basepage %d to all bp and per hp lists\n", i);
			}
			pr_info("done adding hp and bp\n");
		}
		pr_info("add page %d to lru list\n", count);

		list_add_tail(&page->lru, lru_list_tmp);

		pr_info("done add page %d to lru list\n", count);
		count++;
	}
}

static void process_lru_list(struct pglist_data *pgdat, struct lruvec *lruvec, enum lru_list lru) {
	unsigned long nr_to_scan, scan, nr_scanned = 0, nr_taken;
	LIST_HEAD(page_list);
	LIST_HEAD(lru_list_tmp);
	pr_info("lruvec lru size for lru %d\n", lru);
	nr_to_scan = lruvec_lru_size(lruvec, lru, MAX_NR_ZONES);
	pr_info("lruvec lru size %lu\n", nr_to_scan);

	while (nr_scanned < nr_to_scan) {
		scan = nr_to_scan >> 2;	// isolate 25% pages from the lru list
		if (!scan) {
			scan = nr_to_scan;
		}

		pr_info("scanning %lu out of %lu pages\n", scan, nr_to_scan);

		spin_lock_irq(&lruvec->lru_lock);
		pr_info("isolate lru pages\n");
		nr_taken = isolate_lru_pages(scan, lruvec, lru, &page_list, 0);
		pr_info("mod node page state\n");
		__mod_node_page_state(pgdat, NR_ISOLATED_ANON, nr_taken);
		pr_info("done mod node page state\n");
		spin_unlock_irq(&lruvec->lru_lock);

		pr_info("add to nucleus lists\n");
		add_to_nucleus_lists(&page_list, &lru_list_tmp);
		pr_info("done add to nucleus lists\n");

		spin_lock_irq(&lruvec->lru_lock);
		pr_info("move pages to lru\n");
		move_pages_to_lru(lruvec, &lru_list_tmp);
		pr_info("mod node page state\n");
		__mod_node_page_state(pgdat, NR_ISOLATED_ANON, -nr_taken);
		pr_info("done mod node page state\n");
		spin_unlock_irq(&lruvec->lru_lock);

		nr_scanned += nr_taken;
	}
}

void create_nucleus_input_lists() {
	int nid;
	for_each_node_state(nid, N_MEMORY) {
		struct pglist_data *pgdat = NODE_DATA(nid);
		struct mem_cgroup_per_node *pn;
		struct mem_cgroup *memcg;
		struct lruvec *lruvec;
		
		// Considering only one memcg per node
		// TODO: Change this to support multiple memcgs per node
		pn = next_memcg_cand(pgdat);
		if (!pn) {
			continue;
		}

		memcg = pn->memcg;
		if (!memcg || !memcg->htmm_enabled) {
			continue;
		}

		lruvec = mem_cgroup_lruvec(memcg, pgdat);
		process_lru_list(pgdat, lruvec, LRU_ACTIVE_ANON);
		process_lru_list(pgdat, lruvec, LRU_INACTIVE_ANON);
	}
}

EXPORT_SYMBOL(create_nucleus_input_lists);
