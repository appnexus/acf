#include <assert.h>
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "an_array.h"

struct node {
	ssize_t value;
};

AN_ARRAY(node, test);
AN_ARRAY_PRIMITIVE(int, test2);

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

START_TEST(stack_generic)
{
	AN_ARRAY_INSTANCE(test) array;
	struct node entry[1024];
	ssize_t i;
	struct node *cursor;

	AN_ARRAY_INIT(test, &array, 1);
	for (i = 0; i < 1024; i++) {
		entry[i].value = i;
		AN_ARRAY_PUSH(test, &array, &entry[i]);
	}

	i = 0;
	AN_ARRAY_FOREACH(&array, cursor) {
		fail_if((uintptr_t)cursor->value != (uintptr_t)i++);
	}

	i = 0;
	AN_ARRAY_FOREACH(&array, cursor) {
		fail_if((uintptr_t)cursor->value != (uintptr_t)i++);
	}

	for (i = 1024; i > 0; i--) {
		unsigned int n_entries;
		struct node *n;

		n = AN_ARRAY_POP(test, &array, &n_entries);
		fail_if(n_entries != i);
		fail_if((uintptr_t)n->value != (uintptr_t)i - 1);
	}

	AN_ARRAY_PUSH(test, &array, &entry[0]);
	fail_if(array.capacity != 1024);

	AN_ARRAY_DEINIT(test, &array);
}
END_TEST

START_TEST(stack)
{
	AN_ARRAY_INSTANCE(test2) array;
	int i;

	AN_ARRAY_INIT(test2, &array, 1);
	for (i = 0; i < 1024; i++) {
		AN_ARRAY_PUSH(test2, &array, &i);
	}

	for (i = 1024; i > 0; i--) {
		unsigned int n_entries;
		int *value = 0;

		value = AN_ARRAY_POP(test2, &array, &n_entries);
		fail_if((int)n_entries != i);
		fail_if(*value != i - 1);
	}

	int x = 0;
	AN_ARRAY_PUSH(test2, &array, &x);
	fail_if(array.capacity != 1024);

	AN_ARRAY_DEINIT(test2, &array);
}
END_TEST

START_TEST(find_element)
{
	AN_ARRAY_INSTANCE(test2) array;
	int i;

	AN_ARRAY_INIT(test2, &array, 1);
	for (i = 0; i < 1024; i++) {
		AN_ARRAY_PUSH(test2, &array, &i);
	}
	for (i = 0; i < 1024; i++) {
		fail_if(!AN_ARRAY_MEMBER(test2, &array, &i), "Could not find a valid member");
	}

	int x = -1;
	fail_if(AN_ARRAY_MEMBER(test2, &array, &x), "Found a member that does not belong");
	AN_ARRAY_POP(test2, &array, NULL); // remove one to test the odd element count
	fail_if(AN_ARRAY_LENGTH(test2, &array) != 1023,"Pop failed ?");
	for (i = 0; i < 1023; i++) {
		fail_if(!AN_ARRAY_MEMBER(test2, &array, &i), "Could not find a valid member");
	}
	fail_if(AN_ARRAY_MEMBER(test2, &array, &x), "Found a member that does not belong");

	AN_ARRAY_DEINIT(test2, &array);
}
END_TEST

START_TEST(remove_element)
{
	AN_ARRAY_INSTANCE(test2) array;
	int i;

	AN_ARRAY_INIT(test2, &array, 1);
	for (i = 0; i < 1024; i++)
		AN_ARRAY_PUSH(test2, &array, &i);

	for (i = 0; i < 1024; i++) {
		fail_if(!AN_ARRAY_REMOVE(test2, &array, &i) , "Could not find a valid member to remove");
		fail_if(AN_ARRAY_MEMBER(test2, &array, &i), "Found a deleted member");
		fail_if((int)AN_ARRAY_LENGTH(test2, &array) != 1023-i,"Remove member failed");
	}

	AN_ARRAY_DEINIT(test2, &array);
}
END_TEST

START_TEST(remove_order)
{
	AN_ARRAY_INSTANCE(test2) array;
	int i;
	int to_remove = 876;
	int to_remove_order = 450;
	int last;
	int prev_length;

	AN_ARRAY_INIT(test2, &array, 1);
	for (i = 0; i < 1024; i++) {
		AN_ARRAY_PUSH(test2, &array, &i);
	}

	/* Confirm order */
	int prev = *AN_ARRAY_VALUE(test2, &array, 0);
	int length = AN_ARRAY_LENGTH(test2, &array);
	fail_if(prev != 0, "0th element was not 0");
	for (i = 1; i < length; i++) {
		int val = *AN_ARRAY_VALUE(test2, &array, i);
		fail_if(val != (prev + 1), "Element [%d] = %d is out of order", i, val);
		prev = val;
	}

	/* Confirm AN_ARRAY_REMOVE removes nth element and swaps in last element */
	prev_length = AN_ARRAY_LENGTH(test2, &array);
	last = *AN_ARRAY_VALUE(test2, &array, prev_length - 1);
	AN_ARRAY_REMOVE(test2, &array, &to_remove);
	length = AN_ARRAY_LENGTH(test2, &array);
	fail_if(length != (prev_length - 1), "Did not remove element, length not modified");
	fail_if(*AN_ARRAY_VALUE(test2, &array, to_remove) == to_remove, "Did not remove element, element exists");
	fail_if(*AN_ARRAY_VALUE(test2, &array, to_remove) != last, "Last element not swapped in");

	/* Confirm AN_ARRAY_REMOVE_IN_ORDER removes mth element and preserves order */
	prev_length = AN_ARRAY_LENGTH(test2, &array);
	AN_ARRAY_REMOVE_IN_ORDER(test2, &array, &to_remove_order);
	length = AN_ARRAY_LENGTH(test2, &array);
	fail_if(length != (prev_length - 1), "Did not remove element, length not modified");
	fail_if(*AN_ARRAY_VALUE(test2, &array, to_remove_order) == to_remove_order,
	    "Did not remove element, element exists");
	fail_if(*AN_ARRAY_VALUE(test2, &array, to_remove_order - 1) != (to_remove_order - 1),
	    "Previous element [%d] = %d is incorrect", to_remove_order - 1, to_remove_order - 1);
	fail_if(*AN_ARRAY_VALUE(test2, &array, to_remove_order) != (to_remove_order + 1),
	    "Next element [%d] = %d is incorrect", to_remove_order, to_remove_order + 1);


	AN_ARRAY_DEINIT(test2, &array);
}
END_TEST

START_TEST(resize)
{
	AN_ARRAY_INSTANCE(test) array2;

	AN_ARRAY_INIT(test, &array2, 16);

	fail_if(AN_ARRAY_LENGTH(test, &array2) != 0);
	AN_ARRAY_RESIZE(test, &array2, 2);
	fail_if(AN_ARRAY_LENGTH(test, &array2) != 0);
}
END_TEST

START_TEST(allocator)
{
	struct an_allocator check_allocator = {
		.malloc = check_malloc,
		.calloc = check_calloc,
		.realloc = check_realloc,
		.free = check_free
	};

	memset(&allocator_stats, 0, sizeof(allocator_stats));

	an_array_set_allocator(&check_allocator);

	AN_ARRAY_INSTANCE(test2) array;
	AN_ARRAY_INIT(test2, &array, 2);
	AN_ARRAY_RESIZE(test2, &array, 128);
	AN_ARRAY_DEINIT(test2, &array);

	ck_assert(allocator_stats.n_calloc + allocator_stats.n_malloc > 0);
	ck_assert(allocator_stats.n_realloc > 0);
	ck_assert(allocator_stats.n_free > 0);

	an_array_set_allocator(an_default_allocator());
}
END_TEST

int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("an_array");
	TCase *tc = tcase_create("test_an_array");

	tcase_add_test(tc, allocator);
	tcase_add_test(tc, stack);
	tcase_add_test(tc, stack_generic);
	tcase_add_test(tc, find_element);
	tcase_add_test(tc, remove_element);
	tcase_add_test(tc, remove_order);
	tcase_add_test(tc, resize);

	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_array.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
