#include <assert.h>
#include <modp_numtoa.h>
#include <stdlib.h>
#include <stdlib.h>

#include "common/an_md.h"
#include "common/an_sstm.h"
#include "common/assert_dev.h"
#include "common/btree.h"
#include "common/debug.h"
#include "common/int_set.h"
#include "common/libevent_extras.h"
#include "common/util.h"

#define INT_SET_CMP(X, Y) ((*(X) == *(Y)) ? 0 : ((*(X) < *(Y)) ? -1 : 1))

#define AN_QSORT_SUFFIX int16
#define AN_QSORT_TYPE int16_t
#define AN_QSORT_CMP INT_SET_CMP

#include "common/an_qsort.inc"

#define AN_QSORT_SUFFIX int32
#define AN_QSORT_TYPE int32_t
#define AN_QSORT_CMP INT_SET_CMP

#include "common/an_qsort.inc"

#define AN_QSORT_SUFFIX int64
#define AN_QSORT_TYPE int64_t
#define AN_QSORT_CMP INT_SET_CMP

#include "common/an_qsort.inc"

UNIQ_DEFINE(int16, int16_t, INT_SET_CMP, UNIQ_NOOP_COMBINE);
UNIQ_DEFINE(int32, int32_t, INT_SET_CMP, UNIQ_NOOP_COMBINE);
UNIQ_DEFINE(int64, int64_t, INT_SET_CMP, UNIQ_NOOP_COMBINE);

static BTREE_CONTEXT_DEFINE(default_int_set_btree_ctx, "int_set");
static BTREE_CONTEXT_DEFINE(default_pair_int_set_btree_ctx, "pair_int_set");

DEFINE_AN_SSTM_OPS(int_set_ops, "sstm_int_set_t", sstm_int_set_t,
    AN_SSTM_INIT(sstm_int_set_t, btree_overwrite),
    AN_SSTM_FREEZE(sstm_int_set_t, int_set_resume_sorting),
    AN_SSTM_RELEASE(sstm_int_set_t, btree_shallow_deinit));

void
int_set_initialize(void)
{

}

void
int_set_cleanup(void)
{

}

static int
int16_comparator(const int16_t *a, const int16_t *b)
{

	return *a - *b;
}

static int
int32_comparator(const int32_t *a, const int32_t *b)
{

	return (*a > *b) - (*a < *b);
}

static int
int64_comparator(const int64_t *a, const int64_t *b)
{

	return (*a > *b) - (*a < *b);
}

static int
pair_int_comparator(const pair_int_t *a, const pair_int_t *b)
{

	if (a->a != b->a) {
		return a->a - b->a;
	}

	return a->b - b->b;
}

bool
int_set_init(int_set_t *set, const btree_context_t *ctx, size_t int_len, size_t initial_size)
{

	assert_dev(int_len == 8 || int_len == 4 || int_len == 2);

	if (ctx == NULL) {
		ctx = default_int_set_btree_ctx;
	}

	if (int_len == 2) {
		return btree_init(set, ctx, int16_t, initial_size, int16_comparator, NULL);
	} else if (int_len == 4) {
		return btree_init(set, ctx, int32_t, initial_size, int32_comparator, NULL);
	} else {
		return btree_init(set, ctx, int64_t, initial_size, int64_comparator, NULL);
	}
}

int_set_t *
new_int_set(const btree_context_t *ctx, size_t int_len, size_t initial_size)
{

	assert_dev(int_len == 8 || int_len == 4 || int_len == 2);

	if (ctx == NULL) {
		ctx = default_int_set_btree_ctx;
	}

	if (int_len == 2) {
		return create_btree(ctx, int16_t, initial_size, int16_comparator, NULL);
	} else if (int_len == 4) {
		return create_btree(ctx, int32_t, initial_size, int32_comparator, NULL);
	} else {
		return create_btree(ctx, int64_t, initial_size, int64_comparator, NULL);
	}
}

sstm_int_set_t *
new_sstm_int_set(const btree_context_t *ctx, size_t int_len, size_t initial_size)
{

	assert_dev(int_len == 8 || int_len == 4 || int_len == 2);

	if (ctx == NULL) {
		ctx = default_int_set_btree_ctx;
	}

	if (int_len == 2) {
		return create_sstm_btree(ctx, int16_t, initial_size, int16_comparator, NULL);
	} else if (int_len == 4) {
		return create_sstm_btree(ctx, int32_t, initial_size, int32_comparator, NULL);
	} else {
		return create_sstm_btree(ctx, int64_t, initial_size, int64_comparator, NULL);
	}
}

pair_int_set_t *
new_pair_int_set(const btree_context_t *ctx, size_t initial_size)
{

	if (ctx == NULL) {
		ctx = default_pair_int_set_btree_ctx;
	}

	return create_btree(ctx, pair_int_t, initial_size, pair_int_comparator, NULL);
}

sstm_pair_int_set_t *
new_sstm_pair_int_set(const btree_context_t *ctx, size_t initial_size)
{

	if (ctx == NULL) {
		ctx = default_pair_int_set_btree_ctx;
	}

	return create_sstm_btree(ctx, pair_int_t, initial_size, pair_int_comparator, NULL);
}

void
free_int_set(int_set_t *set)
{

	if (set == NULL) {
		return;
	}

	free_btree(set);
}

void
free_sstm_int_set(sstm_int_set_t *set)
{

	if (set == NULL) {
		return;
	}

	free_sstm_btree(set);
}

void
int_set_deinit(int_set_t *set)
{

	if (set == NULL) {
		return;
	}

	btree_deinit(set);
}

void
int_set_defer(void *set)
{

	free_int_set(set);
	return;
}

void
free_pair_int_set(pair_int_set_t *set)
{

	if (set == NULL) {
		return;
	}

	free_btree(set);
}

void
free_sstm_pair_int_set(sstm_pair_int_set_t *set)
{

	if (set == NULL) {
		return;
	}

	free_sstm_btree(set);
}

void
add_int_to_set(int_set_t *set, int64_t val)
{

	assert_dev(set);
	if (set->size == 2) {
		int16_t val16 = val;
		btree_insert(set, &val16);
	} else if (set->size == 4) {
		int32_t val32 = val;
		btree_insert(set, &val32);
	} else {
		btree_insert(set, &val);
	}
}

void
add_pair_int_to_set(pair_int_set_t *set, int a, int b)
{

	pair_int_t pi = {a, b};

	btree_insert(set, &pi);
}

void
add_int_to_set_init_size(int_set_t **set, const btree_context_t *ctx, int64_t add, size_t int_len, size_t initial_capacity)
{
	if (*set == NULL) {
		*set = new_int_set(ctx, int_len, initial_capacity);
	}

	add_int_to_set(*set, add);

	return;
}

#ifndef INT_SIZE_DEFAULT_INIT
#define INT_SIZE_DEFAULT_INIT 4
#endif

void
add_int_to_set_init(int_set_t **set, const btree_context_t *ctx, int64_t add, size_t int_len)
{

	add_int_to_set_init_size(set, ctx, add, int_len, INT_SIZE_DEFAULT_INIT);

	return;
}

void
add_int_to_sstm_set_init(sstm_int_set_t **set, const btree_context_t *ctx, int64_t add, size_t int_len)
{

	if (*set == NULL) {
		an_pr_store_ptr(set,
		    new_sstm_int_set(ctx, int_len, INT_SIZE_DEFAULT_INIT));
	}
	add_int_to_set(int_set_sstm_write(*set), add);
}

bool
remove_int_from_set(int_set_t *set, int64_t val)
{

	if (set == NULL) {
		return false;
	}

	if (set->size == 2) {
		int16_t val16 = val;
		return btree_delete(set, &val16);
	} else if (set->size == 4) {
		int32_t val32 = val;
		return btree_delete(set, &val32);
	} else {
		return btree_delete(set, &val);
	}
}

void
remove_int_from_set_deinit(int_set_t **set, int64_t remove)
{

	remove_int_from_set(*set, remove);
	if (int_set_is_empty(*set)) {
		int_set_t *to_free = *set;
		*set = NULL;
		free_int_set(to_free);
	}
}

bool
remove_int_from_sstm_set_deinit(sstm_int_set_t **set, int64_t remove)
{
	sstm_int_set_t *to_free = NULL;

	bool r = remove_int_from_set(int_set_sstm_write(*set), remove);
	if (int_set_is_empty(an_sstm_read(*set))) {
		to_free = *set;
		an_pr_store_ptr(set, NULL);
	}

	free_sstm_int_set(to_free);
	return r;
}

void
remove_pair_int_from_set(pair_int_set_t *set, int a, int b)
{

	if (set == NULL) {
		return;
	}

	pair_int_t pi = {a, b};
	btree_delete(set, &pi);
}

void
clear_int_set(int_set_t *set)
{
	assert_dev(set);
	set->num = 0;
}

void
int_set_postpone_sorting(int_set_t *set, int num_new)
{

	btree_start_bulk_mode(set, num_new);
	return;
}

void
int_set_resume_sorting(int_set_t *set)
{

	set->bulk_mode = false;
	if (set->sorted == true) {
		return;
	}

	switch (set->size) {
	case 8:
		an_qsort_int64(set->base, set->num);
		set->num = UNIQ(int64, set->base, set->num);
		break;

	case 4:
		an_qsort_int32(set->base, set->num);
		set->num = UNIQ(int32, set->base, set->num);
		break;

	case 2:
		an_qsort_int16(set->base, set->num);
		set->num = UNIQ(int16, set->base, set->num);
		break;

	default:
		assert_crit(false && "Unexpected int_set size");
		abort();
	}
	set->sorted = true;

	return;
}

/*
 * The loop body compiles down to shift, add, lookup and a single
 * conditional move.  This is much simpler than testing for equality
 * (which can only bring a negligible improvement overall), and than
 * offsetting bound updates by 1 (an even more negligible
 * improvement).
 *
 * It operates with a pair of (lower bound pointer, range length) to
 * palliate issues in GCC's codegen (which detects ?: with redundant
 * tests and converts them into branches), and to minimise the number
 * of cmov.
 *
 * Range length is rounded up, so is never too short, and the lower
 * bound increment is rounded down, so we never go out of bounds.
 */
#define INT_SET_CONTAINS(W) 					\
	bool							\
	int_set_contains_##W(const int_set_t *array, int##W##_t value)\
	{							\
		size_t half, n;					\
		const int##W##_t *lo, *vector;			\
								\
		if (array == NULL) {				\
			return false;				\
		}						\
								\
		n = array->num;					\
		lo = vector = array->base;			\
								\
		if ((n == 0) || (*lo > value)) {		\
			return false;				\
		}						\
								\
		half = n / 2;					\
		while (half > 0) {				\
			const int##W##_t *mid = lo + half;	\
			lo = (*mid <= value) ? mid : lo;	\
			n -= half;				\
			half = n / 2;				\
		}						\
								\
		return *lo == value;				\
	}

INT_SET_CONTAINS(16)
INT_SET_CONTAINS(32)
INT_SET_CONTAINS(64)

#undef INT_SET_CONTAINS

static inline bool
int_set_gt_max_16(int64_t a)
{

	return a > INT16_MAX;
}

static inline bool
int_set_gt_max_32(int64_t a)
{

	return a > INT32_MAX;
}

static inline bool
int_set_gt_max_64(int64_t a)
{

	(void)a;
	return false;
}

/* Return greatest i such that array[i] < value, or -1ul if none */
#define INT_SET_LB(W)						\
	static size_t						\
	int_set_lower_bound_##W(const int_set_t *array, int64_t value, \
				size_t *out_upper_bound)	\
	{							\
		size_t half, n;					\
		const int##W##_t *lo, *vector;			\
		int##W##_t needle = value;			\
								\
		n = array->num;					\
		lo = vector = array->base;			\
								\
		if ((n == 0) || (*lo >= value)) {		\
			if (out_upper_bound != NULL) {		\
				*out_upper_bound = 0;		\
			}					\
			return -1UL;				\
		}						\
								\
		if (int_set_gt_max_##W(value)) {		\
			if (out_upper_bound != NULL) {		\
				*out_upper_bound = n;		\
			}					\
			return n - 1;				\
		}						\
								\
		half = n / 2;					    \
		/* Invariant: *lo < value and lo[hi...) >= value */ \
		while (half > 0) {				    \
			const int##W##_t *mid = lo + half;	\
								\
			lo = (*mid < needle) ? mid : lo;	\
			n -= half;				\
			half = n / 2;				\
		}						\
								\
		if (out_upper_bound != NULL) {			\
			*out_upper_bound = lo - vector + 1;	\
		}						\
		return lo - vector;				\
	}

INT_SET_LB(16)
INT_SET_LB(32)
INT_SET_LB(64)

#undef INT_SET_LB

/*
 * Test whether two int_sets intersect with a mixture of leap-frogging
 * binary and linear searches.
 *
 * For each binary search, we perform (about) log_2(n) iterations of
 * linear search; asymptotically, this is never worse than either
 * binary or linear search.  Linear search is simple and is expected
 * to work well on tiny inputs, so we cap the number of linear search
 * iterations from below.  8 is arbitrary, but on the order of half a
 * cache line.
 *
 * We also always perform binary searches over complete vectors: this
 * means that bsearch will keep hitting the same locations at the
 * beginning of each search, so the workload will be much better
 * cachable than moving lower bounds with the cursors.  The impact on
 * runtime is small: log(n) grows very slowly.
 */
#define INT_SET_INTERSECT(W1,W2)					\
	static bool							\
	int_set_intersect_##W1##_##W2(const int_set_t *one, const int_set_t *two) \
	{								\
		const int##W1##_t *one_vec;				\
		const int##W2##_t *two_vec;				\
		size_t i, j, nlinear, one_count, two_count;		\
		int64_t one_cursor, two_cursor;				\
									\
		one_count = int_set_count(one);				\
		two_count = int_set_count(two);				\
		nlinear = max(8UL, log2_ceiling(min(one_count, two_count)));	\
									\
		i = 0;							\
		j = 0;							\
		one_vec = one->base;					\
		two_vec = two->base;					\
		one_cursor = one_vec[i];				\
		two_cursor = two_vec[j];				\
									\
		for (;;) {						\
			if (one_cursor == two_cursor) {			\
				return true;				\
			} else if (one_cursor < two_cursor) {		\
				int_set_lower_bound_##W1(one,		\
				    two_cursor,				\
				    &i);				\
				if (i >= one_count) {			\
					return false;			\
				}					\
				one_cursor = one_vec[i];		\
			} else {					\
				int_set_lower_bound_##W2(two,		\
				    one_cursor,				\
				    &j);				\
				if (j >= two_count) {			\
					return false;			\
				}					\
				two_cursor = two_vec[j];		\
			}						\
			for (size_t k = 0; k < nlinear; k++) {		\
				if (one_cursor == two_cursor) {		\
					return true;			\
				} else if (one_cursor < two_cursor) {	\
					if (++i >= one_count) {		\
						return false;		\
					}				\
					one_cursor = one_vec[i];	\
				} else	{				\
					if (++j >= two_count) {		\
						return false;		\
					}				\
					two_cursor = two_vec[j];	\
				}					\
			}						\
		}							\
	}

INT_SET_INTERSECT(64, 64)
INT_SET_INTERSECT(64, 32)
INT_SET_INTERSECT(64, 16)
INT_SET_INTERSECT(32, 32)
INT_SET_INTERSECT(32, 16)
INT_SET_INTERSECT(16, 16)

#undef INT_SET_INTERSECT

bool
int_set_intersect(const int_set_t *one, const int_set_t *two)
{

	if ((int_set_is_empty(one) == true) || (int_set_is_empty(two) == true)) {
		return false;
	}

	int16_t one_size = ((binary_tree_t *)one)->size;
	int16_t two_size = ((binary_tree_t *)two)->size;

	if (one_size < two_size) {
		{
			const int_set_t *temp = one;
			one = two;
			two = temp;
		}
		{
			int64_t temp = one_size;
			one_size = two_size;
			two_size = temp;
		}
	}

	assert(one->sorted == true);
	assert(two->sorted == true);

	switch(one_size) {
	case 8: switch(two_size) {
		case 8: return int_set_intersect_64_64(one, two);
		case 4: return int_set_intersect_64_32(one, two);
		case 2: return int_set_intersect_64_16(one, two);
		default:
			assert_crit(false && "Unexpected int_set size");
			abort();
		}
	case 4:	switch(two_size) {
		case 4: return int_set_intersect_32_32(one, two);
		case 2: return int_set_intersect_32_16(one, two);
		default:
			assert_crit(false && "Unexpected int_set size");
			abort();
		}
	case 2:	assert(two_size == 2);
		return int_set_intersect_16_16(one, two);
	default:
		assert_crit(false && "Unexpected int_set size");
		abort();
	}
}

/* Utility macros for intersection below. */
#define ADV_I(INC) do {				\
		i += (INC);			\
		if (i >= one_count) {		\
			goto out;		\
		}				\
		one_cursor = one_vec[i];	\
	} while (0)

#define ADV_J(INC) do {				\
		j += (INC);			\
		if (j >= two_count) {		\
			goto out;		\
		}				\
		two_cursor = two_vec[j];	\
	} while (0)

#define ACC(X) do {				\
		dst[dst_alloc++] = (X);		\
		ADV_I(1);			\
		ADV_J(1);			\
	} while (0)

/*
 * Same as intersect above, but write intersections in dst instead of
 * immediately returning success.  We assume that dst has enough space
 * for the whole intersection (e.g., min(one_count, two_count)).
 *
 * HACK HACK HACK: `dst` can be the backing store for `one` or `two`.
 * The linear searches are always ahead of the dst_alloc index, and
 * lower_bound searches are always for needle > any value in the
 * output.  So items may be out of order immediately after dst_alloc,
 * but they're all uniformly < needle, and binary search works.
 */
#define INT_SET_INTERSECTION(W)						\
	static size_t							\
	int_set_intersection_##W(int##W##_t *dst,			\
				 const int_set_t *one, const int_set_t *two) \
	{								\
		const int##W##_t *one_vec;				\
		const int##W##_t *two_vec;				\
		size_t i, j, dst_alloc, nlinear, one_count, two_count;  \
		int64_t one_cursor, two_cursor;				\
									\
		one_count = int_set_count(one);				\
		two_count = int_set_count(two);				\
		nlinear = max(8UL, log2_ceiling(min(one_count, two_count)));	\
									\
		dst_alloc = 0;						\
		i = 0;							\
		j = 0;							\
		one_vec = one->base;					\
		two_vec = two->base;					\
		one_cursor = one_vec[i];				\
		two_cursor = two_vec[j];				\
									\
		for (;;) {						\
			if (one_cursor == two_cursor) {			\
				ACC(one_cursor);			\
			} else if (one_cursor < two_cursor) {		\
				int_set_lower_bound_##W(one,		\
				    two_cursor,				\
				    &i);				\
				ADV_I(0);				\
			} else {					\
				int_set_lower_bound_##W(two,		\
				    one_cursor,				\
				    &j);				\
				ADV_J(0);				\
			}						\
			for (size_t k = 0; k < nlinear; k++) {		\
				if (one_cursor == two_cursor) {		\
					ACC(one_cursor);		\
				} else if (one_cursor < two_cursor) {	\
					ADV_I(1);			\
				} else	{				\
					ADV_J(1);			\
				}					\
			}						\
		}							\
	out:								\
		return dst_alloc;					\
	}

INT_SET_INTERSECTION(64)
INT_SET_INTERSECTION(32)
INT_SET_INTERSECTION(16)

#undef INT_SET_INTERSECTION
#undef ACC
#undef ADV_J
#undef ADV_I

int_set_t *
int_set_intersection(const int_set_t *one, const int_set_t *two)
{
	size_t intersection_size;
	int_set_t *intersection;
	int one_count, two_count;
	int16_t one_size, two_size;

	if ((one == NULL) || (two == NULL)) {
		return NULL;
	}

	one_size = ((binary_tree_t *)one)->size;
	two_size = ((binary_tree_t *)two)->size;
	if (one_size != two_size) {
		debug(3, "Can't create intersection, int widths do not match!\n");
		return NULL;
	}

	one_count = int_set_count(one);
	two_count = int_set_count(two);

	if ((one_count == 0) || (two_count == 0)) {
		return NULL;
	}

	intersection = new_int_set(one->context, one_size, min(one_count, two_count));
	if (intersection == NULL) {
		debug(3, "Failed to allocate result of int_set_intersection\n");
		return NULL;
	}

	assert(one->sorted == true);
	assert(two->sorted == true);

	switch(one_size) {
	case 8: intersection_size = int_set_intersection_64(intersection->base, one, two);
		break;
	case 4: intersection_size = int_set_intersection_32(intersection->base, one, two);
		break;
	case 2: intersection_size = int_set_intersection_16(intersection->base, one, two);
		break;
	default:
		assert_crit(false && "Unexpected int_set size");
		abort();
	}
	assert(intersection_size <= (size_t)(min(one_count, two_count)));
	intersection->num = intersection_size;

	if (btree_resize((binary_tree_t *)intersection) == false) {
		debug(3, "Failed to resize result of int_set_intersection\n");
		return NULL;
	}

	return intersection;
}

void
int_set_intersection_dst(int_set_t *dst, const int_set_t *one, const int_set_t *two)
{
	size_t intersection_size;
	size_t one_count, two_count, bound;
	int16_t one_size, two_size;

	if ((one == NULL) || (two == NULL)) {
		clear_int_set(dst);
		return;
	}

	one_size = ((binary_tree_t *)one)->size;
	two_size = ((binary_tree_t *)two)->size;
	if (one_size != two_size) {
		debug(3, "Can't compute intersection, int widths do not match!\n");
		return;
	}

	one_count = int_set_count(one);
	two_count = int_set_count(two);

	if ((one_count == 0) || (two_count == 0)) {
		clear_int_set(dst);
		return;
	}

	bound = min(one_count, two_count);
	if (dst->max < bound) {
		int_set_postpone_sorting(dst, bound - dst->max);
		int_set_resume_sorting(dst);
	}

	assert(one->sorted == true);
	assert(two->sorted == true);

	switch(one_size) {
	case 8: intersection_size = int_set_intersection_64(dst->base, one, two);
		break;
	case 4: intersection_size = int_set_intersection_32(dst->base, one, two);
		break;
	case 2: intersection_size = int_set_intersection_16(dst->base, one, two);
		break;
	default:
		assert_crit(false && "Unexpected int_set size");
		abort();
	}
	assert(intersection_size <= (size_t)bound);
	dst->num = intersection_size;

	if (btree_resize((binary_tree_t *)dst) == false) {
		debug(3, "Failed to resize result of int_set_intersection_dst\n");
		return;
	}

	return;
}

#define UNION(WIDTH)							\
static void								\
int_set_union_##WIDTH(int_set_t *dst,					\
    const int_set_t *x, const int_set_t *y)				\
{									\
	const int##WIDTH##_t *x_buf, *y_buf;				\
	int##WIDTH##_t *dst_buf;					\
	size_t nx = int_set_count(x);					\
	size_t ny = int_set_count(y);					\
	size_t i, j, k;							\
	size_t dst_n = nx + ny;						\
									\
	assert((size_t)dst->size == sizeof(*dst_buf));			\
	if (dst_n > (size_t)int_set_count(dst)) {			\
		btree_start_bulk_mode(dst, dst_n - int_set_count(dst));	\
	}								\
									\
	if (dst_n == 0) {						\
		k = 0;							\
		goto out;						\
	}								\
									\
	if (nx == 0) {							\
		assert((size_t)y->size == sizeof(*y_buf));		\
		if (dst != y) {						\
			memcpy(dst->base, y->base,			\
			    dst_n * sizeof(*dst_buf));			\
		}							\
									\
		k = ny;							\
		goto out;						\
	}								\
									\
	if (ny == 0) {							\
		assert((size_t)x->size == sizeof(*x_buf));		\
		if (dst != x) {						\
			memcpy(dst->base, x->base,			\
			    dst_n * sizeof(*dst_buf));			\
		}							\
									\
		k = nx;							\
		goto out;						\
	}								\
									\
	assert((size_t)x->size == sizeof(*x_buf));			\
	assert((size_t)y->size == sizeof(*y_buf));			\
									\
	x_buf = x->base;						\
	y_buf = y->base;						\
	dst_buf = dst->base;						\
									\
	if (x_buf == dst_buf) {						\
		memmove(dst_buf + ny, x_buf, nx * sizeof(*dst_buf));	\
		x_buf += ny;						\
	} else if (y_buf == dst_buf) {					\
		memmove(dst_buf + nx, y_buf, ny * sizeof(*dst_buf));	\
		y_buf += nx;						\
	}								\
									\
	for (i = 0, j = 0, k = 0; i < nx && j < ny; ) {			\
		int##WIDTH##_t xi = x_buf[i], yj = y_buf[j];		\
		int##WIDTH##_t min = (xi < yj) ? xi : yj;		\
									\
		/* gcc 4.4 won't emit cmp/cmov/setcc/setcc. */		\
		i += !!(xi == min);					\
		j += !!(yj == min);					\
									\
		dst_buf[k++] = min;					\
	}								\
									\
	if (i < nx) {							\
		size_t remainder = nx - i;				\
									\
		memmove(dst_buf + k, x_buf + i,				\
		    remainder * sizeof(*dst_buf));			\
		k += remainder;						\
	} else if (j < ny) {						\
		size_t remainder = ny - j;				\
									\
		memmove(dst_buf + k, y_buf + j,				\
		    remainder * sizeof(*dst_buf));			\
		k += remainder;						\
	}								\
									\
out:									\
	dst->num = k;							\
	dst->bulk_mode = false;						\
	return;								\
}

UNION(16);
UNION(32);
UNION(64);

#undef UNION

int_set_t *
int_set_union(const int_set_t *one, const int_set_t *two)
{
	int16_t size = 0;
	int_set_t *dst;

	if (one == NULL && two == NULL) {
		return NULL;
	}

	if (one == two) {
		return copy_int_set(one);
	}

	if (one != NULL) {
		size = one->size;
	}

	if (two != NULL) {
		if (size != 0) {
			assert(two->size == size &&
			    "Can't take union with mismatched int set sizes");
		}

		size = two->size;
	}

	if (one != NULL) {
		dst = new_int_set(one->context, size, 0);
	} else {
		dst = new_int_set(two->context, size, 0);
	}

	assert(one == NULL || one->sorted == true);
	assert(two == NULL || two->sorted == true);

	switch(size) {
	case 2:
		int_set_union_16(dst, one, two);
		return dst;
	case 4:
		int_set_union_32(dst, one, two);
		return dst;
	case 8:
		int_set_union_64(dst, one, two);
		return dst;
	default:
		assert_crit(false && "Unexpected int_set size");
		abort();
	}
}

void
int_set_union_dst(int_set_t *dst, const int_set_t *one, const int_set_t *two)
{

	assert(one == NULL || one->sorted == true);
	assert(two == NULL || two->sorted == true);

	switch(dst->size) {
	case 2:
		int_set_union_16(dst, one, two);
		return;
	case 4:
		int_set_union_32(dst, one, two);
		return;
	case 8:
		int_set_union_64(dst, one, two);
		return;
	default:
		assert_crit(false && "Unexpected int_set size");
		abort();
	}
}

void
int_set_append_int_set(int_set_t **dst, const int_set_t *src)
{

	if (*dst == NULL) {
		*dst = copy_int_set(src);
		return;
	}

	int_set_union_dst(*dst, *dst, src);
	return;
}

struct int_set_union_record {
	const int_set_t *set;
	uint32_t size;
	bool fresh;
};

/*
 * records[0, 1] point to the two smallest int sets.
 */
static void
int_set_union_find_min(struct int_set_union_record *records, size_t nsrc)
{
#define SWAP(I, J) do {					\
		struct int_set_union_record tmp;	\
							\
		tmp = records[I];			\
		records[I] = records[J];		\
		records[I] = tmp;			\
	} while (0)

	if (nsrc < 2) {
		return;
	}

	if (records[0].size > records[1].size) {
		SWAP(0, 1);
	}

	for (size_t i = 2; i < nsrc; i++) {
		if (records[i].size < records[1].size) {
			SWAP(i, 1);
			if (records[0].size > records[1].size) {
				SWAP(0, 1);
			}
		}
	}

#undef SWAP
}

static void
int_set_union_pair(struct int_set_union_record *x, struct int_set_union_record *y)
{
	const int_set_t *one = x->set, *two = y->set;
	int_set_t *dst = NULL;

	if (x->fresh == true) {
		dst = (int_set_t *)one;
	} else if (y->fresh == true) {
		dst = (int_set_t *)two;
	}

	if (dst == NULL) {
		dst = int_set_union(one, two);
	} else {
		int_set_union_dst(dst, one, two);
	}

	if (x->fresh == true && one != dst) {
		free_int_set((int_set_t *)one);
	}

	if (y->fresh == true && two != dst) {
		free_int_set((int_set_t *)two);
	}

	x->set = dst;
	x->size = int_set_count(dst);
	x->fresh = true;
	return;
}

/*
 * Attempt to minimise runtime by pairing sets in balanced merges.
 */
int_set_t *
int_set_union_all(const int_set_t *src[], size_t nsrc)
{
	struct int_set_union_record *records;
	int_set_t *ret;
	size_t n;

	if (nsrc == 0) {
		return NULL;
	}

	if (nsrc == 1) {
		assert(src[0] == NULL || src[0]->sorted == true);
		return copy_int_set(src[0]);
	}

	records = calloc(nsrc, sizeof(*records));
	n = 0;
	for (size_t i = 0; i < nsrc; i++) {
		if (int_set_count(src[i]) > 0) {
			assert(src[i]->sorted == true);
			records[n].set = src[i];
			records[n].size = int_set_count(src[i]);
			records[n].fresh = false;
			n++;
		}
	}

	if (n == 0) {
		ret = NULL;
		goto out;
	}

	while (n > 1) {
		int_set_union_find_min(records, n);
		int_set_union_pair(records, records + 1);
		records[1] = records[--n];
	}

	ret = (int_set_t *)records[0].set;
	if (records[0].fresh == false) {
		ret = copy_int_set(ret);
	}

out:
	free(records);
	return ret;
}

static int_set_t *
int_set_union_array16(int_set_t *set, int16_t *array, size_t array_count)
{

	if (array == NULL || array_count == 0) {
		return copy_int_set(set);
	}

	size_t set_count = int_set_count(set);
	size_t new_set_count = array_count + set_count;
	size_t new_set_size = sizeof(int16_t);
	const btree_context_t *context = set != NULL ? set->context : NULL;

	int_set_t *new_set = new_int_set(context, new_set_size, new_set_count);

	int_set_postpone_sorting(new_set, new_set_count);
	an_qsort_int16(array, array_count);

	size_t set_index = 0;
	size_t array_index = 0;
	for(; set_index < set_count && array_index < array_count; ++set_index) {
		int16_t set_val = int_set_index(set, set_index);
		add_int_to_set(new_set, set_val);
		if (set_val == array[array_index]) {
			array_index++;
			continue;
		}
		if (set_val > array[array_index]) {
			for(; array_index < array_count; ++array_index) {
				if (set_val == array[array_index]) {
					array_index++;
					break;
				} else if(set_val < array[array_index]) {
					break;
				}
				if ((array_index==0) || (array[array_index] != array[array_index-1])) {
					add_int_to_set(new_set,array[array_index]);
				}
			}
		}
	}
	if (set_index < set_count) {
		for (; set_index < set_count; ++set_index) {
			add_int_to_set(new_set, int_set_index(set, set_index));
		}
	} else if (array_index < array_count) {
		for (; array_index < array_count; ++array_index) {
			if ((array_index==0) || (array[array_index] != array[array_index-1])) {
				add_int_to_set(new_set, array[array_index]);
			}
		}
	}
	int_set_resume_sorting(new_set);
	return new_set;
}

static int_set_t *
int_set_union_array32(int_set_t *set, int32_t *array, size_t array_count)
{

	if (array == NULL || array_count == 0) {
		return copy_int_set(set);
	}

	size_t set_count = int_set_count(set);
	size_t new_set_count = array_count + set_count;
	size_t new_set_size = sizeof(int32_t);
	const btree_context_t *context = set != NULL ? set->context : NULL;

	int_set_t *new_set = new_int_set(context, new_set_size, new_set_count);

	int_set_postpone_sorting(new_set, new_set_count);
	an_qsort_int32(array, array_count);

	size_t set_index = 0;
	size_t array_index = 0;
	for(; set_index < set_count && array_index < array_count; ++set_index) {
		int32_t set_val = int_set_index(set, set_index);
		add_int_to_set(new_set, set_val);
		if (set_val == array[array_index]) {
			array_index++;
			continue;
		}
		if (set_val > array[array_index]) {
			for(; array_index < array_count; ++array_index) {
				if (set_val == array[array_index]) {
					array_index++;
					break;
				} else if(set_val < array[array_index]) {
					break;
				}
				if ((array_index==0) || (array[array_index] != array[array_index-1])) {
					add_int_to_set(new_set,array[array_index]);
				}
			}
		}
	}
	if (set_index < set_count) {
		for (; set_index < set_count; ++set_index) {
			add_int_to_set(new_set, int_set_index(set, set_index));
		}
	} else if (array_index < array_count) {
		for (; array_index < array_count; ++array_index) {
			if ((array_index==0) || (array[array_index] != array[array_index-1])) {
				add_int_to_set(new_set, array[array_index]);
			}
		}
	}
	int_set_resume_sorting(new_set);
	return new_set;
}

static int_set_t *
int_set_union_array64(int_set_t *set, int64_t *array, size_t array_count)
{

	if (array == NULL || array_count == 0) {
		return copy_int_set(set);
	}

	size_t set_count = int_set_count(set);
	size_t new_set_count = array_count + set_count;
	size_t new_set_size = sizeof(int64_t);
	const btree_context_t *context = set != NULL ? set->context : NULL;

	int_set_t *new_set = new_int_set(context, new_set_size, new_set_count);

	int_set_postpone_sorting(new_set, new_set_count);
	an_qsort_int64(array, array_count);

	size_t set_index = 0;
	size_t array_index = 0;
	for(; set_index < set_count && array_index < array_count; ++set_index) {
		int64_t set_val = int_set_index(set, set_index);
		add_int_to_set(new_set, set_val);
		if (set_val == array[array_index]) {
			array_index++;
			continue;
		}
		if (set_val > array[array_index]) {
			for(; array_index < array_count; ++array_index) {
				if (set_val == array[array_index]) {
					array_index++;
					break;
				} else if(set_val < array[array_index]) {
					break;
				}
				if ((array_index==0) || (array[array_index] != array[array_index-1])) {
					add_int_to_set(new_set,array[array_index]);
				}

			}
		}
	}
	if (set_index < set_count) {
		for (; set_index < set_count; ++set_index) {
			add_int_to_set(new_set, int_set_index(set, set_index));
		}
	} else if (array_index < array_count) {
		for (; array_index < array_count; ++array_index) {
			if ((array_index==0) || (array[array_index] != array[array_index-1])) {
				add_int_to_set(new_set, array[array_index]);
			}
		}
	}
	int_set_resume_sorting(new_set);
	return new_set;
}

int_set_t *
int_set_union_array(int_set_t *set, int64_t *array, size_t array_count, size_t array_size)
{
	size_t set_size = array_size;

	if (set) {
		set_size = (((binary_tree_t*) set)->size > array_size) ? ((binary_tree_t*)set)->size : array_size;
	}

	if (set_size == 2) {
		return int_set_union_array16(set, (int16_t*)array, array_count);
	} else if (set_size == 4) {
		return int_set_union_array32(set, (int32_t*)array, array_count);
	} else {
		return int_set_union_array64(set, (int64_t*)array, array_count);
	}
}

pair_int_t *
pair_int_set_index(pair_int_set_t *set, size_t idx)
{
	assert_dev(set);
	assert_dev(idx < pair_int_set_count(set));
	return btree_lookup_index(set, idx);
}


pair_int_t *
pair_int_set_lookup(pair_int_set_t *set, int a, int b)
{
	pair_int_t search = {a, b};
	return btree_lookup(set, &search);
}

void
bappend_int_set(bstring b, int_set_t *set)
{
	char buffer[32];
	bool first = true;

	INT_SET_FOREACH(set, x) {
		if (first == false) {
			bconchar(b, ',');
		}

		first = false;
		modp_itoa10(x, buffer);
		bcatcstr(b, buffer);
	}
}

void
evbuffer_append_int_set(struct evbuffer *buf, const int_set_t *set)
{
	bool first = true;

	INT_SET_FOREACH(set, x) {
		check_and_print_comma(first, buf);
		evbuffer_add_printf(buf, "%ld", x);
	}
}

void
append_json_intset(struct evbuffer *out, const char *name, const int_set_t *intset, int comma)
{

	append_pre(out, name, comma);

	evbuffer_add(out, "[", 1);
	evbuffer_append_int_set(out, intset);
	evbuffer_add(out, "]", 1);

	append_post(out, comma);
}
