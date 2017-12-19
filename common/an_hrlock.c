#include <assert.h>
#include <limits.h>
#include <stddef.h>

#include "common/an_hrlock.h"
#include "common/an_md.h"
#include "common/util.h"

void
an_hrlock_init(an_hrlock_t *lock)
{
	unsigned int hash;

	hash = an_rand();
	ck_pr_store_uint(&lock->hash, hash);
	ck_pr_barrier();
	return;
}

void
an_hrlock_write_lock_impl(an_hrlock_t *lock, an_hrlock_table_t *table)
{
	struct an_hrlock_record *record;
	unsigned int h = ck_pr_load_uint(&lock->hash) % AN_HRLOCK_COUNT;
	unsigned int id = current->id;
	uint8_t old_depth = 0;

	for (size_t i = 0; i < AN_THREAD_LIMIT; i++) {
		record = &table->records[i * AN_HRLOCK_COUNT + h];
		old_depth = ck_pr_load_8(&record->write_depth);
		ck_pr_store_8(&record->write_depth, old_depth + 1);
	}

	if (CK_CC_UNLIKELY(old_depth > 0)) {
		an_thread_push_poison(an_hrlock_write_unlock_impl);
		return;
	}

	ck_pr_fence_store_load();
	for (size_t i = 0; i < AN_THREAD_LIMIT; i++) {
		record = &table->records[i * AN_HRLOCK_COUNT + h];

		if (i == id) { /* Skip self. */
			continue;
		}

		while (ck_pr_load_8(&record->read_depth) != 0) {
			ck_pr_stall();
		}
	}

	an_thread_push_poison(an_hrlock_write_unlock_impl);
	return;
}

void
an_hrlock_write_unlock_impl(an_hrlock_t *lock, an_hrlock_table_t *table)
{
	struct an_hrlock_record *record;
	unsigned int h = ck_pr_load_uint(&lock->hash) % AN_HRLOCK_COUNT;
	uint8_t old_depth;

	ck_pr_fence_release();
	an_thread_pop_poison(an_hrlock_write_unlock_impl);
	for (size_t i = 0; i < AN_THREAD_LIMIT; i++) {
		record = &table->records[i * AN_HRLOCK_COUNT + h];
		old_depth = ck_pr_load_8(&record->write_depth);
		ck_pr_store_8(&record->write_depth, old_depth - 1);
	}

	return;
}

bool
an_hrlock_read_lock_slow(struct an_hrlock_record *record, uint64_t timeout_us)
{
	uint64_t begin = 0, ticks = 0;

	ck_pr_store_8(&record->read_depth, 0);
	if (timeout_us == 0) {
		return false;
	}

	if (timeout_us != UINT64_MAX) {
		ticks = an_md_us_to_rdtsc(timeout_us);
		if (ticks == 0) {
			ticks = 1;
		}

		begin = an_md_rdtsc();
	}

	for (;;) {
		for (;;) {
			if (ticks != 0) {
				uint64_t delta;

				delta = an_md_rdtsc() - begin;
				if (delta > ticks) {
					return false;
				}
			}

			for (size_t i = 0; i < 128; i++) {
				if (ck_pr_load_8(&record->write_depth) == 0) {
					goto try;
				}

				ck_pr_stall();
			}
		}

try:
		ck_pr_store_8(&record->read_depth, 1);
		ck_pr_fence_store_load();
		if (ck_pr_load_8(&record->write_depth) == 0) {
			break;
		}

		ck_pr_store_8(&record->read_depth, 0);
	}

	return true;
}

static size_t
an_hrlock_read_maybe_lock_all(an_hrlock_table_t *table, uint64_t timeout_us)
{

	/* XXX: HACK HACK HACK. Clean this up if we need it for a good reason. */
	for (size_t i = 0; i < AN_HRLOCK_COUNT; i++) {
		struct an_hrlock lock = { .hash = i };

		if (an_hrlock_read_lock_timeout(&lock, table, timeout_us) == false) {
			return i;
		}

		an_thread_pop(an_hrlock_read_unlock_all, table);
	}

	return AN_HRLOCK_COUNT;
}

void
an_hrlock_read_lock_all(an_hrlock_table_t *table)
{

	(void)an_hrlock_read_maybe_lock_all(table, UINT64_MAX);
	return;
}

void
an_hrlock_read_unlock_all(an_hrlock_table_t *table)
{
	unsigned int id = current->id;

	for (size_t i = 0; i < AN_HRLOCK_COUNT; i++) {
		ck_pr_store_8(&table->records[id * AN_HRLOCK_COUNT + i].read_depth, 0);
	}

	ck_pr_fence_release();
	return;
}

bool
an_hrlock_read_trylock_all(an_hrlock_table_t *table)
{
	size_t locked_count;
	unsigned int id = current->id;

	locked_count = an_hrlock_read_maybe_lock_all(table, 0);

	if (locked_count == AN_HRLOCK_COUNT) {
		return true;
	}

	/*
	 * We were not able to lock all of the members, we need to free the ones
	 * that we previously locked
	 */
	for (size_t i = 0; i < locked_count; i++) {
		ck_pr_store_8(&table->records[id * AN_HRLOCK_COUNT + i].read_depth, 0);
	}

	return false;
}
