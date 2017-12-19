#include <assert.h>
#include <sys/mman.h>

#include "common/memory/map.h"
#include "common/memory/reserve.h"

#ifndef MADV_DODUMP
#define MADV_DODUMP 17
#endif

size_t
an_memory_map(void *address, size_t at_least, size_t at_most)
{
	size_t mask;
	size_t rounded_size;
	void *ret;

	if (an_memory_reserve_reserved((uintptr_t)address) == false) {
		return 0;
	}

	mask = ck_pr_load_64(&an_memory_reserve_page_size) - 1;
	rounded_size = (at_least + mask) & ~mask;
	if (rounded_size < at_least || rounded_size > at_most) {
		return 0;
	}

	ret = mmap(address, rounded_size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
	    -1, 0);

	if (ret == MAP_FAILED) {
		return 0;
	}

	(void)madvise(ret, rounded_size, MADV_DODUMP);
	assert(ret == address);
	if (rounded_size < at_most) {
		return rounded_size;
	}

	return at_most;
}
