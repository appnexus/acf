#include "common/an_interpolation_table.h"
#include "common/an_malloc.h"
#include "common/an_syslog.h"
#include "common/util.h"

static AN_MALLOC_DEFINE(an_interpolation_table_buckets_token,
    .string = "an_interpolation_table.buckets",
    .mode = AN_MEMORY_MODE_VARIABLE);

void
an_interpolation_table_init_internal(struct an_interpolation_table *at, const void *sorted, size_t n_elem, size_t n_buckets, size_t size, uint64_t (*key_fn)(const void *))
{
#define VALUE_AT_INTERNAL(PTR, SIZE, INDEX, KEY_FN) (KEY_FN((void *)((char *)(PTR) + ((INDEX) * SIZE))))
#define SORTED_VALUE_AT(INDEX) VALUE_AT_INTERNAL(sorted, size, (INDEX), key_fn)
	size_t bucket_size, cur_index, actual_n_buckets;
	uint64_t delta, last, first, value_at_end;
	/* __uint128_t only to avoid explicit casts when taking high 64 bits */
	__uint128_t multiplier;

	memset(at, 0, sizeof(struct an_interpolation_table));

	if (n_elem > INT32_MAX) {
		an_syslog(LOG_ERR, "an_interpolation_table_init: Number of elements in sorted array too high %lu\n", n_elem);
		assert(n_elem <= INT32_MAX);
		return;
	}

	/* store [0, nelem) in index_range but reserve LSB for storing state about whether there is only one value in the range */
	at->index_range[1] = n_elem << 1;

	if (sorted == NULL || n_elem == 0) {
		at->delta = UINT64_MAX;
		return;
	}

	first = SORTED_VALUE_AT(0);
	last = SORTED_VALUE_AT(n_elem - 1);

	delta = last - first;
	cur_index = 0;

	at->min = first;
	at->delta = delta;

	if (n_buckets <= 1 || delta <= 1) {
		/* Use index range as our one bucket */
		if (delta == 0) {
			/* there is only a single value in the entire sorted array */
			at->index_range[0] = 1;
		}
		return;
	}

	/* coerce n_buckets to be small enough that we will always have bucket_size >= 1 */
	n_buckets = min(n_buckets, delta);

	/*
	 * Computing ceiling(delta/n_buckets) so that we will never use more buckets than asked for
	 * (so bucket size has to always round up)
	 * compute ceiling(x/y) as 1 + ((x- 1)/y) to avoid overflow
	 */
	bucket_size = 1 + ((delta  - 1) / n_buckets);

	/*
	 * Approximate division of n/d by computing
	 * multiplier = ceiling(2^k / d)
	 * so n/d = (multiplier * n) / 2^k
	 * k = 64 here
	 */
	multiplier = 1;
	multiplier <<= 64;
	multiplier = (multiplier + bucket_size - 1)/bucket_size;

	/* check for overflow in the high bits of the multiplier and if so do the best that we can w/ 64 bits */
	at->multiplier = min(multiplier, UINT64_MAX);

	/* n_buckets is the high index */
	actual_n_buckets = an_interpolation_table_get_lower_index(at, at->delta) + 1;

	/* verify that actual number of buckets is close enough */
	if (actual_n_buckets < n_buckets / 2 || actual_n_buckets > 2 * n_buckets) {
		an_syslog(LOG_DEBUG, "an_interpolation_table failed to correctly allocate buckets: asked for %lu actually allocated %lu nelem: %lu min: %lu max %lu\n",
		    n_buckets, actual_n_buckets, n_elem, first, last);
	}

	at->buckets = an_calloc_region(an_interpolation_table_buckets_token, actual_n_buckets + 1, sizeof(uint32_t));

	at->buckets[actual_n_buckets] = n_elem << 1;

	for (size_t i = 0; i < n_elem; i++) {
		uint64_t cur_value = SORTED_VALUE_AT(i);
		uint64_t index = an_interpolation_table_get_lower_index(at, cur_value - at->min);
		/* Set empty buckets to have empty range [i, i) */
		while (cur_index < index) {
			/* Use the LSB to denote whether the bucket has only one value */
			at->buckets[++cur_index] = i << 1;

			/* check if bucket is empty [i, i) and if so set the lsb */
			if ((at->buckets[cur_index] >> 1) == (at->buckets[cur_index - 1] >> 1)) {
				at->buckets[cur_index - 1] |= 1;
			} else {
				/* get endpoint values for all buckets [low, high) --> [buckets[i - 1], buckets[i])  */
				uint64_t value_at_low_index = SORTED_VALUE_AT(at->buckets[cur_index - 1] >> 1);
				uint64_t value_at_high_index = SORTED_VALUE_AT((at->buckets[cur_index] >> 1) - 1);

				/* Check if bucket has only one value in entire range and if so set the lsb */
				if (value_at_low_index == value_at_high_index) {
					at->buckets[cur_index - 1] |= 1;
				}
			}
		}

	}

	/* check the last bucket to see if its empty or has a single value */
	value_at_end = SORTED_VALUE_AT(at->buckets[actual_n_buckets - 1] >> 1);

	if (value_at_end == last) {
		at->buckets[actual_n_buckets - 1] |= 1;
	}

#undef SORTED_VALUE_AT
#undef VALUE_AT_INTERNAL
	return;
}

void
an_interpolation_table_deinit(struct an_interpolation_table *at)
{

	an_free(an_interpolation_table_buckets_token, at->buckets);
	memset(at, 0, sizeof(struct an_interpolation_table));

	return;
}
