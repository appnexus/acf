#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H
#include <ck_cc.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "common/an_cc.h"
#include "common/memory/bump.h"
#include "common/memory/freelist.h"

struct an_pool_shared {
	struct an_bump_shared *bumps[2];
	struct an_freelist *const freelist;
	const uint64_t bump_size;
} CK_CC_ALIGN(16);

struct an_pool_private {
	struct an_bump_private *bump;
	struct an_freelist_entry *entry;
	struct an_freelist *const freelist;
	const uint64_t bump_size;
} CK_CC_ALIGN(16);

#define AN_POOL_SHARED(LINKAGE, NAME, BUMP_SIZE, ALLOCATION_LIMIT) \
	AN_FREELIST(static, NAME##_freelist, 2 + (ALLOCATION_LIMIT / BUMP_SIZE)); \
	LINKAGE struct an_pool_shared NAME = {			\
		.freelist = &NAME##_freelist,			\
		.bump_size = (BUMP_SIZE)			\
	};

#define AN_POOL_PRIVATE(LINKAGE, NAME, BUMP_SIZE, ALLOCATION_LIMIT)	\
	AN_FREELIST(static, NAME##_freelist, 2 + (ALLOCATION_LIMIT / BUMP_SIZE)); \
	LINKAGE __thread struct an_pool_private NAME = {		\
		.freelist = &NAME##_freelist,				\
		.bump_size = (BUMP_SIZE)				\
	};

#define an_pool_alloc(POOL, SIZE, ZERO, ALIGN)				\
	(__builtin_choose_expr(						\
	    __builtin_types_compatible_p(__typeof__(POOL), struct an_pool_private *), \
		an_pool_private_alloc, an_pool_shared_alloc)((POOL), (SIZE), (ZERO), (ALIGN)))

void *an_pool_shared_alloc_slow(struct an_pool_shared *pool, size_t size, bool zero, size_t align);

static AN_CC_UNUSED void *
an_pool_shared_alloc(struct an_pool_shared *pool, size_t size, bool zero, size_t align)
{
	void *ret;

	if (an_pr_load_ptr(&pool->bumps[1]) != NULL) {
		ret = an_bump_alloc(pool->bumps[1], size, align);
		if (ret != NULL) {
			goto out;
		}
	}

	ret = an_bump_alloc(pool->bumps[0], size, align);
	if (AN_CC_LIKELY(ret != NULL)) {
		goto out;
	}

	return an_pool_shared_alloc_slow(pool, size, zero, align);

out:
	if (zero) {
		memset(ret, 0, size);
	}

	return ret;
}

void *an_pool_private_alloc_slow(struct an_pool_private *pool, size_t size, bool zero, size_t align);

static inline void *
an_pool_private_alloc(struct an_pool_private *pool, size_t size, bool zero, size_t align)
{
	void *ret;

	ret = an_bump_alloc(pool->bump, size, align);
	if (AN_CC_LIKELY(ret != NULL)) {
		if (zero) {
			memset(ret, 0, size);
		}

		return ret;
	}

	return an_pool_private_alloc_slow(pool, size, zero, align);
}
#endif /* !MEMORY_POOL_H */
