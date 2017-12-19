#ifndef MEMORY_BUMP_H
#define MEMORY_BUMP_H
#include <ck_pr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "common/an_cc.h"

#define MEMORY_BUMP_PAGE_SIZE 4096ULL

struct an_bump_policy {
	bool premap; /* If true, map in the whole region from the start. */
};

/* Private bump pointers are thread local. */
struct an_bump_private;
/* Shared bump pointers are lock-free, for any number of allocator. */
struct an_bump_shared;

struct an_bump_private *
an_bump_private_create(size_t capacity, const struct an_bump_policy *);

struct an_bump_shared *
an_bump_shared_create(size_t capacity, const struct an_bump_policy *);

/**
 * @brief reset the allocation pointer on a private bump pointer.
 */
void
an_bump_private_reset(struct an_bump_private *);

/**
 * @brief Make sure no more allocation happens on the shared bump pointer.
 * @return true on success, false if someone else `reset` the bump pointer
 * while we were trying to quiesce.
 */
bool
an_bump_shared_quiesce(struct an_bump_shared *);

/**
 * @brief reset the allocation pointer on a shared bump pointer.
 * @return true on success, false if someone else reset the bump pointer
 * in parallel.
 *
 * We use a generation counter to reset without ABA issue in quiesce.
 */
bool
an_bump_shared_reset(struct an_bump_shared *);

static void *
an_bump_private_alloc(struct an_bump_private **, size_t size, size_t align);

static void *
an_bump_shared_alloc(struct an_bump_shared **, size_t size, size_t align);

#define an_bump_alloc(BUMP, SIZE, ALIGN)				\
	(__builtin_choose_expr(						\
	    __builtin_types_compatible_p(__typeof__(BUMP), struct an_bump_private *), \
	    an_bump_private_alloc, an_bump_shared_alloc)(&(BUMP), (SIZE), (ALIGN)))

void *
an_bump_private_alloc_slow(struct an_bump_private **, size_t size, size_t align);

void *
an_bump_shared_alloc_slow(struct an_bump_shared **, size_t size, size_t align);

struct an_bump_fast {
	union {
		struct {
			uint64_t allocated; /* a pointer. */
			uint32_t capacity; /* in MEMORY_BUMP_PAGE_SIZE increments. */
			uint32_t generation; /* Incremented when allocated is reset. */
		};
		uint64_t bits[2];
	};
};

static inline struct an_bump_fast
an_bump_fast_read(const struct an_bump_fast *fast)
{
	struct an_bump_fast ret;
	struct an_bump_fast copy;

	copy.bits[0] = ck_pr_load_64(&fast->bits[0]);
	copy.bits[1] = ck_pr_load_64(&fast->bits[1]);
	/* Stupid aliasing tricks. */
	memcpy(&ret, &copy, sizeof(ret));
	return ret;
}

static inline bool
an_bump_fast_cas(struct an_bump_fast *dst,
    const struct an_bump_fast *old, const struct an_bump_fast *update,
    struct an_bump_fast *actual)
{
	struct an_bump_fast old_copy;
	struct an_bump_fast update_copy;
	struct an_bump_fast actual_copy;
	bool ret;

	memcpy(&old_copy, old, sizeof(old_copy));
	memcpy(&update_copy, update, sizeof(update_copy));
	if (actual == NULL) {
		return ck_pr_cas_64_2(dst->bits, old_copy.bits, update_copy.bits);
	}

	ret = ck_pr_cas_64_2_value(dst->bits, old_copy.bits, update_copy.bits,
	    actual_copy.bits);
	memcpy(actual, &actual_copy, sizeof(*actual));
	return ret;
}

static inline void *
an_bump_private_alloc(struct an_bump_private **pool_p, size_t size, size_t align)
{
	struct an_bump_fast *fast = *(void **)pool_p;
	uint64_t capacity;
	uint64_t next;
	uint64_t ret;
	size_t mask;

	if (fast == NULL) {
		return NULL;
	}

	size = (size == 0) ? 1 : size;
	if (align == 0) {
		mask = 0;
	} else {
		mask = (align ^ (align - 1)) >> 1;
	}

	capacity = fast->capacity * MEMORY_BUMP_PAGE_SIZE;
	ret = (fast->allocated + mask) & ~mask;
	next = ret + size;
	if (AN_CC_UNLIKELY((next - (uintptr_t)fast) > capacity)) {
		return an_bump_private_alloc_slow(pool_p, size, align);
	}

	fast->allocated = next;
	return (void *)ret;
}

static AN_CC_UNUSED void *
an_bump_shared_alloc(struct an_bump_shared **pool_p, size_t size, size_t align)
{
	struct an_bump_fast copy;
	struct an_bump_fast update;
	struct an_bump_fast *fast;
	uint64_t capacity;
	uint64_t next;
	uint64_t ret;
	size_t mask;

	fast = an_pr_load_ptr((void **)pool_p);
	if (fast == NULL) {
		return NULL;
	}

	copy = an_bump_fast_read(fast);
	capacity = copy.capacity * MEMORY_BUMP_PAGE_SIZE;
	size = (size == 0) ? 1 : size;
	if (align == 0) {
		mask = 0;
	} else {
		mask = (align ^ (align - 1)) >> 1;
	}

	ret = (copy.allocated + mask) & ~mask;
	next = ret + size;
	if (AN_CC_UNLIKELY((next - (uintptr_t)fast) > capacity)) {
		goto slow;
	}

	update = copy;
	update.allocated = next;
	if (an_bump_fast_cas(fast, &copy, &update, NULL)) {
		return (void *)ret;
	}

slow:
	return an_bump_shared_alloc_slow(pool_p, size, align);
}

#endif /* !MEMORY_BUMP_H */
