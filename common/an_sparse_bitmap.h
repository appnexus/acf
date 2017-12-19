/* Sparse bitmap as a sorted vector of disjoint intervals */

#ifndef AN_SPARSE_BITMAP
#define AN_SPARSE_BITMAP
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <event.h>

#include <an_message/an_bstrlib.h>

#include "common/an_array.h"

/**
 * @brief Half-open intervals `[lower, upper)`.
 * @var lower The inclusive lower bound of the interval.
 * @var upper The exclusive upper bound of the interval.
 */
struct interval {
	/* half-open [lower, upper) interval */
	uint32_t lower;
	uint32_t upper;
};

AN_ARRAY(interval, interval)

/**
 * @brief An `an_sparse_bitmap` is a sorted `an_array` of `struct interval`
 */
struct an_sparse_bitmap {
	struct an_array_interval intervals;
};

/**
 * @brief Initialize an `an_sparse_bitmap` object.
 * @param bitmap The struct to initialize.
 * @param initial_length the amount of space to reserve (in intervals).
 */
void an_sparse_bitmap_init(struct an_sparse_bitmap *bitmap, size_t initial_length);

/**
 * @brief Heap allocate a fresh `an_sparse_bitmap` object.
 */
struct an_sparse_bitmap *an_sparse_bitmap_create(size_t initial_length);

/**
 * @brief Deinitialise an `an_sparse_bitmap` object.
 */
void an_sparse_bitmap_deinit(struct an_sparse_bitmap *bitmap);

/**
 * @brief Deintialise and deallocate a heap-allocated `an_sparse_bitmap` object.
 */
void an_sparse_bitmap_destroy(struct an_sparse_bitmap *bitmap);

/**
 * @brief Insert half-open range [lower, upper) in sparse bitmap.
 */
void an_sparse_bitmap_insert_range(struct an_sparse_bitmap *,
    uint32_t lower, uint32_t upper);
/**
 * @brief Remove half-open range [lower, upper) from sparse bitmap.
 */
void an_sparse_bitmap_remove_range(struct an_sparse_bitmap *,
    uint32_t lower, uint32_t upper);

/**
 * @brief Test whether `bitmap` includes an interval that contains `needle`.
 */
bool an_sparse_bitmap_contains(const struct an_sparse_bitmap * bitmap,
    uint32_t needle);

/**
 * @brief Test whether `bitmap` includes an interval that intersects with `needles`.
 * @param bitmap The (sorted) set of intervals.
 * @param needles A sorted vector of uint32_t to look for.
 * @param n The number of uint32_t in `needles`.
 * @return The index of an (the least) intersecting needle, `-1UL` if none.
 */
size_t
an_sparse_bitmap_intersects(const struct an_sparse_bitmap *bitmap,
    const uint32_t *needles, size_t n);

/* Point are just degenerate intervals. */
static inline int
an_sparse_bitmap_insert(struct an_sparse_bitmap *bitmap, uint32_t x)
{

	if (x + 1 < x) {
		return -1;
	}

	an_sparse_bitmap_insert_range(bitmap, x, x + 1);
	return 0;
}

static inline int
an_sparse_bitmap_remove(struct an_sparse_bitmap *bitmap, uint32_t x)
{

	if (x + 1 < x) {
		return -1;
	}

	an_sparse_bitmap_remove_range(bitmap, x, x + 1);
	return 0;
}

static inline bool
an_sparse_bitmap_is_empty(const struct an_sparse_bitmap *bitmap)
{

	if (bitmap == NULL) {
		return true;
	}

	return an_array_length_interval(&bitmap->intervals) == 0;
}

static inline size_t
an_sparse_bitmap_item_count(const struct an_sparse_bitmap *bitmap)
{

	if (bitmap == NULL) {
		return 0;
	}

	return an_array_length_interval(&bitmap->intervals);
}

static inline const struct interval *
an_sparse_bitmap_index(struct an_sparse_bitmap *bitmap, size_t i)
{

	return an_array_value_interval(&bitmap->intervals, i);
}

void an_sparse_bitmap_to_string(const struct an_sparse_bitmap *, bstring);

void an_sparse_bitmap_append_json(struct evbuffer *, const char *name,
    const struct an_sparse_bitmap *, int comma);

struct an_sparse_bitmap_enumerator {
	const struct an_sparse_bitmap *bitmap;
	unsigned int length;
	unsigned int index;
	uint32_t upper;
	uint32_t next_value;
};

static inline void
an_sparse_bitmap_enumerator_reset_interval(struct an_sparse_bitmap_enumerator *enumerator)
{
	const struct interval *interval;

	if (enumerator->index < enumerator->length) {
		interval = an_sparse_bitmap_index((struct an_sparse_bitmap *)enumerator->bitmap, enumerator->index);
		enumerator->upper = interval->upper;
		enumerator->next_value = interval->lower;
	}
}

static inline void
an_sparse_bitmap_enumerator_skip_interval(struct an_sparse_bitmap_enumerator *enumerator)
{

	++enumerator->index;
	an_sparse_bitmap_enumerator_reset_interval(enumerator);
}

static inline struct an_sparse_bitmap_enumerator
an_sparse_bitmap_make_enumerator(const struct an_sparse_bitmap *bitmap)
{
	struct an_sparse_bitmap_enumerator enumerator = {
		.bitmap = bitmap,
		.length = an_sparse_bitmap_item_count(bitmap),
		.index = 0,
	};

	an_sparse_bitmap_enumerator_reset_interval(&enumerator);

	return enumerator;
}

static inline bool
an_sparse_bitmap_enumerator_move_next(struct an_sparse_bitmap_enumerator *enumerator, uint32_t *cursor)
{

	while (enumerator->index < enumerator->length) {
		if (enumerator->next_value < enumerator->upper) {
			*cursor = enumerator->next_value++;
			return true;
		}

		an_sparse_bitmap_enumerator_skip_interval(enumerator);
	}

	return false;
}

#define AN_SPARSE_BITMAP_FOREACH(bitmap, cursor)			\
	for (struct an_sparse_bitmap_enumerator _enumerator = an_sparse_bitmap_make_enumerator(bitmap); \
	     an_sparse_bitmap_enumerator_move_next(&_enumerator, &(cursor)) == true;)

#define AN_SPARSE_BITMAP_SKIP_INTERVAL					\
	({ an_sparse_bitmap_enumerator_skip_interval(&_enumerator); continue; })

#endif
