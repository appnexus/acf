#ifndef _COMMON_AN_ARRAY_H
#define _COMMON_AN_ARRAY_H

#include <acf/an_array.h>
#include <stdbool.h>
#include <stddef.h>

#include "common/an_cc.h"
#include "common/an_swlock.h"
#include "common/an_sstm.h"
#include "common/common_types.h"
#include "common/util.h"
#include "common/int_set.h"

#define AN_ARRAY_SHUFFLE(name, array) \
	an_array_shuffle_##name(array)

#define AN_ARRAY_DEFINE_COPY_INT_SET(name) 						\
AN_CC_UNUSED static int                    						\
an_array_copy_int_set_##name(struct an_array_##name *array, const int_set_t *is)        \
{                                							\
	size_t num, size;                 	   					\
	num = int_set_count(is);                					\
	if (num == 0) {                        						\
		return -1;     		               					\
	}                            							\
											\
	size = is->size;                    						\
	assert(sizeof(array->values[0]) == size);            				\
	if (an_array_initialized_##name(array) == true) {    				\
		an_array_reset_##name(array);            				\
		an_array_resize_##name(array, num);        				\
	} else {                        						\
		an_array_init_##name(array, num);        				\
	}                            							\
											\
	memcpy(array->values, is->base, is->size * num);    				\
	array->n_entries = num;                    					\
	return 0;                        						\
}

#define AN_ARRAY_COPY_INT_SET(name, array, is) \
	an_array_copy_int_set_##name(array, is)

#define AN_ARRAY_DEFINE_SHUFFLE(name) 				\
AN_CC_UNUSED static void 					\
an_array_shuffle_##name(struct an_array_##name *array) 		\
{ 								\
	an_random_shuffle(array->values, array->n_entries); 	\
	return; 						\
}

void an_array_register_allocator(void);

struct an_array {
	void **values;
	unsigned int capacity;
	unsigned int n_entries;
	an_swlock_t lock;
};
typedef struct an_array an_array_t;

DEFINE_SSTM_TYPE(sstm_an_array, an_array_t);

typedef struct sstm_an_array sstm_an_array_t;

extern struct an_sstm_ops an_array_ops;

DEFINE_SSTM_WRITE(an_array_sstm_write, sstm_an_array_t, an_array_ops);

typedef void an_array_map_t(void *);

static inline void *
an_array_buffer(const struct an_array *array, unsigned int *n_entries)
{

	*n_entries = array->n_entries;
	return array->values;
}

static inline void
an_array_write_lock(struct an_array *array)
{

	an_swlock_write_lock(&array->lock);
	return;
}

static inline void
an_array_write_unlock(struct an_array *array)
{

	an_swlock_write_unlock(&array->lock);
	return;
}

static inline void
an_array_read_lock(struct an_array *array)
{

	an_swlock_read_lock(&array->lock);
	return;
}

static inline void
an_array_read_unlock(struct an_array *array)
{

	an_swlock_read_unlock(&array->lock);
	return;
}

an_array_t *an_array_create(unsigned int);
sstm_an_array_t *sstm_an_array_create(unsigned int);
void an_array_defer(an_array_t *);
void an_array_destroy(an_array_t *);
void sstm_an_array_destroy(sstm_an_array_t *);
void an_array_init(an_array_t *, unsigned int);
void an_array_deinit(an_array_t *);
void an_array_duplicate(an_array_t *);
void an_array_resize(an_array_t *, unsigned int);
void an_array_grow_to(an_array_t *, unsigned int, void *fill);
void an_array_squash(an_array_t *);

/**
 * Attempts to squash an array if necessary and if write lock
 * can be acquired in a wait-free manner.
 */
void an_array_trysquash(an_array_t *);

void an_array_map(an_array_t *, an_array_map_t *);
void an_array_shuffle(an_array_t *);

/**
 * Find a 64 bit element val in an array of cnt 64 bit items
 */
static inline void
an_array_reset(an_array_t *array)
{

	array->n_entries = 0;
	return;
}

static inline void *
an_array_pop(an_array_t *array, unsigned int *entries)
{

	if (entries != NULL)
		*entries = array->n_entries;

	if (array->n_entries == 0)
		return NULL;

	return array->values[--array->n_entries];
}

static inline void
an_array_push(an_array_t *array, void *value)
{

	if (array->n_entries == array->capacity) {
		size_t new_size = max(array->capacity << 1,
				      array->n_entries + 1);

		an_array_resize(array, new_size);
	}

	array->values[array->n_entries++] = value;
	return;
}

static inline void *
an_array_peek(an_array_t *array, unsigned int *entries)
{

	if (entries != NULL)
		*entries = array->n_entries;

	if (array->n_entries == 0)
		return NULL;

	return array->values[array->n_entries - 1];
}

static inline void *
an_array_value(an_array_t *array, unsigned int i)
{

	return array->values[i];
}

static inline void
an_array_swap(an_array_t *array, unsigned int i, unsigned int j)
{
	void *temp = array->values[i];

	array->values[i] = array->values[j];
	array->values[j] = temp;
	return;
}

static inline bool
an_array_initialized(const an_array_t *array)
{

	return (array->values != NULL);
}

static inline unsigned int
an_array_length(const an_array_t *array)
{

	return array->n_entries;
}

static inline void
an_array_remove_index(an_array_t *array, unsigned int i)
{

	array->values[i] = array->values[--array->n_entries];
	return;
}

static inline bool
an_array_remove(an_array_t *array, void *entry)
{
	size_t length = an_array_length(array);
	unsigned int i;

	for (i = 0; i < length; i++) {
		if (array->values[i] != entry)
			continue;

		array->values[i] = array->values[--array->n_entries];
		return true;
	}

	return false;
}

static inline bool
an_array_remove_mask(an_array_t *array, void *entry, uintptr_t mask)
{
	uintptr_t match = (uintptr_t)entry & mask;
	size_t length = an_array_length(array);
	size_t i;

	for (i = 0; i < length; i++) {
		uintptr_t target = (uintptr_t)array->values[i] & mask;

		if (target != match) {
			continue;
		}

		array->values[i] = array->values[--array->n_entries];
		return true;
	}

	return false;
}

static inline bool
an_array_member_mask(const an_array_t *array, const void *entry, uintptr_t mask)
{
	uintptr_t match = (uintptr_t)entry & mask;
	size_t length = an_array_length(array);
	size_t i;

	for (i = 0; i < length; i++) {
		uintptr_t target = (uintptr_t)array->values[i] & mask;
		if (target == match) {
			return true;
		}
	}

	return false;
}

static inline bool
an_array_member(const an_array_t *array, const void *entry)
{
	size_t length = an_array_length(array);
	unsigned int i;

	for (i = 0; i < length; i++) {
		if (array->values[i] == entry)
			return true;
	}

	return false;
}

static inline void
an_array_sort(an_array_t *array, int (*compar)(const void *, const void *))
{
	size_t length = an_array_length(array);

	if (length > 0) {
		qsort(array->values, length, sizeof(void *), compar);
	}

	return;
}
#endif /* _COMMON_AN_ARRAY_H */
