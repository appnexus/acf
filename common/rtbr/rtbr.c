#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "common/an_cc.h"
#include "common/an_md.h"
#include "common/an_syslog.h"
#include "common/rtbr/rtbr.h"
#include "common/rtbr/rtbr_impl.h"
#include "common/util.h"

static struct an_rtbr_global rtbr_global = {
	.lock = CK_SPINLOCK_INITIALIZER
};

static __thread struct an_rtbr_record *rtbr_self = NULL;

static inline struct an_rtbr_record *
an_rtbr_ensure(void)
{
	struct an_rtbr_record *self = rtbr_self;

	if (AN_CC_UNLIKELY(self == NULL)) {
		uint64_t record_count;
		uint64_t new_count;

		self = an_rtbr_get_record(&rtbr_global);
		assert(self != NULL);

		rtbr_self = self;

		new_count = self->id + 1;
		record_count = ck_pr_load_ptr(&rtbr_global.record_count);
		while (new_count > record_count &&
		    ck_pr_cas_64_value(&rtbr_global.record_count, record_count, new_count,
			&record_count) == false) {
			ck_pr_stall();
		}
	}

	return self;
}

/*
 * This... is a hack.  We want a way to forcefully ignore really old
 * sections, because we'd rather risk a crash than definitely OOM.  We
 * just pop from the head while it is too old.
 */
static void
cleanup_stale_sections(struct an_rtbr_record *restrict self)
{
	uint64_t now = 0;
	uint64_t min_epoch = ck_pr_load_64(&rtbr_global.minimal_epoch);
	size_t reclaimed = 0;

	if (AN_CC_LIKELY(TAILQ_EMPTY(&self->active) ||
	    TAILQ_FIRST(&self->active)->timestamp.timestamp >= min_epoch)) {
		return;
	}

	while (TAILQ_EMPTY(&self->active) == false) {
		struct an_rtbr_section *restrict head = TAILQ_FIRST(&self->active);

		if (head->timestamp.timestamp >= min_epoch) {
			break;
		}

		if (now == 0) {
			now = an_md_rdtsc();
		}

		head->cookie = NULL;
		head->info[sizeof(head->info) - 1] = '\0';
		an_syslog(LOG_CRIT, "common/rtbr: record %"PRIu64" (tid %"PRIu64") forcibly removed "
		    "%.6f second old section (info = \"%s\") for min_epoch (#%zu).",
		    self->id, self->lock[0],
		    an_md_rdtsc_scale(now - min(now, head->timestamp.timestamp)) * 1e-6,
		    head->info, ++reclaimed);

		TAILQ_REMOVE(&self->active, head, next);
		if (self->active_count > 0) {
			ck_pr_store_64(&self->active_count, self->active_count - 1);
		}
	}

	if (TAILQ_EMPTY(&self->active)) {
		ck_pr_store_64(&self->active_count, 0);
	}

	return;
}

static void
an_rtbr_update(struct an_rtbr_record *restrict self, bool poll)
{
	uint64_t self_epoch;
	uint64_t update;

	cleanup_stale_sections(self);
	self_epoch = self->self_epoch;
	update = self_epoch;

	if (TAILQ_EMPTY(&self->active) == false) {
		/* OR in the low bit to mark us active. */
		update = TAILQ_FIRST(&self->active)->timestamp.timestamp | 1;
	} else if (poll) {
		/* No active section, but we're polling. Let's move forward but remain even (inactive). */
		update = max(self_epoch, an_md_rdtsc_fast()) & ~1ULL;
	} else if ((update & 1) != 0) {
		/* Minimal work to mark us inactive without rolling back the clock. */
		update++;
	}

	if (update != self_epoch) {
		ck_pr_store_64(&self->self_epoch, update);
	}

	return;
}

struct an_rtbr_timestamp
an_rtbr_prepare(void)
{
	struct an_rtbr_timestamp ret;

	ret.timestamp = an_md_rdtsc_fast();
	return ret;
}

void
an_rtbr_begin(struct an_rtbr_section *restrict section, struct an_rtbr_timestamp timestamp,
    const char *restrict info)
{
	struct an_rtbr_record *restrict self;

	self = rtbr_self;
	section->timestamp = timestamp;
	/* Set cookie later to expose more ILP. */
	section->info[0] = '\0';
	if (info != NULL) {
		strncpy(section->info, info, sizeof(section->info) - 1);
		section->info[sizeof(section->info) - 1] = '\0';
	}

	if (AN_CC_UNLIKELY(self == NULL)) {
		self = an_rtbr_ensure();
	}

	section->cookie = self;
	if (TAILQ_EMPTY(&self->active)) {
		if (info != NULL) {
			memcpy(&self->info, &section->info, sizeof(self->info));
		} else {
			self->info[0] = '\0';
		}
	}

	TAILQ_INSERT_TAIL(&self->active, section, next);
	ck_pr_store_64(&self->active_count, self->active_count + 1);
	an_rtbr_update(self, false);
	return;
}

void
an_rtbr_end(struct an_rtbr_section *restrict section)
{
	struct an_rtbr_record *restrict self;
	bool oldest;

	if (section->cookie == NULL) {
		return;
	}

	self = an_rtbr_ensure();
	assert(section->cookie == self);

	oldest = (TAILQ_FIRST(&self->active) == section);
	TAILQ_REMOVE(&self->active, section, next);
	if (TAILQ_EMPTY(&self->active)) {
		ck_pr_store_64(&self->active_count, 0);
	} else if (self->active_count != 0) {
		ck_pr_store_64(&self->active_count, self->active_count - 1);
	}

	an_rtbr_update(self, false);
	if (oldest) {
		struct an_rtbr_section *first = TAILQ_FIRST(&self->active);

		if (first == NULL || first->info[0] == '\0') {
			self->info[0] = '\0';
		} else {
			memcpy(&self->info, &first->info, sizeof(self->info));
			self->info[sizeof(self->info) - 1] = '\0';
		}
	}

	return;
}

void
an_rtbr_call(struct an_rtbr_entry *restrict entry, void (*fn)(void *), void *object)
{
	struct an_rtbr_record *restrict self;

	self = an_rtbr_ensure();
	memset(entry, 0, sizeof(*entry));
	entry->function = fn;
	entry->argument = object;
	entry->timestamp = an_md_rdtsc_fast();

	STAILQ_INSERT_TAIL(&self->limbo, entry, next);
	ck_pr_store_64(&self->limbo_count, self->limbo_count + 1);
	return;
}

uint64_t
an_rtbr_active(const struct an_rtbr_record *record)
{

	if (record == NULL) {
		if (rtbr_self == NULL) {
			return 0;
		}

		record = rtbr_self;
	}

	return ck_pr_load_64(&record->active_count);
}

const char *
an_rtbr_info(const struct an_rtbr_record *record)
{

	if (record == NULL) {
		return NULL;
	}

	if (record->info[sizeof(record->info) - 1] != 0) {
		/* This is fine, we're just being paranoid for our consumers. */
		ck_pr_store_char((char *)&record->info[sizeof(record->info) - 1], '\0');
	}

	return record->info;
}

uint64_t
an_rtbr_epoch(void)
{

	return an_rtbr_ensure()->global_epoch;
}

uint64_t
an_rtbr_local_epoch(const struct an_rtbr_record *record)
{

	if (record == NULL) {
		struct an_rtbr_record *self;

		self = an_rtbr_ensure();
		an_rtbr_update(self, true);
		record = self;
	}

	return max(ck_pr_load_64(&record->self_epoch), ck_pr_load_64(&record->last_safe));
}

bool
an_rtbr_poll(bool hard)
{
	struct an_rtbr_record *self;
	bool ret = false;

	self = an_rtbr_ensure();
	an_rtbr_update(self, true);
	if (hard) {
		uint64_t old_epoch = self->global_epoch;
		bool advanced;

		advanced = an_rtbr_poll_hard(&rtbr_global, self);
		self->global_epoch = an_rtbr_poll_easy(&rtbr_global);

		if (advanced && self->global_epoch > old_epoch) {
			uint64_t now = an_md_rdtsc();

			an_syslog(LOG_INFO, "common/rtbr: record %"PRIu64" (tid %"PRIu64") "
			    "polled epoch to %"PRIu64" "
			    "(%.6f sec in the past, self is %.6f sec behind).",
			    self->id, self->lock[0],
			    self->global_epoch,
			    an_md_rdtsc_scale(now - min(now, self->global_epoch)) * 1e-6,
			    an_md_rdtsc_scale(now - min(now, self->self_epoch)) * 1e-6);
		}
	} else {
		uint64_t global_epoch = ck_pr_load_64(&rtbr_global.global_epoch);

		if (global_epoch == self->global_epoch) {
			global_epoch = an_rtbr_poll_easy(&rtbr_global);
		}

		/* No forward progress, and we're really behind ourself. Let's do it the hard way. */
		if (global_epoch == self->global_epoch) {
			if (global_epoch < self->self_epoch &&
			    (self->self_epoch - global_epoch) > an_md_us_to_rdtsc(RTBR_HARD_POLL_PERIOD_MS * 1000)) {
				return an_rtbr_poll(true);
			}
		}

		self->global_epoch = global_epoch;
	}

	while (STAILQ_EMPTY(&self->limbo) == false) {
		struct an_rtbr_entry *head;

		head = STAILQ_FIRST(&self->limbo);
		if ((int64_t)(head->timestamp - self->global_epoch) >= 0) {
			break;
		}

		STAILQ_REMOVE_HEAD(&self->limbo, next);
		if (self->limbo_count > 0) {
			ck_pr_store_64(&self->limbo_count, self->limbo_count - 1);
		}

		head->function(head->argument);
		ret = true;
	}

	if (STAILQ_EMPTY(&self->limbo)) {
		ck_pr_store_64(&self->limbo_count, 0);
	}

	return ret;
}

void
an_rtbr_synchronize(void)
{
	struct an_rtbr_record *self = rtbr_self;
	size_t i = 0;

	if (self == NULL) {
		return;
	}

	an_rtbr_poll(true);
	while (STAILQ_FIRST(&self->limbo) != NULL) {
		/* If we're stalled on ourselves, stop. */
		if (TAILQ_EMPTY(&self->active) == false &&
		    (int64_t)(STAILQ_FIRST(&self->limbo)->timestamp - self->self_epoch) >= 0) {
			break;
		}

		if ((++i % 1000) == 0) {
			an_syslog(LOG_CRIT, "common/rtbr: record %"PRIu64" (tid %"PRIu64") "
			    "failed to synchronize after %zu iterations of poll.",
			    self->id, self->lock[0],
			    i);
		}

		usleep(1000);
		an_rtbr_poll(true);
	}

	return;
}

void
an_rtbr_force_progress(uint64_t latency_ms)
{
	struct an_rtbr_record *self = rtbr_self;
	uint64_t current_min;
	uint64_t delay;
	uint64_t min_epoch;
	uint64_t now;

	delay = an_md_us_to_rdtsc(latency_ms * 1000);
	now = an_md_rdtsc();
	if (delay >= now) {
		return;
	}

	current_min = ck_pr_load_64(&rtbr_global.minimal_epoch);
	min_epoch = now - delay;
	while (min_epoch > current_min &&
	    ck_pr_cas_64_value(&rtbr_global.minimal_epoch, current_min, min_epoch,
	    &current_min) == false) {
		ck_pr_stall();
	}

	if (min_epoch > current_min) {
		an_syslog(LOG_CRIT, "common/rtbr: forced progress to (now - %"PRIu64" ms) "
		    "for min_epoch = %"PRIu64".",
		    latency_ms, min_epoch);
	}

	if (self != NULL) {
		an_rtbr_update(self, false);
	}

	return;
}

uint64_t
an_rtbr_record_count(void)
{

	return ck_pr_load_64(&rtbr_global.record_count);
}

const struct an_rtbr_record *
an_rtbr_self(void)
{

	return an_rtbr_ensure();
}

uint64_t
an_rtbr_id(void)
{

	return an_rtbr_ensure()->id;
}

uint64_t
an_rtbr_record_id(const struct an_rtbr_record *record)
{

	if (record == NULL) {
		return UINT64_MAX;
	}

	return ck_pr_load_64(&record->id);
}

bool
an_rtbr_record_oldest_section(struct an_rtbr_section *dst, const struct an_rtbr_record *record)
{
	const struct an_rtbr_section *head;

	memset(dst, 0, sizeof(*dst));
	if (record == NULL) {
		return false;
	}

	head = an_pr_load_ptr(&record->active.tqh_first);
	if (head == NULL) {
		return false;
	}

	memcpy(dst, head, sizeof(*dst));
	memset(&dst->next, 0, sizeof(dst->next));
	if (dst->cookie == record &&
	    head == an_pr_load_ptr(&record->active.tqh_first)) {
		return true;
	}

	memset(dst, 0, sizeof(*dst));
	return false;
}

const struct an_rtbr_record *
an_rtbr_iterate(struct an_rtbr_iterator *iterator)
{
	struct an_rtbr_slice *slice;
	size_t slice_id;
	size_t record_index;

	while (1) {
		size_t allocated_records;
		size_t n_records;

		slice_id = iterator->slice;
		record_index = iterator->index;
		if (slice_id >= ARRAY_SIZE(rtbr_global.slices)) {
			return NULL;
		}

		slice = an_pr_load_ptr(&rtbr_global.slices[slice_id]);
		if (slice == NULL) {
			return NULL;
		}

		allocated_records = ck_pr_load_64(&slice->allocated_records);
		n_records = ck_pr_load_64(&slice->n_records);
		if (record_index >= allocated_records) {
			allocated_records = n_records;
		}

		if (record_index < allocated_records) {
			struct an_rtbr_record *ret = &slice->records[record_index];

			iterator->index++;
			if (ck_pr_load_64(&ret->lock[0]) != 0 &&
			    ck_pr_load_64(&ret->lock[1]) != 0) {
				return ret;
			}
		} else {
			iterator->index = 0;
			iterator->slice++;
		}
	}

	return NULL;
}

void
an_rtbr_reinit(void)
{
	struct an_rtbr_record copy;
	bool has_copy = false;

	if (rtbr_self != NULL) {
		memcpy(&copy, rtbr_self, sizeof(copy));
		has_copy = true;
		rtbr_self = NULL;
	}

	an_rtbr_reinit_impl(&rtbr_global);
	if (has_copy) {
		struct an_rtbr_record *self;

		self = an_rtbr_ensure();
		TAILQ_CONCAT(&self->active, &copy.active, next);
		ck_pr_store_64(&self->active_count, copy.active_count);
		STAILQ_CONCAT(&self->limbo, &copy.limbo);
		ck_pr_store_64(&self->limbo_count, copy.limbo_count);
	}

	return;
}
