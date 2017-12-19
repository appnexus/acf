#ifndef AN_HRLOCK_H
#define AN_HRLOCK_H
/**
 * Single-writer multiple big-reader lock.
 *
 * hrlocks are similar to a bytelock, but aggregate lock bytes by
 * reader to minimise false sharing.  Dually, hrlocks can be seen as
 * Hashed big Reader locks, hence their name.
 *
 * Given that we are single-writer, each field is only written to by a
 * single thread (the writer for write_depth and the owning reader
 * thread for read_depth) and read by both the writer and the owner,
 * so we get away with only barriers.
 *
 * Another special point is that each "lock" hashes into a table of
 * lock records.  Multiple locks can thus map to the same record. This
 * is only safe because there is a single writer *for all locks backed
 * by the same table*.
 *
 * __ If hrlocks protect an object type, we only support one writer! __
 *
 * Violating the single-writer constraint will let concurrent writes
 * go through and probably leave the hrlock table in an inconsistent
 * state.
 *
 * Another requirement is that lock ordering dependencies must be
 * backed by different tables.  This comes from the fact that multiple
 * objects can hash to the same record.  This does not preclude using
 * hrlocks at multiple levels of a lock hierarchy: we can use hrlocks
 * that are backed by a different hrlock_table, e.g., one table per
 * object type.
 *
 * __ There must not be dependencies between hrlocks in the same table! __
 *
 * Violating the independence constraint should only result in
 * deadlock.
 *
 * The constants are chosen to make false conflicts between readers
 * and writer on the order of 1%, and to maximise cache utilisation.
 * As a side effect, acquiring the write lock is particularly slow,
 * but that's an acceptable trade off for much quicker read
 * acquisition.
 *
 * Note the atomic load/stores to each hrlock's hash value: we use the
 * top bit of that field to mark locks that are locked for writes.  We
 * like to check for locks being held (or not) in asserts, and using
 * the hashed record's state would cause false positives: a given
 * record can be acquired for locking for one hrlock but not for
 * another that happens to hash to the same record.
 */

#include <ck_cc.h>
#include <ck_pr.h>
#include <stdbool.h>
#include <stdint.h>

#include "common/an_hook.h"
#include "common/an_latency.h"
#include "common/an_thread.h"

/* Must be a power of two. */
#define AN_HRLOCK_COUNT 128

struct an_hrlock_record {
	uint8_t write_depth;
	uint8_t read_depth;
};

struct an_hrlock_table {
	struct an_hrlock_record records[AN_THREAD_LIMIT * AN_HRLOCK_COUNT];
};

typedef struct an_hrlock_table an_hrlock_table_t;

typedef struct an_hrlock {
	unsigned int hash;
} an_hrlock_t;

void an_hrlock_init(an_hrlock_t *lock);

CK_CC_INLINE static void
an_hrlock_latency_init(an_hrlock_t *lock,
    an_latency_context_t wa, an_latency_context_t wh,
    an_latency_context_t ra, an_latency_context_t rh)
{

	(void)lock;
	(void)wa;
	(void)wh;
	(void)ra;
	(void)rh;

	return;
}

void an_hrlock_write_lock_impl(an_hrlock_t *lock, an_hrlock_table_t *table);

CK_CC_INLINE static void
an_hrlock_write_lock(an_hrlock_t *lock, an_hrlock_table_t *table)
{

        AN_HOOK(an_disabled_on_startup, enable_writelock) {
                an_hrlock_write_lock_impl(lock, table);
        }
}

void an_hrlock_write_unlock_impl(an_hrlock_t *lock, an_hrlock_table_t *table);

CK_CC_INLINE static void
an_hrlock_write_unlock(an_hrlock_t *lock, an_hrlock_table_t *table)
{

        AN_HOOK(an_disabled_on_startup, enable_writelock) {
                an_hrlock_write_unlock_impl(lock, table);
        }
}

void an_hrlock_read_lock_all(an_hrlock_table_t *table);
bool an_hrlock_read_trylock_all(an_hrlock_table_t *table);
void an_hrlock_read_unlock_all(an_hrlock_table_t *table);

CK_CC_INLINE static bool
an_hrlock_read_lock_timeout(const an_hrlock_t *lock, an_hrlock_table_t *table, uint64_t timeout_us)
{
	extern bool an_hrlock_read_lock_slow(struct an_hrlock_record *, uint64_t timeout_us);

	struct an_hrlock_record *record;
	unsigned int h = ck_pr_load_uint(&lock->hash) % AN_HRLOCK_COUNT;
	unsigned int id = current->id;
	uint8_t old_depth;

	record = &table->records[id * AN_HRLOCK_COUNT + h];
#if defined(__x86__) || defined(__x86_64__)
	/* atomic increment is quicker than store + fence on x86oids (as of 2014). */
	old_depth = ck_pr_faa_8(&record->read_depth, 1);
#else
       old_depth = ck_pr_load_8(&record->read_depth);
       ck_pr_store_8(&record->read_depth, old_depth + 1);
#endif

	if (CK_CC_LIKELY(old_depth == 0)) {
#if !(defined(__x86__)  || defined(__x86_64__))
		ck_pr_fence_store_load();
#endif
		if (CK_CC_UNLIKELY(ck_pr_load_8(&record->write_depth) != 0)) {
			if (an_hrlock_read_lock_slow(record, timeout_us) == false) {
				return false;
			}
		}

		ck_pr_fence_load();
	}

	an_thread_push(an_hrlock_read_unlock_all, table);
	return true;
}

CK_CC_INLINE static void
an_hrlock_read_lock(const an_hrlock_t *lock, an_hrlock_table_t *table)
{

	(void)an_hrlock_read_lock_timeout(lock, table, UINT64_MAX);
	return;
}

CK_CC_INLINE static bool
an_hrlock_read_trylock(const an_hrlock_t *lock, an_hrlock_table_t *table)
{

	return an_hrlock_read_lock_timeout(lock, table, 0);
}

CK_CC_INLINE static void
an_hrlock_read_unlock(const an_hrlock_t *lock, an_hrlock_table_t *table)
{
	struct an_hrlock_record *record;
	unsigned int h = ck_pr_load_uint(&lock->hash) % AN_HRLOCK_COUNT;
	unsigned int id = current->id;
	unsigned char old_depth;

	an_thread_pop(an_hrlock_read_unlock_all, table);
	record = &table->records[id * AN_HRLOCK_COUNT + h];
	ck_pr_fence_release();
	old_depth = ck_pr_load_8(&record->read_depth);
	ck_pr_store_8(&record->read_depth, old_depth - 1);

	return;
}
#endif /* !AN_HRLOCK_H */
