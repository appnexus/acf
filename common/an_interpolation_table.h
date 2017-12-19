#ifndef COMMON_AN_INTERPOLATION_TABLE_H
#define COMMON_AN_INTERPOLATION_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * Interpolation table structure used to make searching on sorted arrays of arbitrary structs faster. Requires that the sorted arrays have <= INT32_MAX elements.
 */
struct an_interpolation_table {
	uint64_t multiplier;	/** this is used to avoid dividing to interpolate approximate n/d by (n * (2^k/d) )/ 2^k. The multiplier is (2^k/d) */
	uint64_t min;		/** min key value in sorted array */
	uint64_t delta;		/** precomputed max - min */
	uint32_t *buckets;	/** array of buckets (indices in sorted array) */
	uint32_t index_range[2];/** holds [0,n_elem] used for small object optimization */
};

/**
 * @brief Initialize and allocate buckets for an interpolation table
 * @param at Pointer to already allocated struct an_interpolation_table
 * @param sorted Pointer to he array of sorted elements
 * @param n_elem the number of elements in sorted array (must be less than UINT32_MAX)
 * @param n_buckets the number of buckets in your interpolation table
 * @param size size in bytes of each element
 * @param key_function Pointer to function that maps each element to a uin64_t key
 *
 * Since the interpolation table is only valid for a static sorted array, we save the min, max
 * number of elements and stride to avoid doing unnecessary computation during lookups.
 *
 * at->delta is set to UINT64_MAX if sorted is null or nelem is zero
 * at->buckets is set to NULL and index_range is used instead if n_buckets <= 1 or delta <= 1
 */
void an_interpolation_table_init_internal(struct an_interpolation_table *at,
    const void *sorted, size_t n_elem, size_t n_buckets, size_t size, uint64_t (*key_fn)(const void *));

#define an_interpolation_table_init(TABLE, SORTED, NELEM, NBUCKETS, KEY_FN)			\
	an_interpolation_table_init_internal((TABLE),						\
	    (SORTED), (NELEM), (NBUCKETS), sizeof((SORTED)[0]),					\
	    AN_CC_CAST_CONST_CB(uint64_t, (KEY_FN), (SORTED)[0]))				\

/**
 * @brief maps the offset of a key where offset = (key - min) being searched for to an index
 * such that sorted[buckets[index]] <= searched_key
 * Assumes that the key is min <= key <= max
 */
static inline uint64_t
an_interpolation_table_get_lower_index(const struct an_interpolation_table *at, uint64_t offset)
{

	return ((__uint128_t)at->multiplier * offset) >> 64;
}

/**
 * @brief Returns the bucket (high and low inclusive indices) the search key may be in inside the sorted array. Also returns true if the bucket contains
 * 1 value or no values, and false otherwise
 * @param search the key being searched for
 *
 * Returns the bucket in the interpolation table that the search key potentially lies in.
 * The bucket is a pair of indices low and high such that sorted[buckets[low]] <= search < sorted[buckets[high]].
 * Another way to look at it is that the key exists in indices in range [low, high). Note that
 * this method does not definitely say whether or not the search key is in the sorted array unless the search
 * key is either less than the minimum value or greater than the maximum value.
 */
static inline bool
an_interpolation_table_get_indices(const struct an_interpolation_table *at, uint64_t search, uint32_t *ret_low_index, uint32_t *ret_high_index)
{
	const uint32_t *buckets;
	uint64_t offset = search - at->min;

	buckets = (at->buckets == NULL) ? at->index_range : at->buckets;

	/* take advantage of unsignedness to compute whether search lies in the range [min, max] with one comparison */
	if (offset <= at->delta) {
		size_t index = an_interpolation_table_get_lower_index(at, offset);
		uint32_t low = buckets[index];
		uint32_t high = buckets[index + 1];
		bool ret = (low & 1) != 0;

		/* clear the low  bits of both the upper and lower index */
		low >>= 1;
		high >>= 1;

		*ret_low_index = low;
		*ret_high_index = high;

		return ret;
	}

	if (search < at->min) {
		*ret_low_index = *ret_high_index = 0;
		return true;
	}

	/* It can only be greater than the max */
	*ret_low_index = *ret_high_index = at->index_range[1] >> 1;

	return true;
}

/**
 * @brief Deallocates the interpolation table
 */
void an_interpolation_table_deinit(struct an_interpolation_table *at);

#endif /* !COMMON_AN_INTERPOLATION_TABLE_H */
