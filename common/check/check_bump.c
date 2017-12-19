#include <check.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "common/memory/bump.h"

START_TEST(smoke_private)
{
	struct an_bump_private *private;
	uintptr_t last = 0;
	size_t allocated = 0;
	size_t capacity = 1UL << 23;

	private = an_bump_private_create(capacity, NULL);
	{
		void *first = an_bump_alloc(private, 0, 0);
		capacity -= 1 + (uintptr_t)first - (uintptr_t)private;
	}

	while (allocated < capacity) {
		size_t request;
		void *alloc;

		request = 1 + (1024.0 * random() / RAND_MAX);
		if (request > capacity - allocated) {
			request = capacity - allocated;
		}

		alloc = an_bump_alloc(private, request, 0);
		fail_if(alloc == NULL);
		fail_if((uintptr_t)alloc < last);
		last = (uintptr_t)alloc + request;
		allocated += request;
	}
} END_TEST

START_TEST(smoke_shared)
{
	struct an_bump_shared *shared;
	uintptr_t last = 0;
	size_t allocated = 0;
	size_t capacity = 1UL << 22;

	shared = an_bump_shared_create(capacity, NULL);
	{
		void *first = an_bump_alloc(shared, 0, 0);
		capacity -= 1 + (uintptr_t)first - (uintptr_t)shared;
	}

	while (allocated < capacity) {
		size_t request;
		void *alloc;

		request = 1 + (1024.0 * random() / RAND_MAX);
		if (request > capacity - allocated) {
			request = capacity - allocated;
		}

		alloc = an_bump_alloc(shared, request, 0);
		fail_if(alloc == NULL);
		fail_if((uintptr_t)alloc < last);
		last = (uintptr_t)alloc + request;
		allocated += request;
	}
} END_TEST

int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("common/bump");
	TCase *tc = tcase_create("test_bump");

	tcase_add_test(tc, smoke_private);
	tcase_add_test(tc, smoke_shared);

	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_bump.xml");
	srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
