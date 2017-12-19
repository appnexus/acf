#ifndef MEMORY_FREELIST_H
#define MEMORY_FREELIST_H
/*
 * Statically allocated fixed-size free list of RTBRed pointers.
 *
 * A pointer enters the free list on the limbo FIFO, with a timestamp
 * equal to ~now.  When RTBR tells us that nothing refers to that
 * pointer (all read-side sections started after the pointer's
 * timestamp), the pointer leaves the FIFO and enters the stack of
 * entries that can actually be reused.
 *
 * A pointer leaves the free list when a caller pops off the free
 * list.  If that is successful, the caller also receives a free list
 * entry.  It should use that entry when pushing the item back on the
 * free list: entries are allocated statically to prevent runaway
 * resource allocation.
 *
 * N.B., freelists are global singletons even for thread-local
 * resources.  The idea is that the unit of freelist allocation should
 * be coarse enough to perform thread-caching; once we recycle a
 * coarse allocation, we should push it back to the global pool to
 * keep fragmentation in check.
 */

#include <ck_cc.h>
#include <ck_stack.h>
#include <ck_fifo.h>
#include <stddef.h>
#include <stdint.h>

struct an_freelist_entry {
	struct ck_fifo_mpmc_entry fifo_entry;
	struct ck_stack_entry stack_entry;
	void *value;
	uint64_t deletion_timestamp;
} CK_CC_CACHELINE;

struct an_freelist {
	struct ck_stack stack CK_CC_ALIGN(16);
	const uint64_t n_elem;
	uint64_t used_elem;
	struct ck_fifo_mpmc fifo;
	struct an_freelist_entry *const entries;
};

#define AN_FREELIST(LINKAGE, NAME, N_ELEM)				\
	static struct an_freelist_entry NAME##_freelist_entries[(N_ELEM) + 1]; \
	LINKAGE struct an_freelist NAME = {				\
		.stack = CK_STACK_INITIALIZER,				\
		.n_elem = (N_ELEM) + 1,					\
		.used_elem = 1,						\
		.fifo = {						\
			.head = {					\
				.pointer = &NAME##_freelist_entries[0].fifo_entry \
			},						\
			.tail = {					\
				.pointer = &NAME##_freelist_entries[0].fifo_entry \
			}						\
		},							\
		.entries = &NAME##_freelist_entries[0]			\
	};

/**
 * @brief allocate a new entry in the free list.
 * @return an entry on success, or NULL if the free list has reached capacity.
 */
struct an_freelist_entry *
an_freelist_register(struct an_freelist *);

/**
 * @brief attempt to pop an element off the free list
 * @param OUT_entry the free list entry associated with the return value
 *  on success, NULL otherwise.
 * @return the element on success, NULL on failure.
 */
void *an_freelist_pop(struct an_freelist *, struct an_freelist_entry **OUT_entry);

/**
 * @brief schedule @a value for re-use once all current read-side sections have terminated.
 */
void an_freelist_shelve(struct an_freelist *, struct an_freelist_entry *entry, void *value);

/**
 * @brief Mark @a value as immediately ready for re-use.
 */
void an_freelist_push(struct an_freelist *, struct an_freelist_entry *entry, void *value);
#endif /* !MEMORY_FREELIST_H */
