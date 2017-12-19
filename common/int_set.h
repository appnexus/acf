#ifndef _COMMON_INT_SET_H
#define _COMMON_INT_SET_H

#include <assert.h>
#include <evhttp.h>
#include <stdbool.h>

#include <an_message/an_bstrlib.h>

#include "common/an_cc.h"
#include "common/an_md.h"
#include "common/an_sstm.h"
#include "common/btree.h"
#include "common/util.h"

typedef struct pair_int {
	int a;
	int b;
} pair_int_t;

typedef struct binary_tree int_set_t;
typedef struct sstm_binary_tree sstm_int_set_t;
typedef struct binary_tree pair_int_set_t;
typedef struct sstm_binary_tree sstm_pair_int_set_t;

extern struct an_sstm_ops int_set_ops;

DEFINE_SSTM_WRITE(int_set_sstm_write, sstm_int_set_t, int_set_ops);

/**
 * initialize the int_set subsystem
 */
void int_set_initialize(void);

/**
 * deinitialize the int_set subsystem
 */
void int_set_cleanup(void);

/**
 * In-place initialization of an integer set.
 *
 * @param set	Pointer to a region of memory reserved for
 *		an integer set.
 * @param ctx 	Pointer to an_malloc_group associated with
 * 		allocation. May be NULL.
 * @param element_size Size of integers to be stored, in bytes.
 * @param initial_size Initial number of elements to allocate space for.
 * @return Returns true on success and false on failure.
 */
bool int_set_init(int_set_t *set, const btree_context_t *ctx,
    size_t element_size, size_t initial_size);

/**
 * Destroy inlined integer set object.
 */
void int_set_deinit(int_set_t *set);

bool int_set_intersect(const int_set_t *set1, const int_set_t *set2);
int_set_t *int_set_intersection(const int_set_t *one, const int_set_t *two);
/**
 * @brief Fill dst with the intersection of int_set one and two.
 * @param dst the destination int_set; may be the same as one or two.
 */
void int_set_intersection_dst(int_set_t *dst, const int_set_t *one, const int_set_t *two);
int_set_t *int_set_union(const int_set_t *one, const int_set_t *two);
/**
 * @brief Fill dst with the union of int_set one and two.
 * @param dst the destination int_set; may be the same as one or two.
 */
void int_set_union_dst(int_set_t *dst, const int_set_t *one, const int_set_t *two);
void int_set_postpone_sorting(int_set_t *set, int num_new_elements);
/*
 * Sometimes we add a sorted array to an empty int_set and we don't have to resort when it's done, just enable sorting
 */
void int_set_resume_sorting(int_set_t *set);
void bappend_int_set(bstring b, int_set_t *set);
void evbuffer_append_int_set(struct evbuffer *buf, const int_set_t *set);
int_set_t *new_int_set(const btree_context_t *ctx, size_t int_len, size_t initial_size);
sstm_int_set_t *new_sstm_int_set(const btree_context_t *ctx, size_t int_len, size_t initial_size);
void free_int_set(int_set_t *set);
void free_sstm_int_set(sstm_int_set_t *set);
void add_int_to_set(int_set_t *set, int64_t val);
bool remove_int_from_set(int_set_t *set, int64_t val);
int_set_t *int_set_union_array(int_set_t *set, int64_t *array,
    size_t array_count, size_t array_size);
void clear_int_set(int_set_t *set);
bool int_set_contains_16(const int_set_t *, int16_t);
bool int_set_contains_32(const int_set_t *, int32_t);
bool int_set_contains_64(const int_set_t *, int64_t);
void int_set_defer(void *set);

/**
 * @brief Add an int set to another int set.  If the destination is
 * NULL, it will be created.  The destination int type must be the same as the source.
 * @param src set to insert
 * @param dst pointer to the INOUT union set.
 */
void int_set_append_int_set(int_set_t **dst, const int_set_t *src);

int_set_t *int_set_union_all(const int_set_t *src[], size_t nsrc);

/**
 * @brief Add an integer to an int set. If the set does not exist, it will be created with
 * the specified initial capacity.
 *
 * @param set Set to which to add int
 * @param ctx btree context
 * @param add int to add to the set
 * @param int_len the size of the integer being added
 * @param initial_capactiy capacity of the int set (to prevent need to grow dynamically)
 */
void add_int_to_set_init_size(int_set_t **set, const btree_context_t *ctx, int64_t add, size_t int_len, size_t initial_capacity);


/**
 * @brief Add an integer to an int set. If the set does not exist, it will be
 * created with the default init size.
 * @param set Set to which to add int
 * @param ctx btree context
 * @param add int to add to the set
 * @param int_len the size of the integer being added
 */
void add_int_to_set_init(int_set_t **set, const btree_context_t *ctx, int64_t add, size_t int_len);

/**
 * @brief SSTM variant of add_int_to_set_init
 */
void add_int_to_sstm_set_init(sstm_int_set_t **set, const btree_context_t *ctx, int64_t add, size_t int_len);

/**
 * @brief Remove an integer from an int set. If the set empties, then free it
 *
 * @param set Set from which to remove int
 * @param remove int to remove from the set
 * @return true if an element is removed, false otherwise
 */
void remove_int_from_set_deinit(int_set_t **set, int64_t remove);

/**
 * @brief SSTM variant of remove_int_from_set_deinit
 */
bool remove_int_from_sstm_set_deinit(sstm_int_set_t **set, int64_t remove);

static inline size_t
int_set_count(const int_set_t *set)
{

	return btree_item_count(set);
}

static inline int_set_t *
copy_int_set(const int_set_t *set)
{

	return btree_copy(set);
}

static inline sstm_int_set_t *
copy_sstm_int_set(const int_set_t *set)
{

	return btree_sstm_copy(set);
}

static inline sstm_int_set_t *
convert_sstm_int_set(int_set_t *set)
{
	sstm_int_set_t *ret;

	ret = copy_sstm_int_set(set);
	free_int_set(set);
	return ret;
}

static inline bool
int_set_is_empty(const int_set_t *set)
{
	return set == NULL || set->num == 0;
}

static inline int16_t
int_set_index_16(const int_set_t *array, size_t index)
{
	int16_t *r = array->base;

	return r[index];
}

static inline int32_t
int_set_index_32(const int_set_t *array, size_t index)
{
	int32_t *r = array->base;

	return r[index];
}

static inline int64_t
int_set_index_64(const int_set_t *array, size_t index)
{
	int64_t *r = array->base;

	return r[index];
}

/**
 * If there are many calls to int_set_index in the same compilation
 * unit, this might help the compiler keep the slow path out of line.
 */
AN_CC_UNUSED static int64_t
int_set_index_64_16(const int_set_t *array, size_t index)
{

	if (array->size == sizeof(int64_t)) {
		return int_set_index_64(array, index);
	}

	return int_set_index_16(array, index);
}

static inline int64_t
int_set_index(const int_set_t *array, size_t index)
{

	if (AN_CC_LIKELY(array->size == sizeof(int32_t))) {
		return int_set_index_32(array, index);
	}

	return int_set_index_64_16(array, index);
}

AN_CC_UNUSED static bool
int_set_contains_64_16(const int_set_t *array, int64_t val)
{

	if (array->size == sizeof(int64_t)) {
		return int_set_contains_64(array, val);
	}

	return int_set_contains_16(array, val);
}

static inline bool
int_set_contains(const int_set_t *array, int64_t val)
{

	if (array == NULL) {
		return false;
	}

	if (AN_CC_LIKELY(array->size == sizeof(int32_t))) {
		return int_set_contains_32(array, val);
	}

	return int_set_contains_64_16(array, val);
}

#define INT_SET_BASE_GET(W)					\
	static inline const int##W##_t*				\
	int_set_base_get_##W(const int_set_t *array)		\
	{							\
								\
		assert(array->size == sizeof(int##W##_t));	\
		return array->base;				\
	}

INT_SET_BASE_GET(16)
INT_SET_BASE_GET(32)
INT_SET_BASE_GET(64)

#undef INT_SET_BASE_GET

pair_int_set_t *new_pair_int_set(const btree_context_t *ctx, size_t initial_size);
sstm_pair_int_set_t *new_sstm_pair_int_set(const btree_context_t *ctx, size_t initial_size);

void free_pair_int_set(pair_int_set_t *set);
void free_sstm_pair_int_set(sstm_pair_int_set_t *set);

void add_pair_int_to_set(pair_int_set_t *set, int a, int b);
void remove_pair_int_from_set(pair_int_set_t *set, int a, int b);

static inline size_t
pair_int_set_count(pair_int_set_t *set)
{

	return btree_item_count(set);
}

pair_int_t* pair_int_set_index(pair_int_set_t *set, size_t index);
pair_int_t* pair_int_set_lookup(pair_int_set_t* set, int a, int b);

/**
 * int_set: printed as an array of comma-seperated values ([1, 2, 3]).
 */
void append_json_intset(struct evbuffer* out, const char* name, const int_set_t* intset, int comma);

#define INT_SET_FOREACH(int_set, cursor)				\
	for (int64_t _i = 0, _n = int_set_count(int_set), cursor = 0;	\
	     _i < _n && ((cursor = int_set_index(int_set, _i)), 1);	\
	     _i++)

#endif /* _COMMON_INT_SET_H */
