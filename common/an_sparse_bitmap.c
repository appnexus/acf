#include <assert.h>
#include <limits.h>

#include "common/an_cc.h"
#include "common/an_sparse_bitmap.h"
#include "common/libevent_extras.h"
#include "common/util.h"

/*
 * Sorted vector searches. They all return -1UL on failure, so adding
 * 1 yields an exclusive upper bound.
 */
/**
 * @brief Returns the max k such that `haystack[k] < needle`, `-1UL` if none.
 * @param haystack A sorted vector of uint32_t.
 * @param n The number of values in `haystack`.
 * @param needle The value to bsearch for in `haystack`.
 */
static size_t bsearch32(const uint32_t *haystack, size_t n, uint32_t needle);

/**
 * @brief Returns the max k such that `haystack[k].lower <= needle`, `-1UL` if none.
 * @param haystack A sorted vector of intervals.
 * @param n The number of values in `haystack`.
 * @param needle The value to bsearch for in `haystack`.
 */
static size_t bsearch_lower(const struct interval *haystack, size_t n,
    uint32_t needle);
/**
 * @brief Returns the max k such that `haystack[k].upper < needle`, `-1UL` if none.
 * @param haystack A sorted vector of intervals.
 * @param n The number of values in `haystack`.
 * @param needle The value to bsearch for in `haystack`.
 */
static size_t bsearch_upper(const struct interval *haystack, size_t n,
    uint32_t needle);

/**
 * @brief Sanity check for sorted intervals.  They are non-empty,
 * completely disjoint and monotonically increasing. Enabled by
 * defining `AN_SPARSE_BITMAP_CHECK`. */
static void check_intervals(struct an_array_interval *intervals);

/**
 * @brief Test whether `interval` contains a `x`.
 */
static inline bool
contains(const struct interval interval, uint32_t x)
{
	return (interval.lower <= x) && (x < interval.upper);
}

/**
 * @brief Replace a subrange in a array of interval with another of arbitrary length.
 * @param intervals The sorted `an_array` of intervals.
 * @param from The first (inclusive) interval to replace.
 * @param to The last (exclusive) interval to replace.
 * @param new the vector of intervals to splice in.
 * @param n The number of new intervals to splice in.
 */
static void
replace_range(struct an_array_interval *intervals, size_t from, size_t to,
    const struct interval *new, size_t n);

void
an_sparse_bitmap_init(struct an_sparse_bitmap *bitmap, size_t initial_length)
{

	memset(bitmap, 0, sizeof(*bitmap));
	initial_length = CLAMP(initial_length, 1UL, (size_t)INT_MAX);
	an_array_init_interval(&bitmap->intervals, initial_length);
	return;
}

struct an_sparse_bitmap *
an_sparse_bitmap_create(size_t initial_length)
{

	_Static_assert(sizeof(struct an_sparse_bitmap) == sizeof(struct an_array_interval),
	    "The only member of an_sparse_bitmap is an an_array_interval");
	return (void *)an_array_create_interval(initial_length);
}

void
an_sparse_bitmap_deinit(struct an_sparse_bitmap *bitmap)
{

	an_array_deinit_interval(&bitmap->intervals);
	memset(bitmap, 0, sizeof(*bitmap));
	return;
}

void
an_sparse_bitmap_destroy(struct an_sparse_bitmap *bitmap)
{

	if (bitmap == NULL) {
		return;
	}

	an_array_destroy_interval((void *)bitmap);
	return;
}

static size_t
bsearch32(const uint32_t *haystack, size_t n, uint32_t needle)
{
	const uint32_t *lo = haystack;

	if ((n == 0) || (*lo >= needle)) {
		return -1UL;
	}

	while (n > 1) {
		const uint32_t *pivot = lo + n / 2;

		lo = (*pivot < needle) ? pivot : lo;
		n = (n + 1) / 2;
	}

	return lo - haystack;
}

/*
 * Instead of the usual lower/upper bound pair, these binary searches
 * operate on a pair of:
 *  1. pointer to lower bound;
 *  2. range length.
 *
 * There are two advantages:
 *  1. Only a single conditional move;
 *  2. GCC really likes to convert pairs of conditional moves into
 *  branches; our branches are *not* predictable, so that behaviour
 *  is not ideal.
 *
 * The range length is a bit surprising: we round up to compute the
 * next range length, and down when incrementing the lower bound.  The
 * result is that the range is always exact or slightly too long, but
 * never goes out of bounds.
 */
static size_t
bsearch_lower(const struct interval *haystack, size_t n, uint32_t needle)
{
	const struct interval *lo = haystack;

	if ((n == 0) || (lo->lower > needle)) {
		return -1UL;
	}

	while (n > 1) {
		const struct interval *pivot = lo + n / 2;

		lo = (pivot->lower <= needle) ? pivot : lo;
		n = (n + 1) / 2;
	}

	return lo - haystack;
}

static size_t
bsearch_upper(const struct interval *haystack, size_t n, uint32_t needle)
{
	const struct interval *lo = haystack;

	if ((n == 0) || (lo->upper >= needle)) {
		return -1UL;
	}

	while (n > 1) {
		const struct interval *pivot = lo + n / 2;

		lo = (pivot->upper < needle) ? pivot : lo;
		n = (n + 1) / 2;
	}

	return lo - haystack;
}

static void
check_intervals(struct an_array_interval *intervals)
{
#ifdef AN_SPARSE_BITMAP_CHECK
	size_t i, n;
	struct interval *vec = intervals->values;

	n = an_array_length_interval(intervals);
	if (n == 0) {
		return;
	}

	/* Three invariants: intervals are non-empty, disjoint, and
	 * strictly increasing */
	assert(vec[0].lower < vec[0].upper);
	for (i = 1; i < n; i++) {
		assert(vec[i].lower < vec[i].upper);
		assert(vec[i - 1].upper < vec[i].lower);
	}
#else
	(void)intervals;
#endif

	return;
}

/*
 * intervals[from..to) <- new[0..n)
 *
 * Do that in two steps that minimise the amount of shuffling.	First,
 * shift the tail (everything in intervals[to, end)) in place unless
 * that's a no-op.  Only then, memcpy the new intervals in place.
 */
static void
replace_range(struct an_array_interval *intervals, size_t from, size_t to,
    const struct interval *new, size_t n)
{
	size_t m = to - from;
	size_t new_total = intervals->n_entries + n - m;
	size_t tail = intervals->n_entries - to;

	if (intervals->capacity < new_total) {
		size_t new_length = max(2 * intervals->capacity, new_total);
		an_array_resize_interval(intervals, new_length);
	}

	if (m != n) {
		memmove(intervals->values + from + n, intervals->values + to,
		    tail * sizeof(struct interval));
	}

	memcpy(intervals->values + from, new, n * sizeof(struct interval));
	intervals->n_entries = new_total;
	check_intervals(intervals);
	return;
}

/*
 * Insert a new range `[lower, upper)` in `bitmap`.
 *
 * Do that by iterating over all intervals in `bitmap` that overlap
 * are are adjacent to `[lower, upper)`.  Unite them with the initial
 * new interval, and then replace them with the new interval.  That
 * way, we handle all the easy cases (no-op, extending a range)
 * naturally, as well as the hard ones (joining multiple ranges).
 */
void
an_sparse_bitmap_insert_range(struct an_sparse_bitmap *bitmap,
    uint32_t lower, uint32_t upper)
{
	struct interval new = {.lower = lower, .upper = upper};
	size_t begin = 0;
	size_t i, n = bitmap->intervals.n_entries;
	struct interval *intervals = bitmap->intervals.values;

	if (lower >= upper) { /* Inserting an empty interval is a no-op */
		return;
	}

	/* intervals[0...begin) < lower */
	begin = bsearch_upper(intervals, n, lower) + 1;
	for (i = begin; (i < n) && (intervals[i].lower <= new.upper); i++) {
		assert(new.lower <= intervals[i].upper);
		new.lower = min(new.lower, intervals[i].lower);
		new.upper = max(new.upper, intervals[i].upper);
	}
	/* upper < intervals[i..) */

	assert(new.lower < new.upper);
	replace_range(&bitmap->intervals, begin, i, &new, 1);
	return;
}

/*
 * Remove range `[lower, upper)` from `bitmap`.
 *
 * This is the same as insertion, except that the removed range
 * `[lower, upper)` is removed from the union of
 * intersections/adjacent ranges.
 *
 * In effect, this first inserts `[lower, upper)` in `bitmap` so that
 * it is the range to remove falls in exactly one interval, and then
 * splits that single interval by hollowing out `[lower, upper)`.
 *
 * We detect when we would insert empty ranges, and `replace_range`
 * then naturally handles nice cases (e.g. shortening one range or
 * two).
 */
void
an_sparse_bitmap_remove_range(struct an_sparse_bitmap *bitmap,
    uint32_t lower, uint32_t upper)
{
	struct interval new[2] = {{.lower = lower, .upper = lower},
				  {.lower = upper, .upper = upper}};
	size_t begin = 0;
	size_t i, n = bitmap->intervals.n_entries;
	struct interval *intervals = bitmap->intervals.values;
	bool insert_lower, insert_upper;

	if (lower >= upper) {
		return;
	}

	/* see above */
	begin = bsearch_upper(intervals, n, lower) + 1;
	for (i = begin; (i < n) && (intervals[i].lower <= upper); i++) {
		assert(lower <= intervals[i].upper);
		new[0].lower = min(new[0].lower, intervals[i].lower);
		new[1].upper = max(new[1].upper, intervals[i].upper);
	}

	insert_lower = new[0].lower != new[0].upper;
	insert_upper = new[1].lower != new[1].upper;
	replace_range(&bitmap->intervals, begin, i,
	    insert_lower ? new : (new + 1),
	    insert_lower + insert_upper);
	return;
}

/*
 * Test for containment: find the last interval `[l, u)` such that
 * `l <= needle`.  We then have an intersection iff `needle < u`.
 */
bool
an_sparse_bitmap_contains(const struct an_sparse_bitmap *bitmap,
    uint32_t needle)
{

	if (bitmap == NULL) {
		return false;
	}

	size_t hit, n = bitmap->intervals.n_entries;
	struct interval *intervals = bitmap->intervals.values;

	if (n == 0) {
		return false;
	}

	hit = bsearch_lower(intervals, n, needle);
	return (hit < n) && contains(intervals[hit], needle);
}

/*
 * Determine whether a sparse bitmap intersects with a sorted vector
 * of `n` uint32_t.
 *
 * Do that by alternating leap-frogging binary searches and a few
 * (about log(n)) iterations of linear search.	It's meaningful to
 * compare intervals with integers because we return (successfully)
 * whenever they intersect; needles[j] is thus always smaller or
 * greater than all the values in intervals[i].
 *
 * The alternation means that, asymptotically, this function is never
 * worse than pure leap-frogging linear or binary searches, or than
 * repeated binary searches.  In fact, it is adaptive to local
 * variations in interval/key density.
 *
 * The binary searches are always over the complete vectors to benefit
 * from caches: the searches will always hit the same indices during
 * the first iterations, and incur fewer cache misses.	Binary search
 * is logarithmic-time, so it takes a large reduction in the number of
 * remaining intervals/needles to really affect the runtime of each
 * search.
 */
size_t
an_sparse_bitmap_intersects(const struct an_sparse_bitmap *bitmap,
    const uint32_t *needles, size_t n)
{
	const struct interval *intervals = bitmap->intervals.values;
	size_t i = 0, j = 0, nlinear;
	size_t m = bitmap->intervals.n_entries;

	if ((m == 0) || (n == 0)) {
		return -1UL;
	}

	/*
	 * Linear search will tend to be particularly useful on tiny
	 * inputs, so cap the number of linear leapfrogging sub-iterations
	 * from below. 8 is arbitrary, but touches 1-2 cache line.
	 */
	nlinear = max(8UL, log2_ceiling(min(m, n)));

	for (;;) {
		if (contains(intervals[i], needles[j])) {
			return j;
		}

		if (needles[j] < intervals[i].lower) {
			j = bsearch32(needles, n, intervals[i].lower) + 1;
			if (j >= n) {
				return -1UL;
			}
		} else {
			i = bsearch_upper(intervals, m, needles[j]) + 1;
			if (i >= m) {
				return -1UL;
			}
		}

		for (size_t k = 0; k < nlinear; k++) {
			if (contains(intervals[i], needles[j])) {
				return j;
			}

			if (needles[j] < intervals[i].lower) {
				if (++j >= n) {
					return -1UL;
				}
			} else if (++i >= m) {
				return -1UL;
			}
		}
	}
}

void
an_sparse_bitmap_to_string(const struct an_sparse_bitmap *bitmap, bstring b)
{
	const struct interval *intervals = bitmap->intervals.values;
	size_t n = bitmap->intervals.n_entries;

	if (n == 0) {
		return;
	}

	for (size_t i = 0; i < n; i++) {
		bformata(b, "%d-%d,", intervals[i].lower, intervals[i].upper - 1);
	}

	btrunc(b, blength(b) - 1);
	return;
}

void
an_sparse_bitmap_append_json(struct evbuffer *out, const char *name,
    const struct an_sparse_bitmap *bitmap, int comma)
{

	append_pre(out, name, comma);
	if (bitmap == NULL) {
		evbuffer_add(out, "[]", 2);
	} else {
		size_t n = bitmap->intervals.n_entries;
		const struct interval *intervals = bitmap->intervals.values;

		evbuffer_add(out, "[", 1);
		for (size_t i = 0; i < n; i++) {
			uint32_t left = intervals[i].lower;
			uint32_t right = intervals[i].upper - 1;

			if (i == 0) {
				evbuffer_add_printf(out, "{\"from\":%d, \"to\":%d}",
				    left, right);
			} else {
				evbuffer_add_printf(out, ", {\"from\":%d, \"to\":%d}",
				    left, right);
			}
		}

		evbuffer_add(out, "]", 1);
	}

	append_post(out, comma);
	return;
}
