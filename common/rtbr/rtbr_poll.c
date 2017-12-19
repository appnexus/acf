#include <assert.h>
#include <ck_pr.h>
#include <inttypes.h>

#include "common/an_cc.h"
#include "common/an_md.h"
#include "common/an_syslog.h"
#include "common/rtbr/rtbr_impl.h"

static void
publish(struct ck_stack *freelist, struct an_rtbr_record *record, uint64_t id)
{

	if (ck_pr_load_64(&record->id) != id) {
		ck_pr_store_64(&record->id, id);
	}

	ck_stack_push_mpmc(freelist, &record->stack_entry);
	return;
}

static void
poll_slice_hard(struct an_rtbr_slice *slice, struct an_rtbr_record *self, uint64_t ts,
    struct ck_stack *freelist, bool dump_all)
{
	uint64_t allocated;
	uint64_t n;

	allocated = ck_pr_load_64(&slice->allocated_records);
	n = ck_pr_load_64(&slice->n_records);
	if (allocated > n) {
		allocated = n;
	}

	for (uint64_t i = 0; i < allocated; i++) {
		struct an_rtbr_tid_info info;
		struct an_rtbr_record *record = &slice->records[i];
		uint64_t lock[2];
		uint64_t id = slice->id_offset + i;
		uint64_t epoch = ck_pr_load_64(&record->self_epoch);
		uint64_t last_epoch = ck_pr_load_64(&record->last_self_epoch);

		lock[0] = ck_pr_load_64(&record->lock[0]);
		lock[1] = ck_pr_load_64(&record->lock[1]);
		if (lock[0] == 0 && lock[1] == 0) {
			ck_pr_store_64(&record->last_safe, ts);
			if (dump_all) {
				publish(freelist, record, id);
			}

			continue;
		}

		/*
		 * Check for the easy case before reading the tid's
		 * sched stats.  If the record's epoch counter isn't
		 * stuck, we must be making progress.
		 */
		if (epoch != last_epoch) {
			ck_pr_store_64(&record->last_self_epoch, epoch);
			continue;
		}

		info = an_rtbr_tid_info(lock[0]);
		if (info.dead || info.start_time != lock[1]) {
			assert(record != self);

			an_syslog(LOG_INFO, "common/rtbr: record %"PRIu64" (tid %"PRIu64") "
			    "reclaimed record %"PRIu64" from dead tid %"PRIu64".",
			    self->id, self->lock[0],
			    id, lock[0]);

			ck_pr_store_64(&record->last_safe, ts);
			TAILQ_INIT(&record->active);
			ck_pr_store_64(&record->active_count, 0);

			if (record->limbo.stqh_last != NULL) {
				STAILQ_CONCAT(&self->limbo, &record->limbo);
				ck_pr_store_64(&self->limbo_count,
				    self->limbo_count + ck_pr_load_64(&record->limbo_count));
			}

			STAILQ_INIT(&record->limbo);
			ck_pr_store_64(&record->limbo_count, 0);

			ck_pr_fence_store();
			ck_pr_store_64(&record->lock[0], 0);
			ck_pr_store_64(&record->lock[1], 0);
			ck_pr_fence_store();

			publish(freelist, record, id);
			continue;
		}

		epoch = ck_pr_load_64(&record->self_epoch);
		if (epoch != last_epoch) {
			ck_pr_store_64(&record->last_self_epoch, epoch);
			continue;
		}

		if (epoch & 1) {
			continue;
		}

		if (info.running == false && ts > record->last_safe) {
			/*
			 * No progress since the last poll, and the
			 * thread was sleeping between ts and the time
			 * we noticed it hadn't made any progress.
			 * There was no section in flight at time ts.
			 */
			ck_pr_store_64(&record->last_safe, ts);
		} else if (info.total_time != record->total_time &&
		    record->as_of > record->last_safe) {
			/*
			 * No progress since last poll, and there has
			 * been at least one context switch since
			 * as_of.  We know there was no section in
			 * flight at time as_of.
			 */
			ck_pr_store_64(&record->last_safe, record->as_of);
		}

		record->total_time = info.total_time;
		record->as_of = ts;
	}

	return;
}

bool
an_rtbr_poll_hard(struct an_rtbr_global *global, struct an_rtbr_record *self)
{
	uint64_t delay;
	uint64_t ts;
	bool dump_all;

	ts = an_md_rdtsc();
	delay = an_md_us_to_rdtsc(RTBR_HARD_POLL_PERIOD_MS * 1000);

	/* No need to hammer the system with really frequent hard polls. */
	if (ck_pr_load_64(&global->last_hard_poll) + delay > ts ||
	    ck_spinlock_trylock(&global->lock) == false) {
		return false;
	}

	/*
	 * If the free list is empty, aggressively push anything that
	 * is dead on that list.  Otherwise, only push records that
	 * transitioned from alive to dead (and thus definitely isn't
	 * already on the free list).
	 */
	dump_all = (an_pr_load_ptr(&global->freelist.head) == NULL);
	for (size_t i = 0; i < ARRAY_SIZE(global->slices); i++) {
		struct an_rtbr_slice *slice;

		slice = an_pr_load_ptr(&global->slices[i]);
		if (slice == NULL) {
			break;
		}

		ck_pr_fence_load();
		poll_slice_hard(slice, self, ts, &global->freelist, dump_all);
	}

	ck_pr_store_64(&global->last_hard_poll, ts);
	ck_spinlock_unlock(&global->lock);
	return true;
}

static uint64_t
poll_slice_easy(const struct an_rtbr_slice *slice)
{
	uint64_t ret = UINT64_MAX;
	uint64_t allocated = ck_pr_load_64(&slice->allocated_records);
	uint64_t n = ck_pr_load_64(&slice->n_records);

	if (allocated > n) {
		allocated = n;
	}

	for (uint64_t i = 0; i < allocated; i++) {
		const struct an_rtbr_record *record = &slice->records[i];
		uint64_t epoch;
		uint64_t last_safe;

		if (ck_pr_load_64(&record->lock[0]) == 0 &&
		    ck_pr_load_64(&record->lock[1]) == 0) {
			continue;
		}

		epoch = ck_pr_load_64(&record->self_epoch);
		last_safe = ck_pr_load_64(&record->last_safe);
		if (last_safe > epoch) {
			epoch = last_safe;
		}

		if (epoch < ret) {
			ret = epoch;
		}
	}

	return ret;
}

uint64_t
an_rtbr_poll_easy(struct an_rtbr_global *global)
{
	uint64_t delay;
	uint64_t last;
	uint64_t latest = an_md_rdtsc();
	uint64_t ret;

	for (size_t i = 0; i < ARRAY_SIZE(global->slices); i++) {
		struct an_rtbr_slice *slice;
		uint64_t current;

		slice = an_pr_load_ptr(&global->slices[i]);
		if (slice == NULL) {
			break;
		}

		current = poll_slice_easy(slice);
		if (current < latest) {
			latest = current;
		}
	}

	delay = RTBR_DELAY_TICKS;
	if (latest <= delay) {
		ret = 0;
	} else {
		ret = latest - delay;
	}

	last = ck_pr_load_64(&global->global_epoch);
	while (1) {
		if (ret < last) {
			return last;
		}

		if (ck_pr_cas_64_value(&global->global_epoch, last, ret, &last)) {
			return ret;
		}

		ck_pr_stall();
	}

	return last;
}
