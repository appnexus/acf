#ifndef AN_ARRAY_H
#define AN_ARRAY_H

#include "an_cc.h"

AN_EXTERN_C_BEGIN

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "acf_export.h"
#include "an_allocator.h"

extern const struct an_allocator *an_array_allocator;

/*
 * Not thread safe
 */
ACF_EXPORT void an_array_set_allocator(const struct an_allocator *allocator);

#define AN_ARRAY_INSTANCE(name) \
	struct an_array_##name

#define AN_ARRAY_INIT(name, array, capacity) \
	an_array_init_##name(array, capacity)

#define AN_ARRAY_RESIZE(name, array, length) \
	an_array_resize_##name(array, length)

#define AN_ARRAY_GROW_TO(name, array, length, fill) \
	an_array_grow_to_##name(array, length, fill)

#define AN_ARRAY_CREATE(name, capacity) \
	an_array_create_##name(capacity)

#define AN_ARRAY_DEINIT(name, array) \
	an_array_deinit_##name(array)

#define AN_ARRAY_DESTROY(name, array) \
	an_array_destroy_##name(array)

#define AN_ARRAY_DUPLICATE(name, array) \
	an_array_duplicate_##name(array)

#define AN_ARRAY_MAP(name, array, f) \
	an_array_map_##name(array, f)

#define AN_ARRAY_PEEK(name, array, entries) \
	an_array_peek_##name(array, entries)

#define AN_ARRAY_VALUE(name, array, index) \
	an_array_value_##name(array, index)

#define AN_ARRAY_VALUE_CONST(name, array, index) \
	an_array_value_const_##name(array, index)

#define AN_ARRAY_LENGTH(name, array) \
	an_array_length_##name(array)

#define AN_ARRAY_SHRINK(name, array) \
	an_array_shrink_##name(array)

#define AN_ARRAY_INITIALIZED(name, array) \
	an_array_initialized_##name(array)

#define AN_ARRAY_SWAP(name, array, i, j) \
	an_array_swap_##name(array, i, j)

#define AN_ARRAY_RESET(name, array) \
	an_array_reset_##name(array)

#define AN_ARRAY_BUFFER(name, array, entries) \
	an_array_buffer_##name(array, entries)

#define AN_ARRAY_TRUNCATE_TO_SIZE(name, array, size) \
	an_array_truncate_to_size_##name(array, size)

#define AN_ARRAY_POP(name, array, entries) \
	an_array_pop_##name(array, entries)

#define AN_ARRAY_PUSH(name, array, value) \
	an_array_push_##name(array, value)

#define AN_ARRAY_SORT(name, array, compar) \
	an_array_sort_##name(array, AN_CC_CAST_COMPARATOR(compar, __typeof__(*(array)->values)))

#define AN_ARRAY_CLONE_AN_ARRAY(dstname, dst, dsttype, src, srctype)	\
	do {								\
		size_t num, size;					\
		num = (src)->n_entries;					\
		if (num == 0) {						\
			break;						\
		}							\
									\
		size = sizeof(srctype);					\
		assert(sizeof(dsttype) == size);			\
		if (an_array_initialized_##dstname(dst) == true) {	\
			an_array_reset_##dstname(dst);			\
			an_array_resize_##dstname(dst, num);		\
		} else {						\
			an_array_init_##dstname(dst, num);		\
		}							\
									\
		memcpy((dst)->values, (src)->values, size * num);	\
		(dst)->n_entries = num;					\
	} while (0);

#define AN_ARRAY_REMOVE_INDEX_IN_ORDER(name, array, i) \
	an_array_remove_index_in_order_##name(array, i)

#define AN_ARRAY_REMOVE_INDEX(name, array, i) \
	an_array_remove_index_##name(array, i)

/*
 * Additional operations available on primitive arrays defined with AN_ARRAY_PRIMITIVE()
 */
#define AN_ARRAY_REMOVE(name, array, value) \
	an_array_remove_##name(array, value)

#define AN_ARRAY_REMOVE_IN_ORDER(name, array, value) \
	an_array_remove_in_order_##name(array, value)

#define AN_ARRAY_MEMBER(name, array, value) \
	an_array_member_##name(array, value)

#define AN_ARRAY_PUSH_UNIQUE(name, array, value) \
	an_array_push_unique_##name(array, value)

#define AN_ARRAY(type, name) \
	AN_ARRAY_INTERNAL(struct type, name)

#define AN_ARRAY_PRIMITIVE(type, name)					\
	AN_ARRAY_INTERNAL(type, name)					\
									\
	AN_CC_UNUSED static inline bool					\
	an_array_remove_##name(struct an_array_##name *array, type const *value) \
	{								\
		size_t length = an_array_length_##name(array);		\
		size_t i;						\
									\
		for (i = 0; i < length; i++) {				\
			if (array->values[i] != *value) {		\
				continue;				\
			}						\
									\
			array->values[i] = array->values[--array->n_entries]; \
			return true;					\
		}							\
									\
		return false;						\
	}								\
									\
	AN_CC_UNUSED static inline bool					\
	an_array_remove_in_order_##name(struct an_array_##name *array,	\
	    type const *value)						\
	{								\
		size_t length = an_array_length_##name(array);		\
		size_t i;						\
									\
		for (i = 0; i < length; i++) {				\
			if (array->values[i] != *value) {		\
				continue;				\
			}						\
									\
			an_array_remove_index_in_order_##name(array, i);	\
			return true;					\
		}							\
									\
		return false;						\
	}								\
									\
	AN_CC_UNUSED static inline bool					\
	an_array_member_##name(struct an_array_##name *array, type const *value) \
	{								\
		size_t length = an_array_length_##name(array);		\
		size_t i;						\
									\
		for (i = 0; i < length; i++) {				\
			if (array->values[i] == *value) {		\
				return true;				\
			}						\
		}							\
									\
		return false;						\
	}								\
									\
	AN_CC_UNUSED static inline void					\
	an_array_push_unique_##name(struct an_array_##name *array, type const *value) \
	{								\
		size_t length = an_array_length_##name(array);		\
		size_t i;						\
									\
		for (i = 0; i < length; i++) {				\
			if (array->values[i] == *value) {		\
				return;					\
			}						\
		}							\
									\
		an_array_push_##name(array, value);			\
		return;							\
	}

#define AN_ARRAY_INTERNAL(type, name)					\
	struct an_array_##name {					\
		type *values;						\
		unsigned int capacity;					\
		unsigned int n_entries;					\
	};								\
									\
	AN_CC_UNUSED static void					\
	an_array_resize_##name(struct an_array_##name *array,		\
	    unsigned int length)					\
	{								\
		type *values;						\
		unsigned int allocated_length = (length > 1U) ? length : 1U;	\
									\
		values = (type *)AN_REALLOC(an_array_allocator,		\
		    array->values,					\
		    sizeof(type) * array->capacity,			\
		    sizeof(type) * allocated_length);			\
									\
		array->values = values;					\
		array->capacity = allocated_length;			\
		if (length < array->n_entries) { 			\
			array->n_entries = length; 			\
		} 							\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_array_grow_to_##name(struct an_array_##name *array,		\
	    unsigned int goal, type const *fill)			\
	{								\
		type *values = array->values;				\
		unsigned int new_capacity = array->capacity;		\
									\
		if (goal <= array->n_entries) {				\
			return;						\
		}							\
									\
		if (new_capacity < 2) {					\
			new_capacity = 2;				\
		}							\
									\
		while (new_capacity < goal) {				\
			if (new_capacity < UINT_MAX / 2) {		\
				new_capacity *= 2;			\
			} else {					\
				new_capacity = UINT_MAX;		\
			}						\
		}							\
									\
		if (new_capacity > array->capacity) {			\
			values = (type *)AN_REALLOC(an_array_allocator,	\
			    array->values,				\
			    sizeof(type) * (size_t)array->capacity,	\
			    sizeof(type) * (size_t)new_capacity);	\
			array->values = values;				\
			array->capacity = new_capacity;			\
		}							\
									\
		if (fill == NULL) {					\
			memset(values + array->n_entries, 0,		\
			    sizeof(type) * (size_t)(goal - array->n_entries)); \
		} else {						\
			for (unsigned int i = array->n_entries; i < goal; i++) { \
				values[i] = *fill;			\
			}						\
		}							\
									\
		array->n_entries = goal;				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_array_init_##name(struct an_array_##name *array,		\
			     unsigned int capacity)			\
	{								\
									\
		array->values = (type *)AN_MALLOC(an_array_allocator,	\
				    sizeof(type) * capacity);		\
									\
		array->capacity = capacity;				\
		array->n_entries = 0;					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static struct an_array_##name *			\
	an_array_create_##name(unsigned int capacity)			\
	{								\
		struct an_array_##name *r;				\
									\
		r = (struct an_array_##name *)AN_MALLOC(an_array_allocator, sizeof(*r));		\
									\
		an_array_init_##name(r, capacity);			\
		return r;						\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_array_deinit_##name(struct an_array_##name *array)		\
	{								\
									\
		AN_FREE(an_array_allocator, array->values);		\
		memset(array, 0, sizeof(struct an_array_##name));	\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_array_destroy_##name(struct an_array_##name *array)		\
	{								\
									\
		if (array == NULL) {					\
			return;						\
		}							\
									\
		an_array_deinit_##name(array);				\
		AN_FREE(an_array_allocator, array);			\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_array_duplicate_##name(struct an_array_##name *array)	\
	{								\
		void *tmp;						\
									\
		if (array == NULL || array->values == NULL) {		\
			return;						\
		}							\
									\
		tmp = AN_MALLOC(an_array_allocator, array->capacity * sizeof(type)); \
		memcpy(tmp, array->values, array->capacity * sizeof(type)); \
		array->values = (type *)tmp;				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_array_map_##name(struct an_array_##name *array,		\
	    void (*f)(type *))						\
	{								\
		unsigned int i;						\
									\
		for (i = 0; i < array->n_entries; i++)			\
			f(&array->values[i]);				\
									\
		return;							\
	}								\
									\
	AN_CC_UNUSED static inline type *				\
	an_array_peek_##name(struct an_array_##name *array,		\
			     unsigned int *entries)			\
	{								\
									\
		if (entries != NULL)					\
			*entries = array->n_entries;			\
									\
		if (array->n_entries == 0)				\
			return NULL;					\
									\
		return &array->values[array->n_entries - 1];		\
	}								\
									\
	AN_CC_UNUSED static inline type *				\
	an_array_value_##name(struct an_array_##name *array, unsigned int i) \
	{								\
									\
		return &array->values[i];				\
	}								\
									\
	AN_CC_UNUSED static inline type const *				\
	an_array_value_const_##name(const struct an_array_##name *array, \
				    unsigned int i)			\
	{								\
		return &array->values[i];			\
	}								\
									\
	AN_CC_UNUSED static inline unsigned int				\
	an_array_length_##name(const struct an_array_##name *array)	\
	{								\
									\
		return array->n_entries;				\
	}								\
									\
	AN_CC_UNUSED static inline void					\
	an_array_shrink_##name(struct an_array_##name *array)		\
	{								\
		an_array_resize_##name(array, array->n_entries);	\
		return;							\
	}								\
									\
	AN_CC_UNUSED static inline bool					\
	an_array_initialized_##name(const struct an_array_##name *array) \
	{								\
									\
		return (array->values != NULL);				\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_array_swap_##name(struct an_array_##name *array,		\
	    unsigned int i, unsigned int j)				\
	{								\
		type temp = array->values[i];				\
									\
		array->values[i] = array->values[j];			\
		array->values[j] = temp;				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static inline void					\
	an_array_reset_##name(struct an_array_##name *array)		\
	{								\
									\
		array->n_entries = 0;					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static inline const type *				\
	an_array_buffer_##name(const struct an_array_##name *array, unsigned int *n_entries) \
	{								\
									\
		*n_entries = array->n_entries;				\
		return (const type *)array->values;			\
	}								\
									\
	AN_CC_UNUSED static inline void					\
	an_array_truncate_to_size_##name(struct an_array_##name *array, size_t size) \
	{								\
									\
		if (size >= array->n_entries) {				\
			return;						\
		}							\
		array->n_entries = size;				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static inline type *				\
	an_array_pop_##name(struct an_array_##name *array,		\
			    unsigned int *entries)			\
	{								\
									\
		if (entries != NULL)					\
			*entries = array->n_entries;			\
									\
		if (array->n_entries == 0)				\
			return NULL;					\
									\
		return &array->values[--array->n_entries];		\
	}								\
									\
	AN_CC_UNUSED static inline type *				\
	an_array_push_##name(struct an_array_##name *array,		\
			     type const *value)				\
	{								\
									\
		if (array->n_entries >= array->capacity) {		\
			size_t new_size = (array->capacity << 1);	\
			if (array->n_entries + 1 > new_size) { 		\
				new_size = array->n_entries + 1;	\
			} 						\
									\
			an_array_resize_##name(array, new_size);	\
		}							\
									\
		if (value != NULL) {					\
			memcpy(&array->values[array->n_entries], value, sizeof(type)); \
		} else {						\
			memset(&array->values[array->n_entries], 0, sizeof(type)); \
		}							\
									\
		return &array->values[array->n_entries++];		\
	}								\
									\
	AN_CC_UNUSED static inline void					\
	an_array_remove_index_in_order_##name(struct an_array_##name *array, \
				     unsigned int i)			\
	{								\
									\
		assert(i < array->n_entries);				\
		if (i == array->n_entries - 1) {			\
			array->n_entries--;				\
			return;						\
		}							\
		array->n_entries--;					\
		memmove(&array->values[i], &array->values[i + 1],	\
			sizeof(type) * (array->n_entries - i));		\
		return;							\
	}								\
									\
	AN_CC_UNUSED static inline void					\
	an_array_remove_index_##name(struct an_array_##name *array,	\
	    size_t i)							\
	{								\
									\
		assert(i < array->n_entries);				\
		if (i == array->n_entries - 1) {			\
			array->n_entries--;				\
			return;						\
		}							\
									\
		array->values[i] = array->values[--array->n_entries];	\
		return;							\
	}								\
									\
	AN_CC_UNUSED static inline void					\
	an_array_sort_##name(struct an_array_##name *array,		\
			     int (*compar)(const void *, const void *))	\
	{								\
									\
		if (an_array_length_##name(array) == 0) {		\
			return;						\
		}							\
									\
		qsort(array->values, array->n_entries,			\
		    sizeof(type), compar);				\
		return;							\
	}

#define AN_ARRAY_FOREACH(array, cursor)					\
	for (unsigned int _an_i = 0, _n = (array)->n_entries;		\
	    _an_i < _n && ((cursor = &(array)->values[_an_i]), 1); _an_i++)

#define AN_ARRAY_FOREACH_VAL(array, cursor)				\
	for (unsigned int _an_i = 0, _n = (array)->n_entries;		\
	    _an_i < _n && ((cursor = (array)->values[_an_i]), 1); _an_i++)

AN_EXTERN_C_END

#endif /* AN_ARRAY_H */
