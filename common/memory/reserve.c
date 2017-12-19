#include <assert.h>
#include <ck_pr.h>
#include <ck_spinlock.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common/memory/reserve.h"

#ifndef MADV_DONTDUMP
#define MADV_DONTDUMP 16
#endif

#define MEMORY_RESERVE_VMA_SIZE (1ULL << 40)
#define MEMORY_RESERVE_VMA_ALIGNMENT (1ULL << 30)

extern uint64_t an_memory_reserve_vma_base;
extern uint64_t an_memory_reserve_vma_size;

static ck_spinlock_t lock;
uint64_t an_memory_reserve_vma_base = 0;
uint64_t an_memory_reserve_vma_size = 0;
size_t an_memory_reserve_page_size = 0;
static uint64_t alloc_pointer = 0;

static void
map(size_t size, size_t alignment)
{
	size_t mask = alignment - 1;
	size_t round_size = (size + mask) & ~mask;
	size_t bumped_size = round_size + alignment;
	size_t alloc_size = (bumped_size + mask) & ~mask;
	uintptr_t mapped_end;
	uintptr_t ret_end;
	void *mapped;
	void *ret;

	assert(round_size >= size);
	assert(bumped_size >= round_size);
	assert(alloc_size >= bumped_size);

	mapped = mmap(NULL, round_size,
	    PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
	    -1, 0);
	if (mapped != MAP_FAILED && ((uintptr_t)mapped & mask) != 0) {
		ret = mapped;
		goto success;
	}

	if (mapped != MAP_FAILED) {
		int r;

		r = munmap(mapped, round_size);
		assert(r == 0);
	}

	mapped = mmap(NULL, alloc_size,
	    PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
	    -1, 0);
	assert(mapped != MAP_FAILED);
	mapped_end = (uintptr_t)mapped + alloc_size;

	ret = (void *)(((uintptr_t)mapped + mask) & ~mask);
	assert((uintptr_t)ret >= (uintptr_t)mapped);

	if ((uintptr_t)ret != (uintptr_t)mapped) {
		size_t extra = (uintptr_t)ret - (uintptr_t)mapped;
		int r;

		r = munmap(mapped, extra);
		assert(r == 0);
	}

	ret_end = (uintptr_t)ret + round_size;
	assert(ret_end <= mapped_end);
	if (ret_end != mapped_end) {
		size_t extra = mapped_end - ret_end;
		int r;

		r = munmap((void *)ret_end, extra);
		assert(r == 0);
	}

success:
	(void)madvise(ret, round_size, MADV_DONTDUMP);
	ck_pr_store_64(&an_memory_reserve_vma_base, (uint64_t)ret);
	ck_pr_store_64(&an_memory_reserve_vma_size, round_size);
	return;
}

void
an_memory_reserve_init(size_t vma_size)
{
	size_t alignment = MEMORY_RESERVE_VMA_ALIGNMENT;
	size_t page_size;

	if (vma_size == 0) {
		vma_size = MEMORY_RESERVE_VMA_SIZE;
	}

	{
		long ret;

		ret = sysconf(_SC_PAGE_SIZE);
		assert(ret > 0);
		page_size = (size_t)ret;
		ck_pr_store_64(&an_memory_reserve_page_size, page_size);
	}

	if ((alignment & (alignment - 1)) != 0 ||
	    alignment < page_size) {
		alignment = page_size;
	}

	if (ck_pr_load_64(&an_memory_reserve_vma_size) != 0) {
		return;
	}

	ck_spinlock_lock(&lock);
	if (ck_pr_load_64(&an_memory_reserve_vma_size) != 0) {
		goto out;
	}

	map(vma_size, MEMORY_RESERVE_VMA_ALIGNMENT);
	alloc_pointer = an_memory_reserve_vma_base;

out:
	ck_spinlock_unlock(&lock);
	return;
}

void *
an_memory_reserve(size_t size, size_t alignment)
{
	uintptr_t current;
	uintptr_t next;
	uintptr_t ret;
	uint64_t page_size;
	uint64_t vma_base;
	uint64_t vma_size;
	size_t mask;

	if (ck_pr_load_64(&an_memory_reserve_vma_size) == 0) {
		an_memory_reserve_init(MEMORY_RESERVE_VMA_SIZE);
	}

	page_size = ck_pr_load_64(&an_memory_reserve_page_size);
	vma_size = ck_pr_load_64(&an_memory_reserve_vma_size);
	vma_base = ck_pr_load_64(&an_memory_reserve_vma_base);

	/*
	 * convert power of two x to x - 1, and get a valid mask for
	 * non-powers-of two (corresponds to the largest power of two
	 * that divides the alignment value).
	 */
	mask = page_size - 1;
	if (alignment > page_size) {
		mask |= (alignment ^ (alignment - 1)) >> 1;
	}

	current = ck_pr_load_64(&alloc_pointer);
	while (1) {
		ret = (current + mask) & ~mask;
		if (ret < current) {
			return NULL;
		}

		next = ret + size;
		if (next < ret || (next - vma_base) > vma_size) {
			return NULL;
		}

		if (ck_pr_cas_64_value(&alloc_pointer, current, next,
		    &current)) {
			return (void *)ret;
		}
	}

	return NULL;
}
