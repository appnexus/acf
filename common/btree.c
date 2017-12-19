#include <ck_md.h>
#include <stdlib.h>
#include <string.h>

#include "common/an_malloc.h"
#include "common/an_string.h"
#include "common/assert_dev.h"
#include "common/btree.h"
#include "common/common_types.h"
#include "common/libevent_extras.h"
#include "common/server_config.h"
#include "common/util.h"

static BTREE_CONTEXT_DEFINE(default_context, "default");

AN_MALLOC_DEFINE(btree_context_token,
    .string = "btree_context_t",
    .mode   = AN_MEMORY_MODE_FIXED,
    .size   = sizeof(btree_context_t),
    .use_pool_allocation = true);

DEFINE_AN_SSTM_OPS(btree_sstm_ops, "sstm_binary_tree_t", sstm_binary_tree_t,
    AN_SSTM_INIT(sstm_binary_tree_t, btree_overwrite),
    AN_SSTM_FREEZE(sstm_binary_tree_t, btree_end_bulk_mode),
    AN_SSTM_RELEASE(sstm_binary_tree_t, btree_shallow_deinit));

void AN_CC_NO_SANITIZE
init_btree(void)
{
	extern const struct binary_tree_context *const __start_btree_context_link_list[];
	extern const struct binary_tree_context *const __stop_btree_context_link_list[];

	for (const struct binary_tree_context *const *link = __start_btree_context_link_list;
	     link < __stop_btree_context_link_list;
	     link++) {
		const struct binary_tree_context *ptr = *link;

		if (ptr == NULL) {
			continue;
		}

		*ptr->to_patch = ptr;
	}

	return;
}

static int
find_index(const binary_tree_t *tree, const void *key)
{
	int low = 0;
	int high = tree->num - 1;

	while (low <= high) {
		int ix = (low + high) / 2;
		int comp = tree->compar(tree->base + ix * tree->size, key);
		if (comp < 0) {
			low = ix + 1;
		} else if (comp > 0) {
			high = ix - 1;
		} else {
			return ix;
		}
	}

	return ~low;
}

static void
btree_grow(binary_tree_t *tree)
{
	size_t capacity = tree->max;
	size_t new_capacity = max(1UL, capacity * 2);
	size_t from = capacity * tree->size;
	size_t to = new_capacity * tree->size;

	if (tree->num < capacity) {
		return;
	}

	tree->max = new_capacity;
	tree->base = an_realloc_region(tree->context->base_token, tree->base, from, to);
	return;
}

static void
btree_shrink(binary_tree_t *tree)
{
	size_t capacity = tree->max;
	size_t new_capacity = capacity / 4;
	size_t from = capacity * tree->size;
	size_t to = new_capacity * tree->size;

	if (capacity == 0 || tree->num > new_capacity) {
		return;
	}

	tree->max = new_capacity;
	tree->base = an_realloc_region(tree->context->base_token, tree->base, from, to);
	return;
}

static void *
btree_append_element(binary_tree_t *tree, const void *key)
{
	size_t index = tree->num;

	btree_grow(tree);
	tree->num++;
	return memcpy((char *)tree->base + index * tree->size, key, tree->size);
}

static void *
btree_overwrite_element(binary_tree_t *tree, int ix, const void *key)
{
	void *dst = (char *)tree->base + ix * tree->size;

	if (tree->free_cb != NULL) {
		tree->free_cb(dst);
	}

	return memcpy(dst, key, tree->size);
}

void *
btree_insert(binary_tree_t *tree, const void *key)
{
	int cmp, ix;
	size_t to_move;

	cmp = -1;
	if (tree->num > 0) {
		cmp = tree->compar((const char *)tree->base + (tree->num - 1) * tree->size, key);
	}

	if (cmp < 0) {
		return btree_append_element(tree, key);
	}

	if (cmp == 0) {
		return btree_overwrite_element(tree, tree->num - 1, key);
	}

	if (tree->bulk_mode == true) {
		tree->sorted = false;
		return btree_append_element(tree, key);
	}

	ix = find_index(tree, key);
	if (ix >= 0) {
		return btree_overwrite_element(tree, ix, key);
	}

	btree_grow(tree);
	ix = ~ix;
	to_move = tree->num - ix;
	if (to_move > 0) {
		memmove((char *)tree->base + (ix + 1) * tree->size, tree->base + ix * tree->size,
		    to_move * tree->size);
	}

	tree->num++;
	return memcpy((char *)tree->base + ix * tree->size, key, tree->size);
}

void *
btree_lookup(binary_tree_t *tree, const void *key)
{

	if (tree == NULL) {
		return NULL;
	}

	assert_dev(!tree->bulk_mode);
	int ix = find_index(tree, key);
	if (ix < 0) {
		return NULL;
	}

	return tree->base + ix * tree->size;
}

void
btree_delete_index(binary_tree_t *tree, size_t ix)
{

	btree_delete_index_range(tree, ix, ix + 1);
}

void
btree_delete_index_range(binary_tree_t *tree, size_t start_ix, size_t end_ix)
{

	assert(end_ix <= tree->num);

	if (tree->free_cb != NULL) {
		for (size_t i = start_ix; i < end_ix; i++) {
			an_sstm_call_size(tree->free_cb,
			    tree->base + i * tree->size,
			    tree->size);
		}
	}

	size_t to_move = tree->num - end_ix;
	if (to_move > 0) {
		memmove(tree->base + start_ix * tree->size,
		    tree->base + end_ix * tree->size,
		    to_move * tree->size);
	}
	tree->num -= end_ix - start_ix;

	btree_shrink(tree);
}

bool
btree_delete(binary_tree_t *tree, const void *key)
{
	int ix;

	if (tree == NULL) {
		return false;
	}

	if (tree->sorted == false) {
		bool bulk_mode = tree->bulk_mode;

		btree_end_bulk_mode(tree);
		tree->bulk_mode = bulk_mode;
	}

	ix = find_index(tree, key);
	if (ix < 0) {
		return false;
	}

	btree_delete_index_range(tree, ix, ix + 1);
	return true;
}

bool
btree_init_internal(binary_tree_t *tree, const btree_context_t *context, size_t obj_size,
    size_t initial_capacity, int (*compar)(const void *, const void *),
    void (*free_cb)(void *))
{

	if (context == NULL) {
		context = default_context;
	}

	tree->context = context;
	tree->num = 0;
	tree->max = max(initial_capacity, 4UL);
	tree->size = obj_size;
	tree->base = an_malloc_region(context->base_token, obj_size * tree->max);
	if (tree->base == NULL) {
		return false;
	}

	tree->compar = compar;
	tree->free_cb = free_cb;
	tree->bulk_mode = false;
	tree->sorted = true;
	return true;
}

binary_tree_t *
create_btree_internal(const btree_context_t *context, size_t obj_size,
    size_t initial_capacity, int (*compar)(const void *, const void *),
    void (*free_cb)(void *))
{
	binary_tree_t *tree;

	if (context == NULL) {
		context = default_context;
	}

	tree = an_calloc_object(context->tree_token);
	if (tree == NULL) {
		return NULL;
	}

	if (btree_init_internal(tree, context, obj_size, initial_capacity,
	    compar, free_cb) == false) {
		an_free(context->tree_token, tree);
		return NULL;
	}

	return tree;
}

sstm_binary_tree_t *
create_sstm_btree_internal(const btree_context_t *context, size_t obj_size,
    size_t initial_capacity, int (*compar)(const void *, const void *),
    void (*free_cb)(void *))
{
	sstm_binary_tree_t *tree;

	if (context == NULL) {
		context = default_context;
	}

	tree = an_calloc_object(context->tree_token);
	if (tree == NULL) {
		return NULL;
	}

	if (btree_init_internal(&tree->an_sstm_data,
	    context, obj_size, initial_capacity,
	    compar, free_cb) == false) {
		an_free(context->tree_token, tree);
		return NULL;
	}

	return tree;
}

void
btree_overwrite(binary_tree_t *dst, const binary_tree_t *src)
{

	btree_deinit(dst);
	if (src == NULL) {
		memset(dst, 0, sizeof(*dst));
		return;
	}

	memcpy(dst, src, sizeof(*dst));
	an_sstm_duplicate_size(dst->context->base_token, dst->base, dst->max, dst->size);
	return;
}

binary_tree_t *
btree_copy(const binary_tree_t *src)
{
	binary_tree_t *dest = NULL;

	if (src == NULL) {
		return NULL;
	}

	dest = an_calloc_object(src->context->tree_token);
	btree_overwrite(dest, src);
	return dest;
}

sstm_binary_tree_t *
btree_sstm_copy(const binary_tree_t *src)
{
	sstm_binary_tree_t *dest = NULL;

	if (src == NULL) {
		return NULL;
	}

	dest = an_calloc_object(src->context->tree_token);
	btree_overwrite(&dest->an_sstm_data, src);
	return dest;
}

void
btree_foreach_internal(binary_tree_t *tree,
    void (*foreach_cb)(void *obj, void *context, bool is_first),
    void *context)
{

	if (tree == NULL) {
		return;
	}

	for (size_t i = 0; i < tree->num; i++) {
		foreach_cb(tree->base + i * tree->size, context, (i == 0));
	}

	return;
}

void
btree_clear(binary_tree_t *tree)
{

	if (tree == NULL) {
		return;
	}

	if (tree->free_cb != NULL) {
		for (size_t i = 0; i < tree->num; i++) {
			tree->free_cb(tree->base + i * tree->size);
		}
	}
	tree->num = 0;
}

void
btree_shallow_deinit(binary_tree_t *tree)
{

	if (tree == NULL || tree->base == NULL) {
		return;
	}

	an_free(tree->context->base_token, tree->base);
	return;
}

void
btree_deinit(binary_tree_t *tree)
{

	if (tree == NULL || tree->base == NULL) {
		return;
	}

	btree_clear(tree);
	an_free(tree->context->base_token, tree->base);
	memset(tree, 0, sizeof(*tree));
	return;
}

void
free_btree(binary_tree_t *tree)
{
	an_malloc_token_t context;

	if (tree == NULL) {
		return;
	}

	context = tree->context->tree_token;
	btree_deinit(tree);
	an_free(context, tree);
}

static void
cleanup(sstm_binary_tree_t *ptr)
{
	binary_tree_t *tree = &ptr->an_sstm_data;
	an_malloc_token_t context = tree->context->tree_token;

	btree_deinit(tree);
	an_free(context, ptr);
	return;
}

void
free_sstm_btree(sstm_binary_tree_t *tree)
{

	if (tree == NULL) {
		return;
	}

	an_sstm_call(cleanup, tree);
	return;
}

void
btree_start_bulk_mode(binary_tree_t *tree, size_t num_new_elements)
{
	tree->bulk_mode = true;
	if (tree->num + num_new_elements > tree->max) {
		size_t from = tree->max * tree->size;
		size_t to = (tree->num + num_new_elements) * tree->size;

		tree->max = tree->num + num_new_elements;
		tree->base = an_realloc_region(tree->context->base_token, tree->base, from, to);
	}
}

bool
btree_resize(binary_tree_t *tree)
{
	size_t from = tree->max;
	size_t to = max(tree->num, (size_t)CK_MD_CACHELINE / tree->size);
	size_t size = tree->size;
	void *base;

	base = an_realloc_region(tree->context->base_token, tree->base, from * size, to * size);
	if (base == NULL) {
		return false;
	}

	tree->base = base;
	tree->max = to;
	return true;
}


void
btree_sort(binary_tree_t *tree)
{
	int num = tree->num;
	int remaining;
	int size = tree->size;
	char *current, *dst;

	tree->bulk_mode = false;
	tree->sorted = true;
	if (tree->num <= 1) {
		return;
	}

	qsort(tree->base, num, size, tree->compar);
	/* Dedup. */
	dst = tree->base;
	current = dst + size;
	remaining = 1;
	for (int i = 1; i < num; i++, current += size) {
		int r;

		r = tree->compar(dst, current);
		assert_dev(r <= 0);
		if (r < 0) {
			dst += size;
			remaining++;
		} else if (tree->free_cb != NULL) {
			tree->free_cb(dst);
		}

		if (dst != current) {
			memcpy(dst, current, size);
		}
	}

	tree->num = remaining;
	return;
}

void
btree_end_bulk_mode(binary_tree_t *tree)
{

	tree->bulk_mode = false;
	if (tree->sorted == false) {
		btree_sort(tree);
	}

	return;
}

btree_context_t *
create_btree_context(const char *name)
{
	btree_context_t *context;

	context = an_calloc_object(btree_context_token);
	if (context == NULL) {
		return NULL;
	}

	if (an_string_asprintf(&context->tree_type.string, "binary_tree_t:%s:tree", name) < 0) {
		goto out_free_context;
	}

	if (an_string_asprintf(&context->base_type.string, "binary_tree_t:%s:base", name) < 0) {
		goto out_free_context;
	}

	context->tree_type.mode = AN_MEMORY_MODE_FIXED;
	context->tree_type.size = sizeof(sstm_binary_tree_t);
	context->tree_type.use_pool_allocation = true;

	context->base_type.mode = AN_MEMORY_MODE_VARIABLE;
	context->base_type.use_pool_allocation = true;
	context->tree_token
		= an_malloc_register(&context->tree_type);
	context->base_token
		= an_malloc_register(&context->base_type);

	return context;

out_free_context:
	destroy_btree_context(context);
	return NULL;
}

void
destroy_btree_context(btree_context_t *context)
{

	an_string_free(context->base_type.string);
	an_string_free(context->tree_type.string);
	an_free(btree_context_token, context);
}
