#ifndef MEMORY_RESERVE_H
#define MEMORY_RESERVE_H
#include <ck_pr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

extern uint64_t an_memory_reserve_page_size;

/**
 * @brief Initialise the memory_reserve subsystem to grab a VMA
 * @a vma_size contiguous bytes.  First call wins, later calls are
 * no-ops.
 *
 * Asserts out on failure.
 */
void an_memory_reserve_init(size_t vma_size);

/**
 * @brief Attempt to reserve a chunk of address space in our VMA of
 * @a size bytes with @a alignment (e.g., 32) byte alignment.
 * @return the lowest address in the chunk on success, NULL on failure.
 *
 * Calls init with a default VMA size if necessary.
 */
void *an_memory_reserve(size_t size, size_t alignment);

/**
 * @brief Determine if @a address is allocated *strictly* in the VMA.
 *
 * @return True iff the address is strictly contained in the VMA.
 */
static bool an_memory_reserve_reserved(uintptr_t address);

static inline bool
an_memory_reserve_reserved(uintptr_t address)
{
	/* Base is written first, read last */
	extern uint64_t an_memory_reserve_vma_base;
	extern uint64_t an_memory_reserve_vma_size;

	uint64_t size = ck_pr_load_64(&an_memory_reserve_vma_size);
	uintptr_t base = ck_pr_load_64(&an_memory_reserve_vma_base);

	return (address - base) < size;
}
#endif /*! MEMORY_RESERVE_H */
