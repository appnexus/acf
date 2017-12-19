#ifndef _AN_MALLOC_H
#define _AN_MALLOC_H

#include <assert.h>
#include <evhttp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/types.h>

#include <acf/an_allocator.h>
#include <acf/an_cc.h>
#include <acf/an_util.h>

#include "common/an_hook.h"

/** Simple memory allocator interface. */
struct an_memory_allocator {
	void *(*malloc)(size_t size);
	void *(*calloc)(size_t nmemb, size_t size);
	void (*free)(void *ptr);

	/** prepend an_ to str/strndup to avoid macro name clashes */
	char *(*an_strdup)(const char *s);
	char *(*an_strndup)(const char *s, size_t n);
};

enum an_malloc_mode {
	AN_MEMORY_MODE_FIXED = 1,
	AN_MEMORY_MODE_VARIABLE = 2
};

enum an_malloc_debug {
	AN_MEMORY_DEBUG_TYPE = 2,
	AN_MEMORY_DEBUG_ZERO = 4,
	AN_MEMORY_DEBUG_LEAK = 8
};

struct an_malloc_type {
	char *string;
	enum an_malloc_mode mode;
	unsigned int size;
	unsigned int id;
	bool use_pool_allocation;
};

typedef struct an_malloc_type an_malloc_type_t;

struct an_malloc_token {
	uint32_t size; /* object size, 0 for region */
	uint32_t use_pool_allocation : 1;
	uint32_t id : 31;
};

typedef struct an_malloc_token an_malloc_token_t;

#define AN_MALLOC_TOKEN_INIT { .id = 0 }

/**
 * A link struct informs the an_malloc subsystem to register the type
 * and store its token somewhere if link->token is non-NULL.
 */
struct an_malloc_register_link {
	struct an_malloc_type *type;
	struct an_malloc_token *token;
};

/**
 * In addition to creating a link struct, we store a pointer to it in
 * a special section that an_malloc knows about.  We must use a
 * section of pointers rather than a section of structs because the
 * linker is free to interleaves zeros for padding.
 *
 * The linker will create two extern symbols,
 * __start_an_malloc_register_link_list and
 * __stop_an_malloc_register_link_list at the first and one past the
 * laste byte of the section.  an_malloc uses these symbols to iterate
 * through the set of an_malloc_register_link pointers in the
 * (statically linked) application.
 */
#define AN_MALLOC_REGISTER__(NONCE, TYPE, TOKEN)			\
	static const struct an_malloc_register_link			\
	an_malloc_register_link_##NONCE = {				\
		.type = &(TYPE),					\
		.token = (TOKEN)					\
	};								\
	static const struct an_malloc_register_link *			\
	an_malloc_register_link_ptr_##NONCE				\
	__attribute__((section("an_malloc_register_link_list"), used)) = &an_malloc_register_link_##NONCE

#define AN_MALLOC_REGISTER_(NONCE, TYPE, TOKEN) AN_MALLOC_REGISTER__(NONCE, TYPE, TOKEN)

/**
 * Statically inform an_malloc to register TYPE and optionally store
 * its id in the token pointed to by TOKEN (if non-NULL).
 */
#define AN_MALLOC_REGISTER(TYPE, TOKEN) AN_MALLOC_REGISTER_(__COUNTER__, TYPE, TOKEN)

/**
 * Convenience macro to simultaneously define an an_malloc type, and
 * statically register it into TOKEN.
 */
#define AN_MALLOC_DEFINE(TOKEN, ...)			\
	struct an_malloc_token TOKEN;			\
	static struct an_malloc_type TOKEN##_type = {	\
		__VA_ARGS__				\
	};						\
	AN_MALLOC_REGISTER(TOKEN##_type, &TOKEN)

/*
 * Registers HTTP control handler.
 */
void an_malloc_handler_http_enable(struct evhttp *);

/*
 * Registers a type with the memory sub-system.
 */
an_malloc_token_t an_malloc_register(an_malloc_type_t *);

#define AN_MALLOC_DEFINE_CK_MALLOC(prefix, type_name, ck_malloc_name)	\
	static void							\
	prefix##_destroy(void *p)					\
	{								\
									\
		an_free(type_name, p);				\
		return;							\
	}								\
									\
	static void *							\
	prefix##_malloc(size_t r)					\
	{								\
									\
		return an_malloc_region(type_name, r);			\
	}								\
									\
	static void							\
	prefix##_free(void *p, size_t bytes, bool defer)		\
	{								\
									\
		if (defer == false) {					\
			prefix##_destroy(p);				\
		} else {						\
			an_smr_call(p, prefix##_destroy);		\
		}							\
									\
		return;							\
	}								\
									\
	struct ck_malloc ck_malloc_name = {				\
		.malloc = prefix##_malloc,				\
		.free   = prefix##_free					\
	};

#define AN_MALLOC_DEFINE_CK_MALLOC_NO_SMR(prefix, type_name, ck_malloc_name)	\
	static void *							\
	prefix##_malloc(size_t r)					\
	{								\
									\
		return an_malloc_region(type_name, r);			\
	}								\
									\
	static void							\
	prefix##_free(void *p, size_t bytes, bool defer)		\
	{								\
									\
		(void) bytes;						\
		(void) defer;						\
		an_free(type_name, p);					\
	}								\
									\
	struct ck_malloc ck_malloc_name = {				\
		.malloc = prefix##_malloc,				\
		.free   = prefix##_free					\
	};

void *an_malloc_protobuf_alloc(void *allocator_data, size_t size);

void an_malloc_protobuf_free(void *allocator_data, void *ptr);

#define AN_MALLOC_GET_PROTOBUF_DESTRUCTOR_SIGNATURE(prefix, type_name, arg)	\
	void									\
	prefix##_protobuf_destroy(type_name *arg)

/*
 * We currently require that an_malloc_token_t fits inside the
 * protobuf allocator's `allocator_data' void pointer
 */
_Static_assert(sizeof(an_malloc_token_t) <= sizeof(void *),
    "an_malloc_token_t must not be larger than a pointer");

/**
 * Define a protobuf parser that uses an_malloc as its allocator.
 *
 * @param prefix The prefix to use for function and type names
 * @param type_name The type of object to be parsed from protobuf
 * @param protobuf_ns The protobuf function namespace to use
 *        (eg: for ns=ABC, call ABC__unpack / ABC__free_unpacked)
 */
#define AN_MALLOC_DEFINE_PROTOBUF_PARSER(prefix, type_name, protobuf_ns)	\
	static ProtobufCAllocator prefix##_protobuf_allocator = {		\
		.alloc = an_malloc_protobuf_alloc,				\
		.free = an_malloc_protobuf_free,				\
		.tmp_alloc = NULL,						\
		.max_alloca = UINT_MAX,						\
		.allocator_data = NULL						\
	};									\
										\
	static struct an_malloc_type prefix##_protobuf_allocator_type = {	\
		.string = "protobuf:" STRINGIFY(prefix),			\
		.mode = AN_MEMORY_MODE_VARIABLE,				\
		.use_pool_allocation = true					\
	};									\
										\
	AN_MALLOC_REGISTER(prefix##_protobuf_allocator_type,			\
	    (void *)&prefix##_protobuf_allocator.allocator_data);		\
										\
	static type_name *							\
	prefix##_protobuf_unpack(const uint8_t *data, size_t size)		\
	{									\
										\
		return protobuf_ns##__unpack(&prefix##_protobuf_allocator,	\
		    size, data);						\
	}									\
										\
	AN_MALLOC_GET_PROTOBUF_DESTRUCTOR_SIGNATURE(prefix, type_name, arg)	\
	{									\
										\
		if (arg != NULL) {						\
			protobuf_ns##__free_unpacked(arg,			\
			    &prefix##_protobuf_allocator);			\
		}								\
	}

#if defined(__GNUC__) || defined(__clang__)
#define MALLOC_ATTRIBUTE __attribute__((malloc, warn_unused_result))
#else
#define MALLOC_ATTRIBUTE
#endif

struct an_malloc_keywords {
	bool dummy;
	struct { char hack; } hack; /* Miscompile on positional arguments. */
	bool non_pool;
	uint16_t owner_id;
};

/*
 * Returns a region of memory that is large enough to hold an object
 * of the specified type. On error, returns NULL and sets errno.
 */
MALLOC_ATTRIBUTE void *an_calloc_object_internal(an_malloc_token_t, struct an_malloc_keywords keys);

/*
 * Changes the size of the memory pointed to by the second argument from
 * the number of bytes specified in the third argument to the number of
 * bytes specified in the fourth argument.
 */
MALLOC_ATTRIBUTE void *an_realloc_region_internal(an_malloc_token_t, void *, size_t, size_t, struct an_malloc_keywords keys);

/*
 * Returns a contiguous region of memory of at least n bytes.
 */
MALLOC_ATTRIBUTE void *an_malloc_region_internal(an_malloc_token_t, size_t n, struct an_malloc_keywords keys);
MALLOC_ATTRIBUTE void *an_calloc_region_internal(an_malloc_token_t, size_t, size_t, struct an_malloc_keywords keys);

/*
 * Allows the operating system to reclaim the memory pointed to by the second argument.
 * Works on both fixed and variable sized objects
 */
void an_free_internal(an_malloc_token_t, void *, struct an_malloc_keywords keys);

/* really declared in an_thread.h, but circular deps. */
extern __thread unsigned int an_thread_current_id;

#define an_free(TYPE, PTR, ...)						\
	({								\
		an_malloc_token_t AN_FREE_token = (TYPE);		\
		void *AN_FREE_ptr = (PTR);				\
		struct an_malloc_keywords AN_FREE_kw = {		\
			.dummy = 0, ##__VA_ARGS__			\
		};							\
									\
		do {							\
			AN_HOOK_UNSAFE(an_free, disable) {		\
				if (an_thread_current_id == 0) {	\
					break;				\
				}					\
			}						\
									\
			an_free_internal(AN_FREE_token, AN_FREE_ptr, AN_FREE_kw); \
		} while (0);						\
	})

/*
 * Get the total memory use for an owner's batch objects
 */
uint64_t an_malloc_owner_get_active(uint16_t);

/*
 * Duplicate the memory pointed to by the second argument.
 */
MALLOC_ATTRIBUTE void *an_malloc_copy_internal(an_malloc_token_t, const void *, size_t, struct an_malloc_keywords keys);

#define an_malloc_region(TYPE, SIZE, ...) an_malloc_region_internal((TYPE), (SIZE), (struct an_malloc_keywords){ .dummy = 0, __VA_ARGS__ })

#define an_calloc_object(TYPE, ...) an_calloc_object_internal((TYPE), (struct an_malloc_keywords){ .dummy = 0, __VA_ARGS__ })

#define an_calloc_region(TYPE, NUM, SIZE, ...) an_calloc_region_internal((TYPE), (NUM), (SIZE), (struct an_malloc_keywords){ .dummy = 0, __VA_ARGS__ })

#define an_realloc_region(TYPE, PTR, SIZE_FROM, SIZE_TO, ...) an_realloc_region_internal((TYPE), (PTR), (SIZE_FROM), (SIZE_TO), (struct an_malloc_keywords){ .dummy = 0, __VA_ARGS__ })

#define an_malloc_copy(TYPE, PTR, SIZE, ...) an_malloc_copy_internal((TYPE), (PTR), (SIZE), (struct an_malloc_keywords){ .dummy = 0, __VA_ARGS__ })

/*
 * Initialize memory subsystem.
 */
int an_malloc_init(void);
int an_malloc_detect(struct evhttp *);

#undef MALLOC_ATTRIBUTE

/*
 * Pool Party (ADI-1177) begins below.
 *
 * General idea: We're adding the concept of a transaction to our
 * memory allocator on the request path. This doesn't apply to
 * allocations made from the batch-processing thread.
 *
 * When we'll use the new allocator:
 * - If we're in a thread that's not the batches thread AND
 * - If we're allocating an an_malloc_type which is opted into the new allocator AND
 * - If we're not in a call stack which disallows the use of the new
 *      allocator because it's executed outside of a transaction
 *
 * Basically, we need to only use the new allocator when we're on an
 * imp_req path, but we wanted to avoid having to annotate allocation
 * sites. Instead, We're annotating functions which are called on worker
 * threads but outside of imp_reqs, which are far fewer.
 *
 * The new allocator works as such when we've chosen to use it:
 *
 * When we create a transaction (currently we do this in imp_req_new),
 * we allocate an object to act as a reference for that transaction's
 * lifetime, and to hold its allocations in a large region which we
 * allocate once (>= 2MB). This is called an an_malloc_epoch.
 *
 * The transaction sets a reference to the epoch in which it was
 * created, and increments a reference count on that epoch.
 *
 * When the transaction allocates memory, it uses a bump pointer from
 * the newest an_malloc_epoch (whatever the current one is), and
 * malloc as such should be fast. If there's not space in the current
 * newest epoch, it creates a new one guaranteed to have enough space
 * and allocates from that. It does not increment the ref count on any
 * new epochs created, since it only needs to hold a reference open to
 * the oldest epoch which it has used (since no imp_req will destroy a
 * newer epoch without having destroyed all older ones, and thus this
 * new epoch cannot be destroyed unless the imp_req that has opened it
 * exits).
 *
 * Free is essentially a nop with the new allocator. We have a bitmap
 * of regions which we're using as pools as a way to determine whether or not an
 * allocation came from a region, but that's only so we can call
 * free() on allocations which did not come from the epoch allocator
 * without tracking down all of our calls to an_malloc_free, and
 * keeping the interface the same.
 *
 * When the transaction is destroyed, decrement the ref count on the
 * epoch it was created in.
 *
 * On the next allocation, we will actually do the destruction of any
 * "destroyed" an_malloc_epochs. This is because in certain adserver
 * components we destroy transactions in threads other than the ones
 * they were created in, and because we currently have a tremendous
 * problem in our codebase with sending an imp_req's reply (which
 * calls transaction_close) before we've finished touching all memory
 * associated with that request.
 */

typedef void (an_malloc_epoch_cleanup_cb_t)(void *);

struct an_malloc_epoch_cleanup {
	an_malloc_epoch_cleanup_cb_t *cb;
	void *arg;
	SLIST_ENTRY(an_malloc_epoch_cleanup) linkage;
};

struct an_malloc_epoch {
	size_t offset; /** How far we are into the pool, from the head
			* of the an_malloc_epoch object, NOT from the
			* pool pointer.  */

	/* Stats */
	size_t num_allocations;
	size_t allocation_size;

	time_t created_timestamp; /** When this epoch was created, for debugging purposes */

	size_t transactions_created; /** Number of transactions which
				      * were created when this epoch
				      * was the newest, total. */

	uint64_t ref_count; /** How many transactions were created when
			   * this epoch was the newest, and which are
			   * still active. */

	/**
	 * Stack of cleanup functions to call when this epoch is
	 * destroyed.
	 */
	SLIST_HEAD(an_malloc_epoch_cleanup_head, an_malloc_epoch_cleanup) cleanups;

	/**
	 * Entry for the free list or live epoch queue.
	 */
	STAILQ_ENTRY(an_malloc_epoch) linkage;

	char pool[]; /** The rest of the memory pool. */
};

struct an_malloc_state {
	/*
	 * If use_epoch_malloc is set to false, allow_epoch_malloc
	 * should also be set to false.
	 */
	bool use_epoch_malloc; /** Whether the epoch based allocator should be used in general */
	bool allow_epoch_malloc; /** True iff the thread can currently use pool allocation. */
};

/* Can't use dot initializers here because g++ complains in CPP files which include this. */
AN_CC_UNUSED static struct an_malloc_state an_malloc_unknown_state = {
	false,
	false
};

struct an_malloc_pool {
	struct an_malloc_state state;
	struct an_malloc_epoch *epoch;
};

/**
 * Open a new transaction on the current thread. Callers should save a
 * reference to the returned epoch, since it must be provided in order
 * to close the transaction.
 *
 * @warn This function MUST be called as soon as possible on the request
 * path.
 *
 * Any allocations made before it (on a thread which is not the
 * batches thread) must either:
 * - use types which are not opted into the
 *       pool allocator (.use_pool_allocation = false),
 * - OR, must use an_calloc_object (an_malloc_region, etc.) with the
 *       optional named arguments, ex.:
 *         an_calloc_object(type, .non_pool = true);
 * - OR, must use normal malloc/free directly.
 *
 * Non-adherence to this will result in the weirdest corruption bugs
 * you have ever seen.
 */
AN_CC_WARN_UNUSED_RESULT struct an_malloc_epoch *an_malloc_transaction_open();

/**
 * Open a fresh allocation pool and return an epoch in that pool.
 *
 * This function enables pool allocation, and opens a transaction in
 * the same call; it should be called when possible (transactions are
 * always correctly nested).
 *
 * @param enable whether pool allocation should be immediately enabled
 * locally.
 */
AN_CC_WARN_UNUSED_RESULT struct an_malloc_pool an_malloc_pool_open(bool enable);

/**
 * Close a transaction on the epoch provided. This should be seen as
 * destroying all memory associated with that transaction. Any memory
 * access to request-level variables after this call should be
 * considered a use-after-free.
 *
 * @param created_in the epoch which
 * was provided by the corresponding call to
 * an_malloc_transaction_open.
 */
void an_malloc_transaction_close(struct an_malloc_epoch *created_in);

/**
 * Close the current epoch, and restores global pool allocation to its
 * previous setting.
 */
void an_malloc_pool_close(const struct an_malloc_pool *);

/**
 * @return whether or not the current thread was previously preventing
 * usage of epoch_malloc, and sets the value to @a new_val
 */
bool an_malloc_set_epoch_usage(bool new_val);

/**
 * @brief Set the value of the current thread's usage of epoch_malloc.
 * @warn You should probably only use this from the @a FORBID_EPOCH_USAGE macro.
 */
void an_malloc_restore_epoch_usage(bool *old_value);

/**
 * @brief Set the number of epoch's each thread will cache before freeing the
 * memory
 */
void an_malloc_pool_set_reclaimed_epochs_limit(size_t new_cache_size);

/**
 * @return the current value of the current thread's an_malloc_state.
 */
AN_CC_WARN_UNUSED_RESULT struct an_malloc_state an_malloc_gather_state(void);

/**
 * Set the current thread's an_malloc_state to @a state. You should
 * probably have saved this value from @a an_malloc_gather state.
 */
void an_malloc_restore_state(struct an_malloc_state state);
void an_malloc_restore_state_cleanup(struct an_malloc_state *state);
struct an_malloc_state an_malloc_fas_state(struct an_malloc_state state);

/**
 * Add a cleanup function to @a epoch_to_attach to, which will be called when it is destroyed.
 * Useful for types which cannot be opted into the pool allocator (looking at you, libevent).
 */
#define an_pool_adopt(CB, ARG, EPOCH) an_pool_adopt_internal(AN_CC_CAST_CB(void, (CB), (ARG)), (ARG), (EPOCH))
void an_pool_adopt_internal(an_malloc_epoch_cleanup_cb_t *, void *arg, struct an_malloc_epoch *epoch_to_attach_to);

/**
 * This is a fun macro.
 *
 * This sets a variable equal to the current
 * value of the state of the an_malloc epoch allocator, and calls it
 * "an_malloc_old_epoch_prevention_value".  When this variable goes
 * out of scope, GCC will generate code that calls
 * an_malloc_restore_epoch_usage, with a pointer to
 * an_malloc_old_epoch_prevention_value as an argument. That means
 * that when this macro goes out of scope, we can guarantee that the
 * allocator state is what it was when the macro was called, and that
 * for the intervening period, the allocator was set NOT to use epoch
 * allocation--all allocations in this interval MUST be managed
 * manually.
 *
 * This is an alternative to using a lock-like interface, which would get messy.
 * for instance:
 * an_malloc_set_epoch_usage(true);
 * ... unsafe code ...
 * an_malloc_set_epoch_usage(false);
 *
 * @warn This macro works on the local scope and _all_scopes_created_by_it_
 * This is crucial to ensure correctness.
 * ex: if function A calls FORBID_EPOCH_IN_SCOPE and then calls
 * function B, no allocations in function B will use epoch allocation.
 *
 * https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#Common-Variable-Attributes
 */
#define AN_MALLOC_FORBID_EPOCH_IN_SCOPE AN_CC_UNUSED bool an_malloc_old_epoch_prevention_value __attribute__((__cleanup__(an_malloc_restore_epoch_usage))) = an_malloc_set_epoch_usage(false)
#define AN_MALLOC_ENSURE_EPOCH_IN_SCOPE AN_CC_UNUSED bool an_malloc_old_epoch_enabling_value __attribute__((__cleanup__(an_malloc_restore_epoch_usage))) = an_malloc_set_epoch_usage(true)

/**
 * Do some work (fn) with a given an_malloc_state. Generally useful
 * for places in the codebase in which you are executing a callback,
 * (request queue, cookiemonster), and wish to run the callback with
 * the same an_malloc_state (which allocator to use) as when the code
 * that originated the callback was running.
 *
 * Ex.:
 * - tag_handler makes a bunch of allocations into a pool.
 * - creates and sends a bid request, but saves its allocator state in the request_queue_cb.
 * - when we get responses and wish to call the original callback set by tag_handler, we would like to know which allocator to use.
 * - since the state was saved, we call AN_TRANSFER_CONTROL(callback_function, the_state_we_saved_in_tag_handler).
 */
#define AN_EXECUTE_CALLBACK(fn, original_state)				\
	({ AN_CC_UNUSED struct an_malloc_state an_malloc_current_state __attribute__((__cleanup__(an_malloc_restore_state_cleanup))) = an_malloc_gather_state(); \
	an_malloc_restore_state((original_state));	               \
	(fn);							       \
	})

/**
 * This struct should not be used except by the AN_MALLOC_STATE macro.
 */
struct an_malloc_state_iterator {
	int iterator;
	struct an_malloc_state current_state;
};

/**
 * Execute a block of code with a given an_malloc_state. Similar to
 * AN_EXECUTE_CALLBACK, but with as much code as you want (in a block), not just a
 * function.
 */
#define AN_MALLOC_STATE(original_state)		\
	for (struct an_malloc_state_iterator malloc_state_nonce = {.iterator = 0, .current_state = an_malloc_fas_state((original_state))}; \
	     malloc_state_nonce.iterator < 1;				\
	     malloc_state_nonce.iterator++, an_malloc_restore_state(malloc_state_nonce.current_state))


#define AN_MALLOC_STATE_UNKNOWN an_malloc_unknown_state

void an_malloc_token_metrics_print(struct evbuffer *buf);
void an_malloc_allocator_metrics_print(struct evbuffer *buf);

void *an_acf_malloc(const void *ctx, size_t size, void *return_addr);

void *an_acf_calloc(const void *ctx, size_t nmemb, size_t size, void *return_addr);

void *an_acf_realloc(const void *ctx, void *address, size_t size_from, size_t size_to, void *return_addr);

void an_acf_free(const void *ctx, void *ptr, void *return_addr);

struct an_acf_allocator {
	const struct an_allocator allocator;
	an_malloc_token_t an_token;
};

#define AN_ACF_ALLOCATOR_BASE 			\
{ 						\
	.allocator = { 				\
		.malloc = an_acf_malloc, 	\
		.calloc = an_acf_calloc, 	\
		.realloc = an_acf_realloc, 	\
		.free = an_acf_free 		\
	} 					\
}

#endif /* _AN_MALLOC_H */
