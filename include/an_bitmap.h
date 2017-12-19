#ifndef AN_BITMAP_H
#define AN_BITMAP_H

#include "an_cc.h"

AN_EXTERN_C_BEGIN

#include <stdint.h>
#include <stdbool.h>

#define AN_STATIC_BITMAP_T(name, size) uint64_t name[(size + 63UL) / 64]

#define AN_STATIC_BITMAP_CLEAR(name, size) memset(name, 0, ((size + 63UL) / 64) * sizeof(uint64_t))

AN_CC_UNUSED static void
an_static_bitmap_set(uint64_t *bitmap, unsigned i)
{
        uint64_t mask = 1UL << (i % 64);
        bitmap[i / 64] |= mask;
}

AN_CC_UNUSED static void
an_static_bitmap_unset(uint64_t *bitmap, unsigned i)
{
	uint64_t mask = 1UL << (i % 64);
	bitmap[i / 64] &= ~mask;
}

AN_CC_UNUSED static bool
an_static_bitmap_is_set(const uint64_t *bitmap, unsigned i)
{
	return ((bitmap[i / 64]) & (1UL << (i % 64))) != 0;
}

AN_CC_UNUSED static bool
an_static_bitmap_is_empty(const uint64_t *bitmap, size_t len)
{
	size_t blocks = (len + 63UL) / 64;

	for (size_t i = 0; i < blocks; i++) {
		if (bitmap[i] != 0) {
			return false;
		}
	}

	return true;
}

AN_EXTERN_C_END

#endif
