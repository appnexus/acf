#include <assert.h>
#include <ck_spinlock.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "common/an_cc.h"
#include "common/an_malloc.h"
#include "common/rtbr/rtbr_impl.h"

static AN_MALLOC_DEFINE(slice_token,
    .string = "an_rtbr_slice",
    .mode   = AN_MEMORY_MODE_VARIABLE);

static struct an_rtbr_record *
slice_get_record(struct an_rtbr_slice *slice, const struct an_rtbr_tid_info *info)
{
	uint64_t allocated;
	uint64_t n_records;
	uint64_t record_id;
	struct an_rtbr_record *record;

	allocated = ck_pr_load_64(&slice->allocated_records);
	n_records = slice->n_records;
	if (allocated >= n_records) {
		return NULL;
	}

	allocated = ck_pr_faa_64(&slice->allocated_records, 1);
	if (allocated >= n_records) {
		return NULL;
	}

	record = &slice->records[allocated];
	record_id = slice->id_offset + allocated;
	if (ck_pr_load_64(&record->id) != record_id) {
		/* Racy writes are OK, they're all identical. */
		ck_pr_store_64(&record->id, record_id);
	}

	if (an_rtbr_record_acquire(record, info)) {
		return record;
	}

	return NULL;
}

static struct an_rtbr_slice *
an_rtbr_slice_create(size_t size, uint64_t offset)
{
	struct an_rtbr_slice *ret;
	size_t byte_count = sizeof(*ret) + size * sizeof(ret->records[0]);

	assert(size > 0 && (size & (size - 1)) == 0);
	assert(((byte_count - sizeof(*ret)) / sizeof(ret->records[0])) == size);

	ret = an_calloc_region(slice_token, 1, byte_count);
	assert(ret != NULL);

	ret->n_records = size;
	ret->id_offset = offset;
	return ret;
}

static void
an_rtbr_slice_destroy(struct an_rtbr_slice *slice)
{

	an_free(slice_token, slice);
	return;
}

static struct an_rtbr_slice *
ensure_slice(struct an_rtbr_global *global, size_t i)
{
	struct an_rtbr_slice *ret;
	struct an_rtbr_slice *copy;
	size_t size;
	uint64_t offset = 0;

	ret = an_pr_load_ptr(&global->slices[i]);
	ck_pr_fence_load();
	if (AN_CC_LIKELY(ret != NULL)) {
		return ret;
	}

	if (i > 0) {
		struct an_rtbr_slice *prev;

		prev = an_pr_load_ptr(&global->slices[i - 1]);
		assert(prev != NULL);
		ck_pr_fence_load();
		offset = prev->id_offset + prev->n_records;
	}

	{
		unsigned __int128 temp_size = SLICE_INITIAL_SIZE;
		size_t max_size = (UINT_MAX / 2) + 1;

		temp_size <<= i;
		if (i > 32 || temp_size > max_size) {
			size = max_size;
		} else {
			size = (size_t)temp_size;
		}
	}

	assert(offset + size > offset);
	copy = an_rtbr_slice_create(size, offset);
	ck_pr_fence_store();

	/* coverity[overrun-local : FALSE] */
	if (ck_pr_cas_ptr_value(&global->slices[i], NULL, copy, &ret)) {
		return copy;
	}

	an_rtbr_slice_destroy(copy);
	ck_pr_fence_load();
	return ret;
}

static void
an_rtbr_init(struct an_rtbr_global *global)
{

	ck_spinlock_lock(&global->lock);
	if (ck_pr_load_8(&global->initialized) != 0) {
		goto out;
	}

	(void)pthread_atfork(NULL, NULL, an_rtbr_reinit);
	ck_stack_init(&global->freelist);
	ck_pr_store_8(&global->initialized, 1);

out:
	ck_spinlock_unlock(&global->lock);
	return;
}

struct an_rtbr_record *
an_rtbr_get_record(struct an_rtbr_global *global)
{
	struct an_rtbr_tid_info info;
	struct an_rtbr_record *ret;
	size_t cur_slice;

	if (AN_CC_UNLIKELY(ck_pr_load_8(&global->initialized) == 0)) {
		an_rtbr_init(global);
	}

	info = an_rtbr_tid_info(0);
	while (1) {
		struct ck_stack_entry *entry;

		entry = ck_stack_pop_mpmc(&global->freelist);
		if (entry == NULL) {
			break;
		}

		ret = an_rtbr_record_container(entry);
		if (an_rtbr_record_acquire(ret, &info)) {
			return ret;
		}
	}

	cur_slice = ck_pr_load_32(&global->cur_slice);
	for (size_t i = cur_slice; i < ARRAY_SIZE(global->slices); i++) {
		struct an_rtbr_slice *slice;

		slice = ensure_slice(global, i);
		ret = slice_get_record(slice, &info);
		if (ret != NULL) {
			if (i != cur_slice) {
				ck_pr_store_32(&global->cur_slice, i);
			}

			return ret;
		}
	}

	assert(0 && "Much more than 2^32 concurrent threads?!");
	return NULL;
}

void
an_rtbr_reinit_impl(struct an_rtbr_global *global)
{

	for (size_t i = 0; i < ARRAY_SIZE(global->slices); i++) {
		an_rtbr_slice_destroy(global->slices[i]);
	}

	memset(global, 0, sizeof(*global));
	ck_spinlock_init(&global->lock);
	ck_stack_init(&global->freelist);
	global->initialized = true;
	ck_pr_fence_store();
	return;
}
