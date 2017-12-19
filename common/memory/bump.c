#include <assert.h>
#include <ck_pr.h>
#include <ck_spinlock.h>
#include <string.h>

#include "common/an_cc.h"
#include "common/memory/bump.h"
#include "common/memory/map.h"
#include "common/memory/reserve.h"

struct an_bump_impl {
	struct an_bump_fast fast;
	uint64_t mapped;
	uint64_t reserved;
};

struct an_bump_private {
	struct an_bump_impl impl;
};

struct an_bump_shared {
	struct an_bump_impl impl;
	ck_spinlock_t grow_lock;
};

_Static_assert(sizeof(struct an_bump_private) <= MEMORY_BUMP_PAGE_SIZE,
    "Size of bump allocation header must be at most one page");

_Static_assert(sizeof(struct an_bump_shared) <= MEMORY_BUMP_PAGE_SIZE,
    "Size of bump allocation header must be at most one page");

struct an_bump_private *
an_bump_private_create(size_t capacity, const struct an_bump_policy *policy)
{
	struct an_bump_private *ret;
	size_t mapped;

	if (capacity < MEMORY_BUMP_PAGE_SIZE * 2) {
		capacity = MEMORY_BUMP_PAGE_SIZE * 2;
	}

	if ((capacity % MEMORY_BUMP_PAGE_SIZE) != 0) {
		capacity = (1 + (capacity / MEMORY_BUMP_PAGE_SIZE)) * MEMORY_BUMP_PAGE_SIZE;
		assert(capacity != 0);
	}

	ret = an_memory_reserve(capacity, MEMORY_BUMP_PAGE_SIZE);
	if (policy != NULL && policy->premap) {
		mapped = an_memory_map(ret, capacity, capacity);
	} else {
		mapped = an_memory_map(ret, MEMORY_BUMP_PAGE_SIZE, capacity);
	}

	assert(mapped >= MEMORY_BUMP_PAGE_SIZE);
	ret->impl.fast.allocated = (uintptr_t)ret + sizeof(*ret);
	ret->impl.fast.capacity = mapped / MEMORY_BUMP_PAGE_SIZE;
	ret->impl.mapped = mapped;
	ret->impl.reserved = capacity;
	return ret;
}

struct an_bump_shared *
an_bump_shared_create(size_t capacity, const struct an_bump_policy *policy)
{
	struct an_bump_shared *ret;
	size_t mapped;

	if (capacity < MEMORY_BUMP_PAGE_SIZE * 2) {
		capacity = MEMORY_BUMP_PAGE_SIZE * 2;
	}

	if ((capacity % MEMORY_BUMP_PAGE_SIZE) != 0) {
		capacity = (1 + (capacity / MEMORY_BUMP_PAGE_SIZE)) * MEMORY_BUMP_PAGE_SIZE;
		assert(capacity != 0);
	}

	ret = an_memory_reserve(capacity, MEMORY_BUMP_PAGE_SIZE);
	assert(((uintptr_t)ret % 16) == 0 &&
	    "Need 16 byte alignment for cmpxchg16b.");

	if (policy != NULL && policy->premap) {
		mapped = an_memory_map(ret, capacity, capacity);
	} else {
		mapped = an_memory_map(ret, MEMORY_BUMP_PAGE_SIZE, capacity);
	}

	assert(mapped >= MEMORY_BUMP_PAGE_SIZE);
	ret->impl.fast.allocated = (uintptr_t)ret + sizeof(*ret);
	ret->impl.fast.capacity = mapped / MEMORY_BUMP_PAGE_SIZE;
	ret->impl.mapped = mapped;
	ret->impl.reserved = capacity;
	ck_spinlock_init(&ret->grow_lock);

	ck_pr_fence_store();
	return ret;
}

void
an_bump_private_reset(struct an_bump_private *bump)
{

	bump->impl.fast.allocated = (uintptr_t)bump + sizeof(*bump);
	return;
}

bool
an_bump_shared_quiesce(struct an_bump_shared *bump)
{
	struct an_bump_fast copy;
	struct an_bump_fast update;
	struct an_bump_fast *dst = &bump->impl.fast;
	size_t reserved = ck_pr_load_64(&bump->impl.reserved);

	copy = an_bump_fast_read(dst);
	while (1) {
		update = copy;
		update.allocated = (uintptr_t)bump + reserved;

		if (copy.allocated == update.allocated) {
			return true;
		}

		if (an_bump_fast_cas(dst, &copy, &update, &copy)) {
			return true;
		}

		if (update.generation != copy.generation) {
			return false;
		}
	}

	return false;
}

bool
an_bump_shared_reset(struct an_bump_shared *bump)
{
	struct an_bump_fast *dst = &bump->impl.fast;
	struct an_bump_fast copy;
	struct an_bump_fast update;
	uint32_t old_generation;

	copy = an_bump_fast_read(dst);
	old_generation = copy.generation;
	while (1) {
		update = copy;
		update.allocated = (uintptr_t)bump + sizeof(*bump);
		update.generation++;

		if (an_bump_fast_cas(dst, &copy, &update, &copy)) {
			return true;
		}

		if (copy.generation != old_generation) {
			return false;
		}
	}

	return false;
}

static bool
grow(struct an_bump_impl *impl, size_t goal)
{
	struct an_bump_fast copy;
	struct an_bump_fast update;
	size_t growth;

	if (goal <= impl->mapped) {
		return true;
	}

	if (impl->mapped == impl->reserved ||
	    goal > impl->reserved) {
		return false;
	}

	if ((goal % MEMORY_BUMP_PAGE_SIZE) != 0) {
		goal = (1 + (goal / MEMORY_BUMP_PAGE_SIZE)) * MEMORY_BUMP_PAGE_SIZE;
	}

	/* If shared, we're locked out. */
	growth = an_memory_map((void *)((uintptr_t)impl + impl->mapped),
	    goal - impl->mapped, impl->reserved - impl->mapped);
	if (growth < (goal - impl->mapped)) {
		return false;
	}

	ck_pr_store_64(&impl->mapped, impl->mapped + growth);
	copy = an_bump_fast_read(&impl->fast);

	while (1) {
		update = copy;
		update.capacity = impl->mapped / MEMORY_BUMP_PAGE_SIZE;

		if (an_bump_fast_cas(&impl->fast, &copy, &update, &copy)) {
			return true;
		}
	}

	return true;
}

static void *
private_alloc(struct an_bump_private *pool, size_t size, size_t align)
{
	uint64_t capacity;
	uint64_t next;
	uint64_t ret;
	size_t mask;

	if (align == 0) {
		mask = 0;
	} else {
		mask = (align ^ (align - 1)) >> 1;
	}

	capacity = pool->impl.mapped;
	if (size > pool->impl.reserved) {
		return NULL;
	}

	ret = (pool->impl.fast.allocated + mask) & ~mask;
	next = ret + size;
	if (capacity < pool->impl.reserved &&
	    (next - (uintptr_t)pool) > capacity) {
		if (grow(&pool->impl, next - (uintptr_t)pool) == false) {
			return NULL;
		}
	}

	capacity = pool->impl.mapped;
	if ((next - (uintptr_t)pool) > capacity) {
		return NULL;
	}

	pool->impl.fast.allocated = next;
	return (void *)ret;
}

void *
an_bump_private_alloc_slow(struct an_bump_private **pool_p, size_t size, size_t align)
{
	struct an_bump_private *pool = an_pr_load_ptr(pool_p);

	while (pool != NULL) {
		struct an_bump_private *new_pool;
		void *ret;

		ret = private_alloc(pool, size, align);
		if (ret != NULL) {
			return ret;
		}

		new_pool = an_pr_load_ptr(pool_p);
		if (new_pool == pool) {
			return NULL;
		}

		pool = new_pool;
	}

	return NULL;
}

static void *
shared_alloc(struct an_bump_shared *pool, size_t size, size_t align)
{
	struct an_bump_fast copy;
	struct an_bump_fast update;
	uint64_t capacity;
	uint64_t next;
	uint64_t ret;
	uint64_t reserved;
	uint32_t generation;
	size_t mask;

	if (align == 0) {
		mask = 0;
	} else {
		mask = (align ^ (align - 1)) >> 1;
	}

	copy = an_bump_fast_read(&pool->impl.fast);
	reserved = ck_pr_load_64(&pool->impl.reserved);
	generation = copy.generation;

	if (size > reserved) {
		return NULL;
	}

	while (copy.generation == generation) {
		uint64_t wilderness;

		capacity = ck_pr_load_64(&pool->impl.mapped);
		ret = (copy.allocated + mask) & ~mask;
		next = ret + size;

		wilderness = next - (uintptr_t)pool;
		if (wilderness > reserved) {
			return NULL;
		}

		if (wilderness > capacity) {
			bool success;

			ck_spinlock_lock(&pool->grow_lock);
			success = grow(&pool->impl, next - (uintptr_t)pool);
			ck_spinlock_unlock(&pool->grow_lock);

			if (success == false) {
				return NULL;
			}
		} else {
			update = copy;
			update.allocated = next;
			if (an_bump_fast_cas(&pool->impl.fast,
			    &copy, &update, &copy)) {
				return (void *)ret;
			}
		}
	}

	return NULL;
}

void *
an_bump_shared_alloc_slow(struct an_bump_shared **pool_p, size_t size, size_t align)
{
	struct an_bump_shared *pool = an_pr_load_ptr(pool_p);

	while (pool != NULL) {
		struct an_bump_shared *new_pool;
		void *ret;

		ret = shared_alloc(pool, size, align);
		if (ret != NULL) {
			return ret;
		}

		new_pool = an_pr_load_ptr(pool_p);
		if (new_pool == pool) {
			return NULL;
		}

		pool = new_pool;
	}

	return NULL;
}
