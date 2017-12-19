#ifndef _COMMON_BTREE_H
#define _COMMON_BTREE_H

#include <event.h>
#include <evhttp.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>

#include "an_cc.h"
#include "an_malloc.h"
#include "common/an_sstm.h"

typedef int binary_tree_compare_cb_t(const void *, const void *);
typedef void binary_tree_free_cb_t(void *);

struct binary_tree_context {
	an_malloc_token_t base_token;
	an_malloc_token_t tree_token;
	struct an_malloc_type base_type;
	struct an_malloc_type tree_type;
	const struct binary_tree_context **to_patch;
};

typedef struct binary_tree_context btree_context_t;

typedef struct binary_tree {
	void *base;
	binary_tree_compare_cb_t *compar;
	uint32_t max;
	uint32_t num;
	uint16_t size;
	bool bulk_mode;
	bool sorted;
	binary_tree_free_cb_t *free_cb;
	const btree_context_t *context;
} binary_tree_t;

DEFINE_SSTM_TYPE(sstm_binary_tree, struct binary_tree);

typedef struct sstm_binary_tree sstm_binary_tree_t;

extern struct an_sstm_ops btree_sstm_ops;

DEFINE_SSTM_WRITE(btree_sstm_write, sstm_binary_tree_t, btree_sstm_ops);

#define BTREE_CAST_FREE_CB(CB, TYPE)					\
	AN_CC_CAST_IF_COMPATIBLE((CB), void (*)(TYPE *),		\
	    binary_tree_free_cb_t *)

/**
 * Most of the work here is in AN_MALLOC_REGISTER.  We only need the
 * btree_context_link_list section to backpatch the address of the
 * context in PTR.  That section contains pointers to contexts rather
 * than context structs because the linker can insert its own padding,
 * and it's easier to detect NULL than arbitrary padding in structs.
 */
#define BTREE_CONTEXT_REGISTER__(NONCE, PTR, STRING)			\
	static struct binary_tree_context btree_ctx_##NONCE = {		\
		.base_type = { 						\
			.string = STRING ":base",			\
			.mode = AN_MEMORY_MODE_VARIABLE,		\
			.use_pool_allocation = true			\
		},							\
		.tree_type = {						\
			.string = STRING ":tree",			\
			.mode = AN_MEMORY_MODE_FIXED,			\
			.size = sizeof(sstm_binary_tree_t),		\
			.use_pool_allocation = true			\
		},							\
		.to_patch = &PTR					\
	};								\
	AN_MALLOC_REGISTER(btree_ctx_##NONCE.base_type,			\
	    &btree_ctx_##NONCE.base_token);				\
	AN_MALLOC_REGISTER(btree_ctx_##NONCE.tree_type,			\
	    &btree_ctx_##NONCE.tree_token);				\
	static const struct binary_tree_context *const btree_link_##NONCE \
	__attribute__((section("btree_context_link_list"), used)) = &btree_ctx_##NONCE

#define BTREE_CONTEXT_REGISTER_(NONCE, PTR, STRING) BTREE_CONTEXT_REGISTER__(NONCE, PTR, STRING)

/**
 * Statically register a btree context with name STRING and store a pointer to it in *PTR.
 */
#define BTREE_CONTEXT_REGISTER(PTR, STRING) BTREE_CONTEXT_REGISTER_(__COUNTER__, PTR, STRING)

/**
 * Convenience macro to declare a context pointer and define it.
 */
#define BTREE_CONTEXT_DEFINE(NAME, STRING)	\
	const struct binary_tree_context *NAME; \
	BTREE_CONTEXT_REGISTER(NAME, STRING)

btree_context_t *create_btree_context(const char *name);
void destroy_btree_context(btree_context_t *context);

void *btree_insert(binary_tree_t *tree, const void *key);
void *btree_lookup(binary_tree_t *tree, const void *key);

static inline bool
btree_is_empty(const binary_tree_t *tree)
{
	return tree == NULL || tree->num == 0;
}

static inline size_t
btree_item_count(const binary_tree_t *tree)
{

	if (tree == NULL) {
		return 0;
	}

	return tree->num;
}

static inline const void *
btree_array_const_get(const binary_tree_t *tree, size_t *array_size)
{

	if (array_size != NULL) {
		*array_size = btree_item_count(tree);
	}

	return (tree != NULL) ? tree->base : NULL;
}

static inline void *
btree_array_get(binary_tree_t *tree, size_t *array_size)
{

	return (void *)btree_array_const_get(tree, array_size);
}

static inline bool
btree_insert_is_create(binary_tree_t *tree, const void *key)
{
	size_t count;
	count = btree_item_count(tree);

	btree_insert(tree, key);
	return count != btree_item_count(tree);
}

#define btree_item_count_static btree_item_count

static inline void *
btree_lookup_index(const binary_tree_t *tree, size_t index)
{

	if (tree == NULL || index >= tree->num) {
		return NULL;
	}

	return (char *)tree->base + index * tree->size;
}

void btree_delete_index(binary_tree_t *tree, size_t ix);
bool btree_delete(binary_tree_t *tree, const void *key);

/*
 * Delete a range of elements from the btree
 *
 * Deletes elements ranging from the start index (inclusive) to end index (exclusive)
 *
 * @param tree The tree
 * @param start_ix index of the first element to be deleted
 * @param end_ix index of the element after the last one to be deleted
 */
void btree_delete_index_range(binary_tree_t *tree, size_t start_ix, size_t end_ix);
void btree_clear(binary_tree_t *tree);

/**
 * In-place initialization of a sorted array.
 *
 * @param tree	Pointer to a region of memory reserved for
 *		a sorted array.
 * @param ctx 	Pointer to an_malloc_group associated with
 * 		allocation. May be NULL.
 * @param obj_size Size of objects to be stored, in bytes.
 * @param initial_capacity Initial number of elements to allocate space for.
 * @param compar Pointer to comparison function.
 * @param free_cb Pointer to free function.
 * @return Returns true on success and false on failure.
 */
bool btree_init_internal(binary_tree_t *tree, const btree_context_t *ctx, size_t obj_size, size_t initial_capacity,
    binary_tree_compare_cb_t *compar, binary_tree_free_cb_t *free_cb);

#define btree_init(TREE, CTX, TYPE, CAP, CMP, FREE)			\
	btree_init_internal((TREE), (CTX), sizeof(TYPE), (CAP),		\
	    AN_CC_CAST_COMPARATOR((CMP), TYPE),				\
	    BTREE_CAST_FREE_CB((FREE), TYPE))

/**
 * Destroy inlined binary_tree_t object.
 */
void btree_deinit(binary_tree_t *tree);

/**
 * Destroy the tree, but not its contents.
 */
void btree_shallow_deinit(binary_tree_t *tree);

binary_tree_t *create_btree_internal(const btree_context_t *, size_t obj_size, size_t initial_capacity,
    binary_tree_compare_cb_t *compar, binary_tree_free_cb_t *free_cb);

#define create_btree(CTX, TYPE, CAP, CMP, FREE)				\
	create_btree_internal((CTX), sizeof(TYPE), (CAP),		\
	    AN_CC_CAST_COMPARATOR((CMP), TYPE),				\
	    BTREE_CAST_FREE_CB((FREE), TYPE))

sstm_binary_tree_t *create_sstm_btree_internal(const btree_context_t *, size_t obj_size, size_t initial_capacity,
    binary_tree_compare_cb_t *compar, binary_tree_free_cb_t *free_cb);

#define create_sstm_btree(CTX, TYPE, CAP, CMP, FREE)			\
	create_sstm_btree_internal((CTX), sizeof(TYPE), (CAP),		\
	    AN_CC_CAST_COMPARATOR((CMP), TYPE),				\
	    BTREE_CAST_FREE_CB((FREE), TYPE))

void btree_overwrite(binary_tree_t *dst, const binary_tree_t *src);
binary_tree_t* btree_copy(const binary_tree_t* src);
sstm_binary_tree_t* btree_sstm_copy(const binary_tree_t* src);
void btree_foreach_internal(binary_tree_t *tree,
    void (*foreach_cb)(void *obj, void *context, bool is_first),
    void *context);

#define btree_foreach(TREE, CB, TYPE, CTX)				\
	btree_foreach_internal((TREE),					\
	    AN_CC_CAST_IF_COMPATIBLE((CB),				\
		void (*)(TYPE *, __typeof__(CTX), bool),		\
		void (*)(void *, void *, bool)),			\
	    (CTX))

#define BTREE_FOREACH(tree, cursor) 	\
	for (size_t _i = 0, _n = btree_item_count(tree); _i < _n && (cursor = btree_lookup_index(tree, _i)) != NULL; _i++)

void free_btree(binary_tree_t *tree);
void free_sstm_btree(sstm_binary_tree_t *tree);
void btree_start_bulk_mode(binary_tree_t *tree, size_t num_new);
void btree_end_bulk_mode(binary_tree_t *tree);

/**
 * Explicit sort and dedup step during bulk mode. Transition btree out
 * of bulk mode.
 */
void btree_sort(binary_tree_t *tree);

void init_btree(void);
bool btree_resize(binary_tree_t *tree);
#endif /* _COMMON_BTREE_H */
