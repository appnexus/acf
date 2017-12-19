/**
 * An object-level software transactional memory for single writers.
 *
 * This is an STM in the sense that reads are atomic with respect to
 * write transactions: within a (read-only) transaction, we will
 * always have a consistent view of the heap.  There is only one
 * writer, so it's easy to make sure that it too gets a consistent
 * view of the heap.
 *
 * The implementation is wait-free for readers, and atomic-free for
 * the writer; synchronisation happens via SMR, which is probably not
 * lock-free for the writer, but still non-blocking for readers.
 *
 * The "natural" way to do STM in C is at the word level, with
 * automatically delayed calls to free.  We could have made it work,
 * but the overhead would be awful: we'd have to instrument every
 * single read.
 *
 * Instead, this STM intercepts at the logical object level: we keep
 * multiple versions of objects, not of the heap itself.  We get to
 * add in-band annotations to objects (instead of lock words), which
 * isn't really feasible for raw memory, and readers/writers only have
 * to check for duplicates when accessing an object, not each of its
 * members.  In return, freeing resources is a bit trickier.  We can't
 * automatically delay calls to free: transactional writes may depend
 * on correct frees or otherwise change the object and make the free
 * wrong.  Instead, we have to delay calls to *object destructors*.
 * These may be straight an_free, but probably aren't for objects
 * that contain private pointers (e.g., hash tables, int sets or
 * an_array).
 *
 * The representation favours for frequent operations on the fast
 * (read) path by abusing bitpacking on addresses: ABIs usually
 * guarantee natural alignment for built-in types, so we should be
 * able to assume alignment to (at least) 4 bytes.  We check for that
 * when allocating a shadow (copy) object, and proceed to encode
 * metadata in the two least significant bits of the pointer.
 */

#ifndef AN_SSTM_H
#define AN_SSTM_H
#include <ck_pr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/an_cc.h"
#include "common/an_hook.h"
#include "common/an_malloc.h"
#include "common/an_smr.h"
#include "common/an_thread.h"
#include "common/util.h"

/**
 * Low tag:
 * NULL: empty
 * 0b10: only visible from writer
 * 0b?1: visible from readers if reading from commits
 *
 * The pointer itself is just *after* the sstm_record header (i.e.,
 * straight to the shadow object).
 */
struct an_sstm_linkage {
	union {
		void *link;
		uintptr_t bits;
	};
};

#define DEFINE_SSTM_TYPE(NAME, BASE)			\
	struct NAME {					\
		BASE an_sstm_data;			\
		struct an_sstm_linkage an_sstm_link;	\
	}

#define SSTM_CELL(BASE) DEFINE_SSTM_TYPE(, BASE)

/**
 * vtable for single-writer STM ops (only writer-side).
 */
struct an_sstm_ops {
	size_t shadow_size; /* # bytes to reserve for the shadow copy. */
	size_t release_size; /* # bytes to reserve for storage release metadata. */
	/*
	 * init_shadow: initialise the shadow copy. memcpy if NULL.
	 */
	void (*init_shadow)(void *shadow, const void *original);
	/*
	 * thaw_shadow: prepare the shadow copy for writes. no-op if NULL.
	 */
	void (*thaw_shadow)(void *shadow);
	/*
	 * freeze_shadow: turn the shadow copy into the read-only
	 * format readers expect.  During commit, readers will read
	 * from shadow.  no-op if NULL.
	 */
	void (*freeze_shadow)(void *shadow);
	/*
	 * pre_release: stash some information about the original
	 * object in the release buffer.  memcpy if NULL.
	 */
	void (*pre_release)(void *release, const void *original);
	/*
	 * write shadow copy to the original.  memcpy if NULL.
	 */
	void (*commit_shadow)(void *original, const void *shadow);
	/*
	 * release any resource associated only with the previous
	 * value of the object.  no-op if NULL.
	 */
	void (*release)(void *release);
	/*
	 * an_malloc token for sstm records of that type. (auto-created)
	 */
	struct an_malloc_type type;
	an_malloc_token_t token;
};

/*
 * My kingdom for local macros.
 */
#define AN_SSTM_SUBTYPE(TYPE) __typeof__(((TYPE *)NULL)->an_sstm_data)

#define AN_SSTM_INIT(TYPE, CB)						\
	.init_shadow = AN_CC_CAST_IF_COMPATIBLE(CB,			\
	    void (*)(AN_SSTM_SUBTYPE(TYPE) *, const AN_SSTM_SUBTYPE(TYPE) *), \
	    void (*)(void *, const void *))

#define AN_SSTM_THAW(TYPE, CB)						\
	.thaw_shadow = AN_CC_CAST_IF_COMPATIBLE(CB,			\
	    void (*)(AN_SSTM_SUBTYPE(TYPE) *),				\
	    void (*)(void *))

#define AN_SSTM_FREEZE(TYPE, CB)					\
	.freeze_shadow = AN_CC_CAST_IF_COMPATIBLE(CB,			\
	    void (*)(AN_SSTM_SUBTYPE(TYPE) *),				\
	    void (*)(void *))

#define AN_SSTM_PRE_RELEASE(TYPE, CB)					\
	.pre_release = AN_CC_CAST_IF_COMPATIBLE(CB,			\
	    void (*)(AN_SSTM_SUBTYPE(TYPE) *, const AN_SSTM_SUBTYPE(TYPE) *), \
	    void (*)(void *, const void *))

#define AN_SSTM_COMMIT(TYPE, CB)					\
	.commit_shadow = AN_CC_CAST_IF_COMPATIBLE(CB,			\
	    void (*)(AN_SSTM_SUBTYPE(TYPE) *, const AN_SSTM_SUBTYPE(TYPE) *), \
	    void (*)(void *, const void *))

#define AN_SSTM_RELEASE(TYPE, CB)					\
	.release = AN_CC_CAST_IF_COMPATIBLE(CB,				\
	    void (*)(AN_SSTM_SUBTYPE(TYPE) *),				\
	    void (*)(void *))

#define DEFINE_AN_SSTM_OPS(VAR, STRING, TYPE, ...)		\
	struct an_sstm_ops VAR = {				\
		.type = {					\
			.string = ("an_sstm:" STRING),		\
			.mode = AN_MEMORY_MODE_VARIABLE		\
		},						\
		.shadow_size = sizeof(AN_SSTM_SUBTYPE(TYPE)),	\
		.release_size = sizeof(AN_SSTM_SUBTYPE(TYPE)),	\
		__VA_ARGS__					\
	};							\
	AN_MALLOC_REGISTER(VAR.type, &VAR.token)

/**
 * @brief Register the current an_thread with the SSTM subsystem.
 */
void an_sstm_register_thread();

/**
 * @brief Remove the current an_thread from the SSTM subsystem.
 */
void an_sstm_deregister_thread();

/**
 * @brief Setup the current thread for reads. There must not be any
 * outstanding transaction in this thread.
 */
void an_sstm_open_read_transaction();

/**
 * @brief End the current thread's read transaction.  *Must* be paired
 * with opens, otherwise we may never close the SMR (read-side)
 * critical section.
 */
void an_sstm_close_read_transaction();

/**
 * @brief End the current thread's read transaction if necessary and
 * reopens it.
 */
void an_sstm_cycle_read_transaction();

/**
 * @brief Setup the current thread for writes. There must not be any
 * outstanding transaction in this thread.  Closed by committing;
 * rollback are YAGNI.
 */
void an_sstm_open_write_transaction(bool trivial);

/**
 * @brief Open X (a pointer) for reads, returning the pointer to read from.
 *
 * This is the identity if we're not in a transaction.
 */
#define an_sstm_read(X)							\
	((__typeof__((X)->an_sstm_data) const *)(an_sstm_read_impl((X), \
	    offset_of(__typeof__(*(X)), an_sstm_data),			\
	    offset_of(__typeof__(*(X)), an_sstm_link))))

#define an_sstm_pr_read(X)						\
	((__typeof__((X)->an_sstm_data) const *)			\
	    (an_sstm_read_impl(an_pr_load_ptr(&(X)),			\
		offset_of(__typeof__(*(X)), an_sstm_data),		\
		offset_of(__typeof__(*(X)), an_sstm_link))))

/**
 * @brief Open a pointer for reads without forcing constness.
 */
#define an_sstm_read_mutable(X)						\
	((__typeof__((X)->an_sstm_data) *)(an_sstm_read_impl((X),	\
	    offset_of(__typeof__(*(X)), an_sstm_data),			\
	    offset_of(__typeof__(*(X)), an_sstm_link))))

#define an_sstm_pr_read_mutable(X)					\
	((__typeof__((X)->an_sstm_data) *)				\
	    (an_sstm_read_impl(an_pr_load_ptr(&(X)),			\
	    offset_of(__typeof__(*(X)), an_sstm_data),			\
	    offset_of(__typeof__(*(X)), an_sstm_link))))

/**
 * @brief Open X (a pointer) for writes, returning the pointer to write to.
 *
 * an_sstm_read if we're not in a write transaction.
 */
#define an_sstm_write(X, OPS)						\
	((__typeof__((X)->an_sstm_data) *)(an_sstm_write_impl((X),	\
	    offset_of(__typeof__(*(X)), an_sstm_data),			\
	    offset_of(__typeof__(*(X)), an_sstm_link),			\
	    &(OPS))))

/**
 * @brief if we (non-trivially) opened an object for writes, commit
 * its current state back to the globally visible buffer: this is
 * necessary when zero-filled objects are invalid states.
 *
 * Publishing consists of two steps:
 *  1. copying the private buffer to the public one;
 *  2. calling the thaw method (if it exists) on the public buffer.
 */
void
an_sstm_write_back(void *dst, size_t dst_size,
    const struct an_sstm_linkage *src, struct an_sstm_ops *ops);

#define DEFINE_SSTM_WRITE(NAME, TYPE, OPS)			\
	static inline __typeof__(&((TYPE *)0)->an_sstm_data)	\
	NAME(TYPE *sstm_object)					\
	{							\
								\
		return an_sstm_write(sstm_object, (OPS));	\
	}							\
								\
	static AN_CC_UNUSED inline void				\
	NAME##_back(TYPE *sstm_object)				\
	{							\
								\
		an_sstm_write_back(&sstm_object->an_sstm_data,	\
		    sizeof(sstm_object->an_sstm_data),		\
		    &sstm_object->an_sstm_link, &(OPS));	\
		return;						\
	}

/**
 * @brief Schedule a cleanup callback until just after the transaction.
 *
 * The callback argument will be copied if the call is deferred (we
 * are in a write transaction).  The default is to infer the size of
 * the buffer to copy via sizeof.  This may not work for generic or
 * pointerful data structures, so an_sstm_call_size accepts a size argument.
 *
 * Other use cases (e.g., SMR callbacks) may not want a copy.
 * an_sstm_call passes a size of 0, which disables buffer copying:
 * the pointer is passed straight to the callback.
 *
 * an_sstm_smr_call will call_ptr if we're in a write transaction, and
 * regular SMR call otherwise.
 */
#define an_sstm_call_buf(CB, X) an_sstm_call_size((CB), (X), sizeof(*(X)))
#define an_sstm_call_size(CB, X, SIZE)					\
	an_sstm_call_internal(AN_CC_CAST_CB(void, (CB), (X)), (X), (SIZE))
#if defined(__COVERITY__)
#define an_sstm_call(FUNCTION, OBJECT)					\
	do {								\
		(FUNCTION)((OBJECT));					\
	} while (0);
#else
#define an_sstm_call(CB, X) an_sstm_call_internal(AN_CC_CAST_CB(void, (CB), (X)), (X), 0)
#endif
#define an_sstm_smr_call(X, CB) an_sstm_smr_call_internal((X), AN_CC_CAST_CB(void, (CB), (X)))

static void an_sstm_smr_call_internal(void *, void (*)(void *));

void an_sstm_call_internal(void (*cb)(void *), void *arg, size_t nbytes);

/**
 * @brief Atomically commit outstanding writes.
 * @return true if there was anything to commit.
 */
bool an_sstm_commit();

/**
 * @brief initialise an_sstm.
 */
void an_sstm_init_lib();

#define an_sstm_duplicate(TYPE, PTR, COUNT)	\
	an_sstm_duplicate_size((TYPE), (PTR), (COUNT), sizeof(*(PTR)))

#define an_sstm_duplicate_size(TYPE, PTR, COUNT, SIZE)	\
	do {								\
		if ((PTR) != NULL) {					\
			(PTR) = an_malloc_copy((TYPE), (PTR), (SIZE) * (COUNT)); \
		}							\
	} while (0)

/**
 * inline implementation noise
 */

/*
 * The NULL check will often be compiled away (due to earlier checks),
 * in which case this compiles down to two loads, mask, bit test and
 * conditional move.
 */
static AN_CC_UNUSED const void *
an_sstm_read_impl_impl(const void *object, const void *data, size_t link_offset)
{
	extern __thread uintptr_t an_sstm_mask;

	struct an_sstm_linkage *link;
	uintptr_t bits;
	const void *shadow;

	if (object == NULL) {
		return object;
	}

	link = (void *)((char *)object + link_offset);
	bits = link->bits;
	/*
	 * We tag on the bottom two bits (the ABI guarantees this
	 * works; see block comment header).
	 */
	shadow = (const void *)(bits & ~3ULL);

	return ((bits & an_sstm_mask) == 0) ? data : shadow;
}

static AN_CC_INLINE const void *
an_sstm_read_impl(const void *object, size_t data_offset, size_t link_offset)
{
	const void *data = (void *)((char *)object + data_offset);

	AN_HOOK(an_sstm, read) {
		return an_sstm_read_impl_impl(object, data, link_offset);
	}

	return (object == NULL) ? object : data;
}

/*
 * It may or may not be worth inlining these function.  Let the compiler decide.
 */
AN_CC_UNUSED static void *
an_sstm_write_impl(void *object, size_t data_offset, size_t link_offset, struct an_sstm_ops *ops)
{
	/*
	 * 0 normally, -1ULL if a write transaction is in progress.
	 */
	extern __thread uintptr_t an_sstm_writing;
	void *an_sstm_write_slow(void *, struct an_sstm_linkage *, struct an_sstm_ops *);

	void *data;
	struct an_sstm_linkage *link;

	/* NULL check and state check in one op. */
	if (((uintptr_t)object & an_sstm_writing) == 0) {
		return (void *)an_sstm_read_impl(object, data_offset, link_offset);
	}

	data = (void *)((char *)object + data_offset);
	link = (void *)((char *)object + link_offset);

	if (link->link != NULL) {
		return (void *)(link->bits & -4ULL);
	}

	return an_sstm_write_slow(data, link, ops);
}

AN_CC_UNUSED static void
an_sstm_smr_call_internal(void *object, void (*cb)(void *))
{
	extern __thread uintptr_t an_sstm_writing;

	if (an_sstm_writing != 0) {
		an_sstm_call(cb, object);
	} else {
		an_smr_call(object, cb);
	}

	return;
}
#endif /* !AN_SSTM_H */
