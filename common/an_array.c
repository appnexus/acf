#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "x86_64/cpuid.h"
#include "an_array.h"
#include "an_malloc.h"
#include "an_thread.h"
#include "an_md.h"
#include "common_types.h"
#include "util.h"

static struct an_malloc_type an_array_type = {
	.string = "an_array_t",
	.mode   = AN_MEMORY_MODE_VARIABLE,
	.use_pool_allocation = true
};

static struct an_acf_allocator array_allocator = AN_ACF_ALLOCATOR_BASE;

AN_MALLOC_REGISTER(an_array_type, &array_allocator.an_token);

void
an_array_register_allocator(void)
{
	an_array_set_allocator((const struct an_allocator *)&array_allocator);
}

static void
an_array_thaw(struct an_array *array)
{

	an_swlock_init(&array->lock);
	an_sstm_duplicate_size(an_array_token, array->values, array->capacity, sizeof(void *));
	return;
}

static void
an_array_commit(struct an_array *dst, const struct an_array *src)
{

	dst->values = src->values;
	dst->capacity = src->capacity;
	dst->n_entries = src->n_entries;
	return;
}

DEFINE_AN_SSTM_OPS(an_array_ops, "an_array_t", sstm_an_array_t,
    AN_SSTM_THAW(sstm_an_array_t, an_array_thaw),
    AN_SSTM_COMMIT(sstm_an_array_t, an_array_commit),
    AN_SSTM_RELEASE(sstm_an_array_t, an_array_deinit));

void
an_array_init(an_array_t *array, unsigned int capacity)
{

	array->values = an_malloc_region(an_array_token, sizeof(void *) * capacity);
	array->capacity = capacity;
	array->n_entries = 0;
	an_swlock_init(&array->lock);
	return;
}

void
an_array_map(an_array_t *array, an_array_map_t *map)
{
	unsigned int i;

	for (i = 0; i < array->n_entries; i++)
		map(array->values[i]);

	return;
}

an_array_t *
an_array_create(unsigned int capacity)
{
	an_array_t *r;

	r = an_malloc_region(an_array_token, sizeof(*r));
	an_array_init(r, capacity);
	return r;
}

sstm_an_array_t *
sstm_an_array_create(unsigned int length)
{
	sstm_an_array_t *r;

	r = an_calloc_region(an_array_token, 1, sizeof(*r));
	an_array_init(&r->an_sstm_data, length);
	return r;
}

void
an_array_deinit(an_array_t *array)
{

	an_free(an_array_token, array->values);
	array->values = NULL;
	return;
}

void
an_array_destroy(an_array_t *array)
{

	if (array == NULL) {
		return;
	}

	an_array_deinit(array);
	an_free(an_array_token, array);
	return;
}

void
an_array_duplicate(an_array_t *array)
{

	if (array == NULL || array->values == NULL) {
		return;
	}

	array->values = an_malloc_copy(an_array_token, array->values,
	    array->capacity * sizeof(void *));
	return;
}

static void
cleanup(sstm_an_array_t *array)
{

	an_array_deinit(&array->an_sstm_data);
	an_free(an_array_token, array);
	return;
}

void
sstm_an_array_destroy(sstm_an_array_t *array)
{

	if (array == NULL) {
		return;
	}

	an_sstm_call(cleanup, array);
	return;
}

void
an_array_resize(an_array_t *array, unsigned int length)
{
	void *values;
	unsigned int allocated_length = max(length, 1U);

	values = an_realloc_region(an_array_token, array->values,
	    sizeof(void *) * array->capacity, sizeof(void *) * allocated_length);

	array->values = values;
	array->capacity = allocated_length;
	array->n_entries = min(array->n_entries, length);
	return;
}

void
an_array_grow_to(an_array_t *array, unsigned int goal, void *fill)
{
	void **values = array->values;
	unsigned int new_capacity = array->capacity;

	if (goal <= array->n_entries) {
		return;
	}

	if (new_capacity < 2) {
		new_capacity = 2;
	}

	while (new_capacity < goal) {
		if (new_capacity < UINT_MAX / 2) {
			new_capacity *= 2;
		} else {
			new_capacity = UINT_MAX;
		}
	}

	if (new_capacity > array->capacity) {
		values = an_realloc_region(an_array_token, array->values,
		    sizeof(void *) * (size_t)array->capacity,
		    sizeof(void *) * (size_t)new_capacity);
		array->values = values;
		array->capacity = new_capacity;
	}

	if (fill == NULL) {
		memset(values + array->n_entries, 0,
		    sizeof(void *) * (size_t)(goal - array->n_entries));
	} else {
		for (unsigned int i = array->n_entries; i < goal; i++) {
			values[i] = fill;
		}
	}

	array->n_entries = goal;
	return;
}

void
an_array_squash(an_array_t *array)
{

	if (array->capacity != array->n_entries) {
		an_array_resize(array, array->n_entries);
	}

	return;
}

void
an_array_trysquash(an_array_t *array)
{

	if (array->capacity == array->n_entries) {
		return;
	}

	if (an_swlock_write_trylock(&array->lock) == true) {
		an_array_resize(array, array->n_entries);
		an_swlock_write_unlock(&array->lock);
	}

	return;
}

void
an_array_shuffle(an_array_t *array)
{

	an_random_shuffle(array->values, an_array_length(array));
	return;
}

static void
an_array_defer_free(void *p)
{

	an_array_destroy(p);
	return;
}

void
an_array_defer(an_array_t *array)
{

	an_thread_defer(array, an_array_defer_free);
	return;
}
