#include <limits.h>
#include <math.h>

#include "common/an_array.h"
#include "common/an_interval.h"
#include "common/an_malloc.h"
#include "common/util.h"

#define AN_INTERVAL_INTERPOLATION_TABLE_SCALE 8

AN_ARRAY(an_interval_entry, entry);

struct an_interval_builder {
	AN_ARRAY_INSTANCE(entry) entries;
};

static AN_MALLOC_DEFINE(builder_token,
    .string = "an_interval_builder",
    .mode = AN_MEMORY_MODE_FIXED,
    .size = sizeof(struct an_interval_builder));

static AN_MALLOC_DEFINE(entries_token,
    .string = "an_interval.entries",
    .mode = AN_MEMORY_MODE_VARIABLE);

struct an_interval_builder *
an_interval_builder_create(void)
{

	return an_calloc_object(builder_token);
}

void
an_interval_builder_free(struct an_interval_builder *builder)
{

	if (builder == NULL) {
		return;
	}

	AN_ARRAY_DEINIT(entry, &builder->entries);
	an_free(builder_token, builder);
	return;
}

void
an_interval_builder_insert(struct an_interval_builder *builder,
    uint64_t base, uint32_t width, uint32_t value)
{
	struct an_interval_entry entry = {
		.base = base,
		.width = min((uint64_t)width, UINT64_MAX - base),
		.value_and_first = value << 1
	};

	/* Value must be a 31 bit int and can't be the sentinel. */
	assert(value <= (uint32_t)INT32_MAX);
	assert(value != AN_INTERVAL_SENTINEL_VALUE);
	AN_ARRAY_PUSH(entry, &builder->entries, &entry);
	return;
}

static int
cmp_interval(const struct an_interval_entry *x, const struct an_interval_entry *y)
{

	if (x->base != y->base) {
		return (x->base < y->base) ? -1 : 1;
	}

	if (x->width != y->width) {
		return (x->width < y->width) ? -1 : 1;
	}

	if (x->value_and_first != y->value_and_first) {
		return (x->value_and_first < y->value_and_first) ? -1 : 1;
	}

	return 0;
}

static int
cmp_endpoint(const uint64_t *x, const uint64_t *y)
{

	if (*x == *y) {
		return 0;
	}

	return (*x < *y) ? -1 : 1;
}

static uint64_t
get_base(const struct an_interval_entry *x)
{

	return x->base;
}

static void
sort_endpoints(uint64_t *endpoints, AN_ARRAY_INSTANCE(entry) *input)
{
	struct an_interval_entry *cursor;
	size_t i = 0;

	AN_ARRAY_FOREACH(input, cursor) {
		endpoints[i++] = cursor->base + cursor->width;
	}

	AN_QSORT(endpoints, AN_ARRAY_LENGTH(entry, input), cmp_endpoint);
	return;
}

static size_t
flush(struct an_interval_entry **entries_p, size_t n_entries, size_t *capacity_p,
    AN_ARRAY_INSTANCE(entry) *acc, uint64_t window_max, bool sentinel)
{
	struct an_interval_entry *cursor;
	struct an_interval_entry *entries = *entries_p;
	size_t begin = n_entries;
	size_t capacity = *capacity_p;
	size_t acc_size = AN_ARRAY_LENGTH(entry, acc);

	if (acc_size == 0) {
		return n_entries;
	}

	/* Geometric growth on demand. */
	while (n_entries + acc_size + sentinel > capacity) {
		size_t new_capacity = max(16UL, 2 * capacity);

		entries = an_realloc_region(entries_token, entries,
		    capacity * sizeof(*entries), new_capacity * sizeof(*entries));
		capacity = new_capacity;
	}

	/*
	 * Clamp the range's *lower bound* (we don't need to sort
	 * upper bounds), and clear any spurious "this is the first
	 * element of the subrange" bit.
	 *
	 * This is redundant because we do the same at the end of this
	 * function, but let's make the precondition extra obviously
	 * OK.
	 */
	AN_ARRAY_FOREACH(acc, cursor) {
		struct an_interval_entry copy = *cursor;

		assert(copy.base <= window_max);
		if ((uint64_t)copy.width > window_max - copy.base) {
			copy.width = window_max - copy.base;
		}

		copy.value_and_first &= ~1ULL;
		entries[n_entries++] = copy;
	}

	/* Clamping gets us sortedness across subranges. Sort within the new subrange. */
	AN_QSORT(entries + begin, n_entries - begin, cmp_interval);
	entries[begin].value_and_first |= 1;

	*entries_p = entries;
	*capacity_p = capacity;

	/*
	 * The subrange is [..., UINT64_MAX].  Keys are 64 bits so
	 * there's obviously nothing left.  No entry left.
	 */
	if (window_max == UINT64_MAX) {
		AN_ARRAY_RESET(entry, acc);
	} else {
		/* Insert a sentinel entry if we were asked to. */
		if (sentinel == true) {
			struct an_interval_entry sentinel_entry = {
				.base = window_max + 1,
				.width = 0,
				.value_and_first = (AN_INTERVAL_SENTINEL_VALUE << 1UL) | 1UL
			};

			entries[n_entries++] = sentinel_entry;
		}

		/*
		 * Clamp entries for the next subrange and discard any
		 * that doesn't even intersect with that subrange
		 * (they ended within the current subrange).
		 */
		for (size_t i = 0; i < AN_ARRAY_LENGTH(entry, acc); ) {
			uint64_t current_max;

			cursor = AN_ARRAY_VALUE(entry, acc, i);

			current_max = cursor->base + cursor->width;
			if (current_max <= window_max) {
				AN_ARRAY_REMOVE_INDEX(entry, acc, i);
				/* Replaced with the last element; do not increment i. */
			} else {
				cursor->base = window_max + 1;
				cursor->width = current_max - cursor->base;
				i++;
			}
		}

		assert(sentinel == false || AN_ARRAY_LENGTH(entry, acc) == 0);
	}

	return n_entries;
}

static size_t
build(struct an_interval_entry **entries, size_t *capacity,
    const struct an_interval_entry *input, const uint64_t *ends,
    size_t n_interval, double work_factor, size_t max_extra)
{
	AN_ARRAY_INSTANCE(entry) acc;
	size_t n_entries = 0;
	size_t current_input = 0;
	size_t current_end = 0;
	size_t active = 0;

	if (n_interval == 0) {
		return 0;
	}

	if (isfinite(work_factor) == false || work_factor <= 1) {
		work_factor = 2;
	}

	AN_ARRAY_INIT(entry, &acc, 4);

	/*
	 * Flush the current set of entries; only insert a sentinel if
	 * there is currently no containing interval, and the subrange
	 * has enough entries that we bust our wasted work bound.
	 */
#define FLUSH(WINDOW_MAX)						\
	do {								\
		n_entries = flush(entries, n_entries, capacity,		\
		    &acc, (WINDOW_MAX),					\
		    (active == 0 &&					\
		     AN_ARRAY_LENGTH(entry, &acc) > max_extra));	\
	} while (0)

	while (current_input < n_interval) {
		uint64_t window_max = UINT64_MAX;

		/*
		 * Process any interval that closes *before* the next
		 * opening interval.  Closed intervals are a bit
		 * shitty, but you'll see that strictly less than is
		 * the correct thing.
		 *
		 * If I  have intervals [1, 2]  and [2, 3], I  want to
		 * first process  opening [2,  3] and only  then close
		 * [1,  2]:  otherwise, we'll  hit  active  == 0,  and
		 * decide to close the subrange, when really active is
		 * always 1.
		 */
		while (current_end < n_interval && ends[current_end] < input[current_input].base) {
			assert(active > 0);
			window_max = ends[current_end++];
			active--;
		}

		/*
		 * We only need to check the wasted work bound here,
		 * after the only line that decrements active.
		 */
		if (AN_ARRAY_LENGTH(entry, &acc) > work_factor * active + max_extra) {
			FLUSH(window_max);
			assert(AN_ARRAY_LENGTH(entry, &acc) == active);

			if (n_entries > 0 &&
			    (*entries)[n_entries - 1].value_and_first == ((AN_INTERVAL_SENTINEL_VALUE << 1UL) | 1UL) &&
			    (*entries)[n_entries - 1].base == input[current_input].base) {
				n_entries--;
			}
		}

		/*
		 * We add an entry to the subrange, but also increment
		 * active, so we'll never violate the wasted work
		 * bound here.
		 *
		 * This deviates from Chazelle's algorithm because we
		 * also do a predecessor search within subranges, so
		 * we don't have to campare with the *minimum* number
		 * of active intervals.  Instead, we get away with
		 * comparing with the *current* number of active
		 * intervals.
		 */
		AN_ARRAY_PUSH(entry, &acc, &input[current_input++]);
		active++;
	}

	/*
	 * Don't forget the last subrange.
	 */
	FLUSH(ends[n_interval - 1]);
#undef FLUSH

	AN_ARRAY_DEINIT(entry, &acc);
	return n_entries;
}

void
an_interval_init(struct an_interval *interval, struct an_interval_builder *builder,
    double work_factor, size_t work_add)
{
	static AN_MALLOC_DEFINE(endpoint_token,
	    .string = "an_interval.endpoint",
	    .mode = AN_MEMORY_MODE_VARIABLE);
	AN_ARRAY_INSTANCE(entry) *input;
	uint64_t *endpoints;
	struct an_interval_entry *entries;
	size_t capacity;
	size_t input_size;
	size_t n_entries;

	memset(interval, 0, sizeof(*interval));

	if (builder == NULL) {
		interval->entries = NULL;
		interval->n_entries = 0;
		return;
	}

	input = &builder->entries;
	input_size = AN_ARRAY_LENGTH(entry, input);

	AN_ARRAY_SORT(entry, input, cmp_interval);

	endpoints = an_calloc_region(endpoint_token, input_size, sizeof(uint64_t));
	sort_endpoints(endpoints, input);

	capacity = 1 + ceil(3 * work_factor * input_size / (work_factor - 1));
	entries = an_calloc_region(entries_token, capacity, sizeof(*entries));

	n_entries = build(&entries, &capacity, input->values, endpoints, input_size,
	    work_factor, work_add);

	an_free(endpoint_token, endpoints);
	an_interval_builder_free(builder);

	if (n_entries == 0) {
		an_free(entries_token, entries);
		entries = NULL;
	} else {
		entries = an_realloc_region(entries_token, entries,
		    capacity * sizeof(*entries), n_entries * sizeof(*entries));
	}

	interval->entries = entries;
	interval->n_entries = n_entries;
	an_interpolation_table_init(&interval->interpolation_table, interval->entries,
	    interval->n_entries, interval->n_entries / AN_INTERVAL_INTERPOLATION_TABLE_SCALE,
	    get_base);
	return;
}

void
an_interval_deinit(struct an_interval *intervals)
{

	if (intervals == NULL) {
		return;
	}

	an_free(entries_token, intervals->entries);
	an_interpolation_table_deinit(&intervals->interpolation_table);
	memset(intervals, 0, sizeof(*intervals));
	return;
}

/*
 * We want to find the first index with base > key,
 * or n_entries otherwise.
 *
 * Let's just write bsearch by hand an nth time to make the behaviour
 * on multiple & inexact matches really obvious.
 */
static inline const struct an_interval_entry *
an_interval_bsearch(const struct an_interval_entry *entry, size_t n_entries, uint64_t key)
{
	const struct an_interval_entry *base = entry;
	size_t half;
	size_t n = n_entries;

	/* n > 1 <-> n / 2 > 0. */
	while ((half = n / 2) > 0) {
		const struct an_interval_entry *next = base + half;

		n -= half;
		base = (next->base <= key) ? next : base;
	}

	return base;
}

static inline size_t
search(const struct an_interval *interval, uint64_t key)
{
	const struct an_interval_entry *base = interval->entries;
	const struct an_interval_entry *result;
	const struct an_interpolation_table *table = &interval->interpolation_table;
	uint32_t low_index, high_index;

	an_interpolation_table_get_indices(table, key, &low_index, &high_index);

	if (low_index == high_index) {
		return (size_t)low_index;
	}

	if (base[low_index].base > key) {
		return (size_t)low_index;
	}

	result = an_interval_bsearch(&base[low_index], high_index - low_index, key);

	return (result - base) + (result->base <= key);
}

struct an_interval_iterator
an_interval_iterator_init(struct an_interval_cursor *cursor,
    const struct an_interval *interval,
    uint64_t key)
{
	struct an_interval_iterator ret;
	size_t i;

	memset(&ret, 0, sizeof(ret));

	if (key < an_interval_min_value(interval, cursor) ||
	    an_interval_empty(interval, cursor)) {
		/* We're done, and i == limit == 0, so no iteration. */
		return ret;
	}

	i = search(interval, key);
	ret.key = key;
	ret.i = &interval->entries[i];
	if (cursor != NULL) {
		ret.limit = &interval->entries[cursor->limit];
		cursor->limit = max(i, cursor->limit);
	}

	return ret;
}
