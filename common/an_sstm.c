#include <assert.h>
#include <ck_cc.h>
#include <ck_stack.h>
#include <event2/event.h>
#include <stdint.h>
#include <sys/queue.h>

#include "common/an_array.h"
#include "common/an_hook.h"
#include "common/an_malloc.h"
#include "common/an_md.h"
#include "common/an_smr.h"
#include "common/an_sstm.h"
#include "common/an_thread.h"
#include "common/util.h"

struct barrier_record {
	/* Is the thread active? */
	uint8_t active;
	/* Are we waiting for this thread to ack the barrier? */
	uint8_t flag;
	struct event timer;
};

/**
 * Variable length private (writer-only) record.
 *
 * The linked list of sstm_record is a stack of objects to commit.
 *
 * ops is your usual vtable, and release_offset is the offset of the
 * release data in buf.
 *
 * object_backref points back to where we should commit the changes,
 * and link_backref to the an_sstm_linkage field for that object.
 */
struct sstm_record {
	SLIST_ENTRY(sstm_record) links;
	const struct an_sstm_ops *ops;
	size_t release_offset;
	void *object_backref;
	struct an_sstm_linkage *link_backref;
	char buf[] CK_CC_ALIGN(16);
};

/**
 * Fixed-size cleanup records.  If size is 0 or greater than
 * CLEANUP_INLINE_SIZE, clean up by passing ptr to cb.  If size is
 * greater than zero, free ptr.
 *
 * Otherwise, 0 < size <= 16, and pass buf directly to cb: we copied
 * the data inline when registering this cleanup.
 */
#define CLEANUP_INLINE_SIZE 16

struct cleanup {
	void (*cb)(void *);
	size_t size;
	union {
		void *ptr;
		char buf[CLEANUP_INLINE_SIZE];
	} CK_CC_ALIGN(16);
};

/**
 * an_array_cleanup: stack of cleanups
 */
AN_ARRAY(cleanup, cleanup);

struct trivial_commit {
	struct an_sstm_linkage *link;
	void *object;
	void (*freeze)(void *);
};

AN_ARRAY(trivial_commit, trivial);

/**
 * records: list of sstm records.
 */
SLIST_HEAD(records, sstm_record);

struct sstm_smr_entry {
	struct records records;
	struct an_array_cleanup cleanups;
};

static struct records commit_list = SLIST_HEAD_INITIALIZER(commit_list);
static struct an_array_cleanup cleanups;
static struct an_array_trivial trivial_commits;

static AN_MALLOC_DEFINE(cleanup_buf_token,
    .string = "cleanup_buf",
    .mode = AN_MEMORY_MODE_VARIABLE);

static AN_MALLOC_DEFINE(smr_entry_token,
    .string  = "sstm_smr_entry_t",
    .mode = AN_MEMORY_MODE_FIXED,
    .size = sizeof(struct sstm_smr_entry));

static struct barrier_record barrier_records[AN_THREAD_LIMIT];

/*
 * this is a boolean.
 */
static uint8_t commit_in_progress = 0;
static unsigned int writer_count = 0;

/*
 * 0 most of the time, 1 for readers that should read from commits,
 * and 2 for writers.
 */
__thread uintptr_t an_sstm_mask = 0;
static __thread uint32_t transaction_depth = 0;
/*
 * 0 normally, -1ULL if a write transaction is in progress (in the current thread).
 */
__thread uintptr_t an_sstm_writing = 0;
static __thread bool trivial_write_transaction = false;

#define WRITER_READ_BIT 2ULL
#define READER_READ_BIT 1ULL

/**
 * an_sstm barrier (sub)subsystem
 *
 * an_sstm needs to know when all the worker threads have observed
 * writes to the commit_in_progress flag.  That's "just" a barrier,
 * and an_sstm_open_read_transaction/an_sstm_close_read_transaction
 * act as passing the barrier.  However, we don't want to starve the
 * writer out if one thread isn't doing any work.  Add a busywork
 * callback on a libevent timer that only serves to observe the flag
 * if necessary, and mark the barrier as passed.
 */

/*
 * Try and check in every 500 ms.
 */
#define AN_SSTM_PERIOD_SEC 0
#define AN_SSTM_PERIOD_USEC 500*1000

static void
timer_observe_callback(int fd, short event, void *data)
{
	struct timeval period = {
		.tv_sec = AN_SSTM_PERIOD_SEC,
		.tv_usec = AN_SSTM_PERIOD_USEC
	};
	struct event *timer = &barrier_records[current->id].timer;

	(void)fd;
	(void)event;
	(void)data;

	if (transaction_depth > 0 && an_sstm_writing == 0) {
		/*
		 * Impbus can't be trusted to open/close transactions.
		 * If we're at the toplevel and a transaction is still
		 * open, just cycle it.
		 */
		an_sstm_cycle_read_transaction();
	}

	ck_pr_store_8(&barrier_records[current->id].flag, 0);
	evtimer_add(timer, &period);
	return;
}

void
an_sstm_register_thread()
{
	struct timeval period = {
		.tv_sec = AN_SSTM_PERIOD_SEC,
		.tv_usec = AN_SSTM_PERIOD_USEC
	};
	struct event *timer;
	unsigned int id = current->id;

	assert(id < AN_THREAD_LIMIT);
	timer = &barrier_records[id].timer;

	ck_pr_store_8(&barrier_records[id].active, 1);
	ck_pr_store_8(&barrier_records[id].flag, 0);

	evtimer_assign(timer, current->event_base, timer_observe_callback, NULL);
	evtimer_add(timer, &period);
	return;
}

void
an_sstm_deregister_thread()
{
	unsigned int id = current->id;

	ck_pr_store_8(&barrier_records[id].active, 0);
	ck_pr_store_8(&barrier_records[id].flag, 0);
	evtimer_del(&barrier_records[id].timer);
	return;
}

static void
barrier(unsigned long long timeout)
{
	uint64_t limit;

	limit = an_md_rdtsc() + an_md_us_to_rdtsc(timeout);
	ck_pr_fence_store_load();

	for (size_t i = 0; i < AN_THREAD_LIMIT; i++) {
		if (ck_pr_load_8(&barrier_records[i].active) != 0) {
			ck_pr_fas_8(&barrier_records[i].flag, 1);
		}
	}

	for (size_t i = 0; i < AN_THREAD_LIMIT; i++) {
		const an_smr_record_t *smr;

		if (i == current->id) {
			goto next;
		}

		smr = an_thread_get_smr_record(i);

		do {
			for (size_t j = 0; j < 128; j++) {
				if (an_smr_is_active(smr) == false ||
				    ck_pr_load_8(&barrier_records[i].active) == 0 ||
				    ck_pr_load_8(&barrier_records[i].flag) == 0) {
					goto next;
				}

				ck_pr_stall();
			}

		} while (timeout == 0 || an_md_rdtsc() < limit);
next:
		ck_pr_store_8(&barrier_records[i].flag, 0);
	}

	return;
}

/**
 * Simple local state setup.
 */
void
an_sstm_open_read_transaction()
{

	if (transaction_depth++ > 0) {
		return;
	}

	assert(an_sstm_mask == 0);
	an_sstm_mask = ((ck_pr_load_8(&commit_in_progress) != 0) ? READER_READ_BIT : 0);
	ck_pr_store_8(&barrier_records[current->id].flag, 0);
	return;
}

void
an_sstm_close_read_transaction()
{

	assert(transaction_depth > 0);

	if (--transaction_depth == 0) {
		assert(an_sstm_mask == 0 || an_sstm_mask == READER_READ_BIT);
		an_sstm_mask = 0;
		ck_pr_store_8(&barrier_records[current->id].flag, 0);
	}

	return;
}

void
an_sstm_cycle_read_transaction()
{
	uint32_t depth = transaction_depth;

	assert(depth <= 1);
	if (depth > 0) {
		an_sstm_close_read_transaction();
	}

	an_sstm_open_read_transaction();
	return;
}

void
an_sstm_open_write_transaction(bool trivial)
{

	assert(transaction_depth == 0);
	transaction_depth = 1;
	assert(SLIST_EMPTY(&commit_list) == true);
	assert(ck_pr_load_8(&commit_in_progress) == 0);
	assert(ck_pr_faa_uint(&writer_count, 1) == 0);

	assert(an_sstm_writing == 0);
	an_sstm_writing = ~an_sstm_writing;
	trivial_write_transaction = trivial;

	an_sstm_mask = WRITER_READ_BIT;
	an_hook_activate_kind(an_sstm, NULL);
	an_thread_push_poison(an_sstm_commit);
	return;
}

/**
 * Deferred cleanup callbacks.
 */
void
an_sstm_call_internal(void (*cb)(void *), void *arg, size_t size)
{
	struct cleanup cleanup = {
		.cb = cb,
		.size = size
	};

	/* Only defer if we're in a (write) transaction. */
	if (an_sstm_writing == 0) {
		cb(arg);
		return;
	}

	if (size == 0) {
		cleanup.ptr = arg;
	} else if (size <= CLEANUP_INLINE_SIZE) {
		memcpy(cleanup.buf, arg, size);
	} else {
		void *buf;

		buf = an_malloc_region(cleanup_buf_token, size);
		memcpy(buf, arg, size);
		cleanup.ptr = buf;
	}

	an_array_push_cleanup(&cleanups, &cleanup);
	return;
}

static void
cleanup(struct an_array_cleanup *stack)
{
	unsigned int n_entries = stack->n_entries;
	struct cleanup *cleanups = stack->values;

	for (size_t i = n_entries; i > 0; ) {
		size_t size;

		i--;
		size = cleanups[i].size;
		if (size == 0) {
			cleanups[i].cb(cleanups[i].ptr);
		} else if (size <= CLEANUP_INLINE_SIZE) {
			cleanups[i].cb(cleanups[i].buf);
		} else {
			cleanups[i].cb(cleanups[i].ptr);
			an_free(cleanup_buf_token, cleanups[i].ptr);
		}
	}

	an_array_deinit_cleanup(stack);
	return;
}

/**
 * The actual important part: tracking and committing shadow objects.
 */
void *
an_sstm_write_slow(void *object, struct an_sstm_linkage *linkage, struct an_sstm_ops *ops)
{
	struct sstm_record *record;
	uintptr_t bits;
	size_t shadow_size, total_size;

	assert(linkage->link == NULL);

	shadow_size = (ops->shadow_size + 15) & -16ULL; /* Round to 16 bytes. */
	total_size = sizeof(struct sstm_record) + shadow_size + ops->release_size;

	if (trivial_write_transaction == true) {
		struct trivial_commit commit = {
			.link = linkage,
			.object = object,
			.freeze = ops->freeze_shadow
		};

		bits = (uintptr_t)object;
		an_array_push_trivial(&trivial_commits, &commit);
		linkage->bits = bits | WRITER_READ_BIT;
		return object;
	}

	record = an_calloc_region(ops->token, 1, total_size);

	bits = (uintptr_t)(record->buf);
	assert((bits & (WRITER_READ_BIT | READER_READ_BIT)) == 0);

	record->ops = ops;
	record->release_offset = shadow_size;
	record->object_backref = object;
	record->link_backref = linkage;

	if (ops->init_shadow != NULL) {
		ops->init_shadow(record->buf, object);
	} else {
		memcpy(record->buf, object, ops->shadow_size);
	}

	if (ops->thaw_shadow != NULL) {
		ops->thaw_shadow(record->buf);
	}

	SLIST_INSERT_HEAD(&commit_list, record, links);
	linkage->bits = (bits | WRITER_READ_BIT);
	return record->buf;
}

void
an_sstm_write_back(void *dst, size_t dst_size,
    const struct an_sstm_linkage *link, struct an_sstm_ops *ops)
{
       uintptr_t bits = link->bits;
       const void *src;

       if (bits & READER_READ_BIT) {
               return;
       }

       src = (void *)(bits & ~(uintptr_t)WRITER_READ_BIT);
       if (src == NULL || dst == src) {
               return;
       }

       assert(dst_size == ops->shadow_size);
       memcpy(dst, src, dst_size);
       if (ops->thaw_shadow != NULL) {
               ops->thaw_shadow(dst);
       }

       return;
}

static void
freeze_and_publish()
{
	struct sstm_record *cursor;

	SLIST_FOREACH(cursor, &commit_list, links) {
		uintptr_t bits = (uintptr_t)cursor->buf;
		const struct an_sstm_ops *ops = cursor->ops;
		const void *original;
		void *release;

		if (ops->freeze_shadow != NULL) {
			ops->freeze_shadow((void *)cursor->buf);
		}

		original = cursor->object_backref;
		release = cursor->buf + cursor->release_offset;
		if (ops->pre_release != NULL) {
			ops->pre_release(release, original);
		} else if (ops->release_size > 0) {
			memcpy(release, original, ops->release_size);
		}

		assert((bits & 3) == 0);
		bits |= 3;
		cursor->link_backref->bits = bits;
	}

	return;
}

static void
commit()
{
	const struct sstm_record *cursor;
	struct trivial_commit *trivial_commit;

	SLIST_FOREACH(cursor, &commit_list, links) {
		const struct an_sstm_ops *ops = cursor->ops;
		void *original = cursor->object_backref;

		if (ops->commit_shadow != NULL) {
			ops->commit_shadow(original, cursor->buf);
		} else {
			memcpy(original, cursor->buf, ops->shadow_size);
		}

		ck_pr_fence_store();
		cursor->link_backref->link = NULL;
	}

	AN_ARRAY_FOREACH(&trivial_commits, trivial_commit) {
		if (trivial_commit->freeze != NULL) {
			trivial_commit->freeze(trivial_commit->object);
		}

		trivial_commit->link->link = NULL;
	}

	an_array_resize_trivial(&trivial_commits, 8);
	an_array_reset_trivial(&trivial_commits);
	return;
}

static void
release_callback(struct sstm_smr_entry *release)
{
	uintptr_t mask = an_sstm_mask;
	uintptr_t writing = an_sstm_writing;

	/* Nothing like hand rolling shallow binding. */
	an_sstm_mask = an_sstm_writing = 0;

	while (SLIST_EMPTY(&release->records) == false) {
		struct sstm_record *cursor = SLIST_FIRST(&release->records);

		SLIST_REMOVE_HEAD(&release->records, links);
		if (cursor->ops->release != NULL) {
			cursor->ops->release(cursor->buf + cursor->release_offset);
		}

		an_free(cursor->ops->token, cursor);
	}

	cleanup(&release->cleanups);

	an_free(smr_entry_token, release);
	an_sstm_writing = writing;
	an_sstm_mask = mask;
	return;
}

void
schedule_release()
{
	struct sstm_smr_entry *smr;

	smr = an_calloc_object(smr_entry_token);
	smr->records = commit_list;
	smr->cleanups = cleanups;

	SLIST_INIT(&commit_list);
	an_array_init_cleanup(&cleanups, 16);

	an_smr_call(smr, release_callback);
	return;
}

bool
an_sstm_commit()
{
	uint8_t r;
	bool ret;

	an_thread_pop_poison(an_sstm_commit);
	assert(transaction_depth == 1);
	transaction_depth = 0;

	assert(~an_sstm_writing == 0);
	if (SLIST_EMPTY(&commit_list) == true &&
	    an_array_length_trivial(&trivial_commits) == 0) {
		ret = false;
		goto cleanup;
	}

	assert(ck_pr_load_8(&commit_in_progress) == 0);

	/*
	 * Two phases.
	 *
	 * First, convert our writes to the publication format, and
	 * make them visible to commit-mode readers.
	 */
	freeze_and_publish();

	/*
	 * Now, we're entering a new commit phase.  Readers should go
	 * for our update records.
	 *
	 * This is the actual critical section.
	 */
	r = ck_pr_fas_8(&commit_in_progress, 1);
	assert(r == 0);
	an_thread_tick();

	/*
	 * Workers try and check in every .5 second, and the only
	 * reason they'd fail to do so is if they're handling work.
	 * This 1 second timeout should never be reached.
	 */
	barrier(1000 * 1000ULL);

	commit();

	/*
	 * We're done committing; no shadow pointer remains.
	 *
	 * End of critical section.
	 */
	ck_pr_fas_8(&commit_in_progress, 0);

	/*
	 * Make sure that all threads have observed the state change.
	 */
	an_thread_tick();
	barrier(1000 * 1000ULL);
	ret = true;

cleanup:
	an_hook_deactivate_kind(an_sstm, NULL);
	an_sstm_writing = ~an_sstm_writing;
	trivial_write_transaction = false;
	an_sstm_mask = 0;
	assert(ck_pr_faa_uint(&writer_count, -1U) == 1);

	schedule_release();
	return ret;
}

void
an_sstm_init_lib()
{

	AN_HOOK_DUMMY(an_sstm);
	an_array_init_cleanup(&cleanups, 16);
	an_array_init_trivial(&trivial_commits, 128);
	return;
}
