#include <assert.h>
#include <ck_pr.h>
#include <limits.h>
#include <string.h>

#include "common/an_cc.h"
#include "common/memory/bump.h"
#include "common/memory/freelist.h"
#include "common/memory/pool.h"
#include "common/util.h"

static inline bool
refresh(void *snapshot, const void *source)
{
	void *actual[2];
	bool delta;

	ck_pr_load_ptr_2(source, actual);
	delta = (((void **)snapshot)[0] != actual[0] ||
	    ((void **)snapshot)[1] != actual[1]);
	memcpy(snapshot, &actual, sizeof(actual));
	return delta;
}

static struct an_bump_shared *
an_pool_shared_alloc_bump(struct an_pool_shared *shared,
    struct an_freelist_entry **OUT_entry)
{
	struct an_bump_policy policy = {
		.premap = true
	};
	struct an_bump_shared *ret;

	if (OUT_entry != NULL) {

		*OUT_entry = an_freelist_register(shared->freelist);

		if (*OUT_entry == NULL) {
			return NULL;
		}
	}

	ret = an_bump_shared_create(shared->bump_size, &policy);
	return ret;
}

static bool
an_pool_shared_swap(struct an_pool_shared *shared,
    struct an_bump_shared **snapshot)
{
	struct an_bump_shared *actual[2];
	struct an_freelist_entry *entry;
	struct an_bump_shared *next;
	struct an_bump_shared *old;
	bool r;

	if (refresh(snapshot, &shared->bumps)) {
		return true;
	}

	memcpy(&actual, snapshot, sizeof(actual));

	next = an_freelist_pop(shared->freelist, &entry);
	if (next != NULL) {
		an_bump_shared_reset(next);
	} else {
		entry = NULL;

		/* Only get an entry if we have something to recycle. */
		next = an_pool_shared_alloc_bump(shared,
		    (actual[1] != NULL) ? &entry : NULL);
	}

	if (next == NULL) {
		return refresh(snapshot, &shared->bumps);
	}

	old = actual[1];
	actual[1] = actual[0];
	actual[0] = next;
	if (ck_pr_cas_ptr_2_value(&shared->bumps, snapshot, actual, snapshot) == false) {
		if (entry == NULL) {
			entry = an_freelist_register(shared->freelist);
		}
		if (entry != NULL) {
			/*
			 * the entry will be leaked but it's better than crashing.
			 * if we size buffers correctly this won't get hit
			 */
			an_freelist_push(shared->freelist, entry, next);
		}
		return true;
	}

	memcpy(snapshot, &actual, sizeof(actual));
	if (old != NULL) {
		r = an_bump_shared_quiesce(old);
		assert(r && "Race condition despite CMPXCHG16B?");
		an_freelist_shelve(shared->freelist, entry, old);
	}
	return true;
}

void *
an_pool_shared_alloc_slow(struct an_pool_shared *pool,
    size_t size, bool zero, size_t alignment)
{
	struct an_bump_shared *snapshot[2] = { 0 };
	void *ret = NULL;

	if (size > pool->bump_size / 2 ||
	    size + alignment >= pool->bump_size / 2) {
		return NULL;
	}

	do {
		for (size_t i = 0; i < ARRAY_SIZE(pool->bumps); i++) {
			ret = an_bump_alloc(pool->bumps[i], size, alignment);
			if (ret != NULL) {
				goto out;
			}
		}
	} while (an_pool_shared_swap(pool, snapshot));

out:
	if (zero && ret != NULL) {
		memset(ret, 0, size);
	}

	return ret;
}

static void
an_pool_private_swap(struct an_pool_private *private)
{
	struct an_bump_policy policy = {
		.premap = true
	};
	struct an_bump_private *bump;
	struct an_freelist_entry *entry = NULL;

	if (private->bump != NULL) {
		assert(private->entry != NULL);
		an_freelist_shelve(private->freelist,
		    private->entry, private->bump);
	}

	private->bump = NULL;
	private->entry = NULL;
	bump = an_freelist_pop(private->freelist, &entry);
	if (bump == NULL) {
		entry = an_freelist_register(private->freelist);
		if (entry == NULL) {
			return;
		}

		bump = an_bump_private_create(private->bump_size, &policy);
		if (bump == NULL) {
			return;
		}
	}

	an_bump_private_reset(bump);
	private->bump = bump;
	private->entry = entry;
	return;
}

void *
an_pool_private_alloc_slow(struct an_pool_private *pool,
    size_t size, bool zero, size_t alignment)
{
	void *ret;

	if (size > pool->bump_size / 2 ||
	    size + alignment >= pool->bump_size / 2) {
		return NULL;
	}

	ret = an_bump_alloc(pool->bump, size, alignment);
	if (AN_CC_LIKELY(ret == NULL)) {
		an_pool_private_swap(pool);
		ret = an_bump_alloc(pool->bump, size, alignment);
	}

	if (zero && ret != NULL) {
		memset(ret, 0, size);
	}

	return ret;
}
