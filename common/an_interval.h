#ifndef AN_INTERVAL_H
#define AN_INTERVAL_H
/**
 * A compact, read-optimised, representation of bags of arbitrarily
 * overlapping intervals.
 *
 * # Semantics
 *
 * Given a bag of [base, base + width] -> value mappings, construct an
 * index such that we can easily query for:
 *
 *  1. Given one key `x`, iterate over the bag of values such that x \in [base, base + width];
 *
 *  2. Given a set of keys x0, x1, ... xn, iterate over the set of values such that
 *       \exists i < n s.t. x_i \in [base, base + width], allowing for an arbitrary
 *       number of repetitions of the values. I.e., the same value might appear three
 *       times, regardless of how many mappings match the key
 *
 * # Interface
 *
 * ## Creating an an_interval.
 *
 *  1. Create an opaque builder object with an_interval_builder_create.
 *
 *  2. For each mapping in the bag, call an_interval_builder_insert;
 *     the value must be a 31 bit unsigned value, and must not be
 *     the sentinel value UINT31_MAX
 *
 *  3. Either call an_interval_builder_free to release the builder object,
 *     or convert it to an immutable an_interval object with an_interval_init.
 *     A call to an_interval_init consumes its an_interval_builder argument.
 *
 *     an_interval_init
 *
 *  4. Release the an_interval's contents with an_interval_deinit.
 *
 * ## Using an an_interval.
 *
 *  1. With AN_INTERVAL_FOREACH(interval, cursor, key, value),
 *     where interval is a pointer to an an_interval, cursor is NULL or a pointer to
 *     a struct an_interval_cursor, key is the 64 bit key to look for (x), and
 *     value is a uint32_t variable that will receive the value for the current mapping.
 *     If the cursor is non-NULL, repeated searches with strictly increasing keys
 *     will be faster, but some ranges that match multiple keys will only be seen
 *     once.
 *
 *  2. With an_interval_contains for one-off containment queries.
 *
 * # Implementation
 *
 * The data structure is based on Chazelle's filtering search
 * structure [Filtering Search: a New Approach to
 * Query-Answering](https://www.cs.princeton.edu/~chazelle/pubs/FilteringSearch.pdf).
 *
 * ## What does it do?
 *
 * The gist of the technique for interval queries is to partition the
 * key space in subranges such that traversing all the intervals that
 * intersect with any subrange is "efficient enough."  Chazelle deems
 * a search "efficient enough" if the number of intervals it looks at
 * (all the intervals that intersect with the subrange) is at most
 * \delta times the number of intervals that actually match the key
 * we're looking for.  For each subrange S_i, let
 *
 *  # n_i is the number of intervals that intersect the subrange.
 *  n_i = |{ y \in intervals | x \cap S_i != \emptyset }|.
 *
 * The performance guarantee is
 *  \forall x \in S_i, n_i \leq \delta |{y \in intervals | x \in y}|.
 *
 * In other words,
 *   n_i \leq \delta \min_{x\in S_i} |{y \in intervals | x \in y}|.
 *
 * In particular, if a subrange includes one "x" such that no interval
 * contains x, then that subrange must not intersect with any interval.
 *
 * That sucks.
 *
 * Our implementation relaxes the constraint into
 *  n_i \leq k + \delta \min_{x\in S_i} |{y \in intervals | x \in y}|.
 *
 * Given such a partition into subranges, we can efficiently find the
 * set of entries that contain a point by:
 *
 *  1. Find the subrange that contains the point (there is exactly
 *  one) in log(n) time for a sorted set search;
 *
 *  2. Look at all the intervals that intersect with that subrange to
 *  check for containment.  The filtering search bound means we will
 *  never do much more work than the number of hits (i.e. the lower
 *  bound on work).
 *
 * The rest of Chazelle's paper shows how to build such a set of
 * subranges without using superlinear space (to represent
 * intersecting intervals for each subrange).
 *
 * We deviate from Chazelle's description in another important way in
 * practice.  Chazelle's assume that there is a fast (sorted/hash set)
 * representation for subranges, but then traverses the set of
 * intersecting intervals naively.  If we further sort that set by the
 * lower end of the range, we can disregard all intervals [l, u] such
 * that x < l.
 *
 * This again lets us relax the density/wasted work constraint from
 *   n_i \leq k + \delta \min_{x\in S_i} |{y \in intervals | x \in y}|
 * to
 *   \forall x\in S_i, |{y = [l, u] \in intervals | y \cap S_i != \emptyset /\ l <= x}| \leq k + \delta |{y \in intervals | x \in y}|.
 *
 * That is, the left hand side of the inequation only considers
 * intervals that:
 *  1. intersect with S_i, *and*
 *  2. have a lower bound less than or equal to x.
 *
 * This also happens to simplify the construction phase, as we don't
 * have to remember the minimum # of containing elements anymore.
 *
 * ## How does it do it?
 *
 * Chazelle suggests a very non-clever representation, with a sorted
 * set of (disjoint) subranges to a linked list of entries (interval
 * -> value mappings).
 *
 * We instead have a sorted array of entries with intervals truncated
 * to the containing subrange.  For example, if we have a subrange [2,
 * 5], and an interval [3, 6], the array would contain an entry for
 * interval [3, 5], and another entry for interval [6, 6]
 * (corresponding to subrange [6, ??]).
 *
 * Truncating means that we can now sort on the lower bound of
 * intervals, and achieve:
 *
 *  1. contiguous entries for each subrange, and subranges sorted in
 *  increasing order (subranges partition the key range so they don't
 *  overlap and "increasing order" is meaningful).
 *
 *  2. sorted entries (on the lower bound) within each subrange.
 *
 * We still have to determine where a subrange's entries begin/end.
 * We could simply have another sorted array for subrange -> index in
 * the entries, like CSC/CSR sparse matrices.
 *
 * That seems not ideal if we expect a lot of small intervals, and
 * thus a lot of subranges with 1 or even 0 entries.  The
 * representation uses one bit in the "value" field to mark the first
 * entry in a subrange.  We can thus find the last entry with a lower
 * bound l <= x, and traverse entries to the left until we hit the
 * first entry in the subrange.
 *
 * That's still not enough: there are subranges with no entry in them,
 * so the predecessor search ends up returning the subrange to their
 * left.  When that left neighbour has too many entries (more than k, the
 * additive wasted work factor), we insert a sentinel entry that's the
 * "first" entry in the empty subrange, with a distinguished value that
 * means "not actually an entry."
 *
 * The lookup algorithm is now:
 *
 *  Input: - an array of (interval -> value) entries, sorted on lower bound;
 *         - a key to search for.
 *
 *  Output: the set of all values for intervals that contain the key.
 *
 *  1. Find the last entry in the array such that its lower bound is <= key
 *  2. If that entry is a sentinel, the search completes with no result.
 *  3. Otherwise, iterate over entries, in right-to-left order:
 *     a. check for containment (i.e., (key - base) <= width in unsigned 64 bit arithmetic)
 *     b. if the interval does contain the key, its value is (value_and_first >> 1);
 *     c. stop after the first entry in the subrange (i.e., (value_and_first & 1) == 1).
 *
 * an_interval exposes the search above with an iterator interface, but the code
 * is morally the same as the above.
 *
 * ## Constructing the index
 *
 * The construction phase consists of two pieces of logic:
 *  1. determine when to close a subinterval and open a new one;
 *  2. write all the entries that correspond to the newly closed subinterval.
 *
 * The key to determining when to close subintervals (efficiently) is
 * to sort entries by the lower end of their interval, and to
 * separately sort the upper end of intervals.
 *
 * For any value x, the number of enclosing intervals is:
 *  |{ e \ in entries | x >= e.lower_end }| - |{ u \in upper_bounds | x > u }|
 *
 * We can iterate over the merged entries and upper bound streams to
 * incrementally maintain the number of enclosing intervals.
 *
 * We also know the number of entries in the subrange: it starts with
 * whatever entries spilled over from the previous subrange, and we
 * add one whenever we pop off an entry from the sorted entries
 * stream.
 *
 * When the number of entries is greater than what our wasted work
 * bound allows, i.e., # entries > \delta * enclosing + k, it's time
 * to close the subrange and open a new one.
 *
 * We close a subrange by taking all the entries in the subrange (we
 * accumulated them in the outer loop that determines subrange
 * boundary), and clamping them to the subrange.  We sort the result,
 * mark the first entry as being the first in the subrange, and append
 * all that to the sorted array of entries.
 *
 * If the new set of active intervals is empty (i.e., the next
 * subrange will include at least one value that isn't contained by
 * any interval), and the current subrange has more than "k" (our
 * additive work factor) entries, we also insert a sentinel to stop
 * the search early.
 *
 * In some cases, the sentinel ends up at the exact same lower end as
 * the next interval.  When that's the case, the sentinel is useless
 * and we drop it.
 *
 * The linear space usage bounds follow from the fact that our
 * criterion for breaking subranges is a strict relaxation of
 * Chazelle's; the sentinels are also safe because we only insert
 * sentinels after subranges of at least "k" entries, and we know the
 * sum of entries per subrange is linear in the input size.
 */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "common/an_cc.h"
#include "common/an_interpolation_table.h"

struct an_interval_builder;

struct an_interval_entry {
	uint64_t base;
	uint32_t width;
	uint32_t value_and_first; /* low bit = first */
};

/* Values are 31 bit unsigned ints; use the last one as a sentinel. */
#define AN_INTERVAL_SENTINEL_VALUE ((uint32_t)INT32_MAX)

struct an_interval {
	struct an_interval_entry *entries;
	size_t n_entries;
	struct an_interpolation_table interpolation_table;
};

struct an_interval_cursor {
	size_t limit;
};

#define AN_INTERVAL_CURSOR_INIT ((struct an_interval_cursor) { .limit = 0 })

struct an_interval_iterator {
	const struct an_interval_entry *i;
	const struct an_interval_entry *limit;
	uint64_t key;
};

/**
 * Opaque an_interval factory.
 *
 * You probably don't want to call an_interval_builder_free, except as
 * part of error handling.
 */
struct an_interval_builder *an_interval_builder_create(void);
void an_interval_builder_free(struct an_interval_builder *);

/**
 * Insert a new ([base, base + width] -> value) entry in the factory.
 *
 * The range is inclusive because we can hopefully detect empty
 * intervals before inserting them, and we supporting a range of size
 * 2^32 is useful.
 *
 * @a value is really an unsigned 31 bit value that's also not the
 * sentinel, so anything in [0, 2^31 - 1).
 */
void an_interval_builder_insert(struct an_interval_builder *,
    uint64_t base, uint32_t width, uint32_t value);

/**
 * @brief Consume a builder and initialise an an_interval structure in place.
 *
 * We relax Chazelle's wasted work bound with an additive factor.
 *
 * If the result of a query comprises r elements, we will look *at most* at
 *  (work_factor * r + work_additive elements + 1) entries.
 *
 * A work factor of 2 and work_additive of ~4 probably makes sense.
 * In theory, we could reduce work_factor logarithmically (for example)
 * wrt the input size, but that has a marginal effect.
 */
void an_interval_init(struct an_interval *, struct an_interval_builder *,
    double work_factor, size_t work_additive);

/**
 * @brief release the resources the an_interval refers to, but not the
 * an_interval struct itself.
 */
void an_interval_deinit(struct an_interval *);

/**
 * @brief returns whether the an_interval contains @a key.
 *
 * The cursor argument may be NULL; if it isn't, it will speed up
 * containment checks for a set of keys (in monotonically increasing
 * order)... at the expense of *sometimes* skipping intervals that
 * contains the current key if they also contain a previous key.
 */
AN_CC_UNUSED static bool an_interval_contains(const struct an_interval *, struct an_interval_cursor *, uint64_t key);

/**
 * @a brief is there any entry to explore
 *
 * if @a cursor is NULL, return true iff the interval contains no entry.
 * if @a cursor is a valid cursor, return true iff some keys greater
 * than the previous lookup may find a hit.
 *
 * This function is useful to abort searches early when intersecting an
 * an_interval with a sorted set of keys.
 */
static inline bool
an_interval_empty(const struct an_interval *interval, const struct an_interval_cursor *cursor)
{
	size_t limit = (cursor == NULL) ? 0 : cursor->limit;

	return limit >= interval->n_entries;
}

/**
 * @a brief the minimum key value for which we might find a containing interval
 *
 * if @a cursor is NULL, this is the lowest lower bound in the set of intervals.
 * if @a cursor is a valid cursor, this is the next lower bound in the set of intervals.
 *
 * This function is useful when intersecting an an_interval with a
 * sorted set of keys: we can skip any key that's less than `min_value`.
 */
static inline uint64_t
an_interval_min_value(const struct an_interval *interval, const struct an_interval_cursor *cursor)
{
	size_t limit = (cursor == NULL) ? 0 : cursor->limit;

	if (limit >= interval->n_entries) {
		return UINT64_MAX;
	}

	return interval->entries[limit].base;
}

/**
 * @a brief iterate over the entries in @a INTERVAL that contain @a KEY.
 *
 * @param INTERVAL a pointer to the an_interval to iterate over.
 * @param CURSOR NULL, or a pointer to a valid an_interval_cursor.
 * @param KEY the 64 bit key to look for
 * @param VALUE a uint32_t variable that will contain the value associated with any hit.
 *
 * If @a CURSOR is non-NULL the search skips over entries that were
 * explored in earlier searches.  Assuming @a KEY are monotonically
 * increasing, the set-set intersection will iterate over all matching
 * entries, but might iterate over the same entry multiple times
 * (i.e., we get at least once semantics).
 */
#define AN_INTERVAL_FOREACH(INTERVAL, CURSOR, KEY, VALUE)			\
	for (struct an_interval_iterator an_interval_it = an_interval_iterator_init((CURSOR), (INTERVAL), (KEY)); \
	     an_interval_iterator_advance(&an_interval_it, &(VALUE)) == true; )

/**
 * Inline implementation noise.
 */
struct an_interval_iterator an_interval_iterator_init(struct an_interval_cursor *,
    const struct an_interval *, uint64_t key);

static inline bool
an_interval_iterator_advance(struct an_interval_iterator *it, uint32_t *OUT_value)
{
	const struct an_interval_entry *i = it->i;
	const struct an_interval_entry *limit = it->limit;
	uint64_t key = it->key;

	while (i --> limit) {
		const struct an_interval_entry current = *i;
		uint32_t value = current.value_and_first >> 1;
		bool first = (current.value_and_first & 1) != 0;

		/* Got a hit! */
		if (AN_CC_LIKELY((key - current.base) <= (uint64_t)current.width)) {
			it->i = i;
			/* No need to look past this entry. */
			if (first == true) {
				it->limit = i;
			}

			*OUT_value = value;
			return value != AN_INTERVAL_SENTINEL_VALUE;
		}

		if (first == true) {
			return false;
		}
	}

	return false;
}

AN_CC_UNUSED static bool
an_interval_contains(const struct an_interval *interval, struct an_interval_cursor *cursor, uint64_t key)
{
	uint32_t value;

	AN_INTERVAL_FOREACH(interval, cursor, key, value) {
		(void)value;

		return true;
	}

	return false;
}
#endif /* !AN_INTERVAL_H */
