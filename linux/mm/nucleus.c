#include <linux/huge_mm.h>
#include <linux/nucleus.h>
#include <linux/mempolicy.h>

struct list_head nucleus_hugepages_list = LIST_HEAD_INIT(nucleus_hugepages_list);
struct list_head nucleus_basepages_list = LIST_HEAD_INIT(nucleus_basepages_list);

void add_to_nucleus_lists(struct list_head* page_list, struct list_head* lru_list_tmp) {
	int i, idx, offset;
	while (!list_empty(&page_list)) {
		struct page *page;

		page = lru_to_page(&page_list);
		list_del(&page->lru);

		if (PageTransHuge(compound_head(page))) {
			struct nucleus_basepage *hp = kzalloc(sizeof(struct nucleus_basepage), GFP_KERNEL);
			hp->merge_in_hp = false;
			struct page *meta = get_meta_page(page);
			hp->access_freq = meta->total_accesses;
			hp->access_freq_to_move_in = 0;
			hp->num_to_move_in = 0;
			list_add_tail(&hp->list_all_hp, &nucleus_hugepages_list);
			for (i = 0; i < HPAGE_PMD_NR; i++) {
				struct nucleus_basepage *bp = kzalloc(sizeof(struct nucleus_basepage), GFP_KERNEL);
				bp->place_in_def = false;
				idx = 4 + i / 4;
				offset = i % 4;
				bp->access_freq = page[idx].compound_pginfo[offset].total_accesses;
				bp->page = nth_page(page, i);
				list_add_tail(&bp->list_all_bp, &nucleus_basepages_list);
				list_add_tail(&bp->list_per_hp, &hp->basepages_list);
			}
		}

		list_add_tail(&page->lru, lru_list_tmp);
	}
}

void process_lru_list(struct lruvec *lruvec, enum lru_list lru) {
	unsigned long nr_to_scan, scan, nr_scanned = 0;
	nr_to_scan = lruvec_lru_size(lruvec, lru, MAX_NR_ZONES);
	LIST_HEAD(page_list);
	LIST_HEAD(lru_list_tmp);

	while (nr_scanned < nr_to_scan) {
		scan = nr_to_scan >> 2;	// isolate 25% pages from the lru list
		if (!scan) {
			scan = nr_to_scan;
		}

		spin_lock_irq(&lruvec->lru_lock);
		nr_taken = isolate_lru_pages(nr_to_scan, lruvec, lru, &page_list, 0);
		__mod_node_page_state(pgdat, NR_ISOLATED_ANON, nr_taken);
		spin_unlock_irq(&lruvec->lru_lock);

		add_to_nucleus_lists(&page_list, &lru_list_tmp);

		spin_lock_irq(&lruvec->lru_lock);
		move_pages_to_lru(lruvec, &lru_list_tmp);
		__mod_node_page_state(pgdat, NR_ISOLATED_ANON, -nr_taken);
		spin_unlock_irq(&lruvec->lru_lock);

		nr_scanned += nr_taken;
	}
}

void create_nucleus_input_lists() {
	int nid;
	for_each_node_state(nid, N_MEMORY) {
		struct pglist_data *pgdat = NODE_DATA(nid);
		
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

		struct lruvec *lruvec = mem_cgroup_lruvec(memcg, pgdat);
		process_lru_list(lruvec, LRU_ACTIVE_ANON);
		process_lru_list(lruvec, LRU_INACTIVE_ANON);
    }
}
