#include <assert.h>
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "an_allocator.h"

#ifdef __APPLE__
#include <malloc/malloc.h>
#define malloc_usable_size malloc_size
#else
#include <malloc.h>
#endif

static struct allocator_stats {
	size_t n_malloc;
	size_t n_calloc;
	size_t n_realloc;
	size_t n_free;
} allocator_stats;

static void *
check_calloc(const void *ctx, size_t nmemb, size_t size, void *return_addr)
{
	(void)ctx;
	(void)return_addr;

	allocator_stats.n_calloc++;
	return calloc(nmemb, size);
}

static void *
check_malloc(const void *ctx, size_t size, void *return_addr)
{
	(void)ctx;
	(void)return_addr;

	allocator_stats.n_malloc++;
	return malloc(size);
}

static void *
check_realloc(const void *ctx, void *address, size_t size_from, size_t size_to, void *return_addr)
{
	(void)ctx;
	(void)size_from;
	(void)return_addr;

	allocator_stats.n_realloc++;
	return realloc(address, size_to);
}

static void
check_free(const void *ctx, void *ptr, void *return_addr)
{
	(void)ctx;
	(void)return_addr;

	allocator_stats.n_free++;
	free(ptr);
}

static void
test_allocator(const struct an_allocator *a)
{
	int *data;

	data = AN_MALLOC(a, 10);
	ck_assert(data != NULL);
	ck_assert(malloc_usable_size(data) > 0);
	AN_FREE(a, data);

	data = AN_CALLOC(a, 10, sizeof(int));
	ck_assert(data != NULL);
	ck_assert(malloc_usable_size(data) > 0);
	for (size_t i = 0; i < 10; i++) {
		ck_assert(data[i] == 0);
	}
	AN_FREE(a, data);


	data = AN_MALLOC(a, 10);
	ck_assert(data != NULL);
	size_t original_usable_size = malloc_usable_size(data);
	ck_assert(original_usable_size > 0);

	data = AN_REALLOC(a, data, 10, sysconf(_SC_PAGESIZE) * 4);
	ck_assert(data != NULL);
	ck_assert(malloc_usable_size(data) > original_usable_size);

	AN_FREE(a, data);

	char *s = AN_STRDUP(a, "testing");
	ck_assert(s != NULL);
	ck_assert(strcmp(s, "testing") == 0);
	AN_FREE(a, s);

	s = AN_STRNDUP(a, "testing", 4);
	ck_assert(s != NULL);
	ck_assert(strcmp(s, "test") == 0);
	AN_FREE(a, s);

	s = AN_STRDUP(a, NULL);
	ck_assert(s == NULL);

	s = AN_STRNDUP(a, NULL, 4);
	ck_assert(s == NULL);
}

START_TEST(default_allocator_test)
{
	test_allocator(an_default_allocator());
}
END_TEST

START_TEST(custom_allocator_test)
{
	struct an_allocator check_allocator = {
		.malloc = check_malloc,
		.calloc = check_calloc,
		.realloc = check_realloc,
		.free = check_free
	};

	memset(&allocator_stats, 0, sizeof(allocator_stats));

	test_allocator(&check_allocator);

	ck_assert(allocator_stats.n_calloc > 0);
	ck_assert(allocator_stats.n_malloc > 0);
	ck_assert(allocator_stats.n_realloc > 0);
	ck_assert(allocator_stats.n_free > 0);
}
END_TEST

int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("an_allocator");
	TCase *tc = tcase_create("test_an_allocator");

	tcase_add_test(tc, default_allocator_test);
	tcase_add_test(tc, custom_allocator_test);

	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_allocator.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
