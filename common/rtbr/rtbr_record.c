#include <assert.h>
#include <ck_pr.h>

#include "common/an_cc.h"
#include "common/rtbr/rtbr_impl.h"

bool
an_rtbr_record_acquire(struct an_rtbr_record *record, const struct an_rtbr_tid_info *info)
{
	uint64_t expected[2] = {0, 0};
	uint64_t new[2];
	bool ret;

	assert(info->dead == false);
	assert(info->tid != 0);
	assert(info->start_time != 0);

	new[0] = info->tid;
	new[1] = info->start_time;
	ret = ck_pr_cas_64_2(record->lock, expected, new);
	if (ret) {
		an_pr_store_ptr(&record->stack_entry.next, NULL);
		ck_pr_fence_load();
		ck_pr_fence_store();

		TAILQ_INIT(&record->active);
		ck_pr_store_64(&record->active_count, 0);

		STAILQ_INIT(&record->limbo);
		ck_pr_store_64(&record->limbo_count, 0);
	}

	return ret;
}
