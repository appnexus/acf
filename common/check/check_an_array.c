#include <assert.h>
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common/an_array.h"
#include "common/an_malloc.h"
#include "common/common_types.h"

struct node {
	ssize_t value;
};

AN_ARRAY(node, test);
AN_ARRAY_PRIMITIVE(int, test2);
AN_ARRAY_DEFINE_COPY_INT_SET(test2)

START_TEST(stack_generic)
{
	AN_ARRAY_INSTANCE(test) array;
	struct node entry[1024];
	ssize_t i;
	struct node *cursor;

	AN_ARRAY_INIT(test, &array, 1);
	for (i = 0; i < 1024; i++) {
		entry[i].value = i;
		struct node *p = AN_ARRAY_PUSH(test, &array, &entry[i]);
        fail_if(p != &array.values[i]);
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

    struct node testnode;
    size_t size = sizeof(struct node);
    memset(&testnode, 0, size);

    for (i = 0; i < 10; i++) {
        struct node *p = AN_ARRAY_PUSH(test, &array, NULL);
        fail_if(memcmp(p, &testnode, size) != 0);
    }
}
END_TEST

START_TEST(stack)
{
	an_array_t array;
	ssize_t i;

	an_array_init(&array, 1);
	for (i = 0; i < 1024; i++) {
		an_array_push(&array, (void *)i);
	}

	for (i = 1024; i > 0; i--) {
		unsigned int n_entries;
		void *value = NULL;

		value = an_array_pop(&array, &n_entries);
		fail_if(n_entries != i);
		fail_if((uintptr_t)value != (uintptr_t)i - 1);
	}

	an_array_push(&array, NULL);
	fail_if(array.capacity != 1024);
}
END_TEST
START_TEST(find_element)
{
	an_array_t array;
	ssize_t i;

	an_array_init(&array, 1);
	for (i = 0; i < 1024; i++) {
		an_array_push(&array, (void *)i);
	}
	for (i = 0; i < 1024; i++) {
		fail_if(!an_array_member(&array, (void*)i), "Could not find a valid member");
	}
	fail_if(an_array_member(&array, (void*)-1), "Found a member that does not belong");
	an_array_pop(&array,NULL); // remove one to test the odd element count
	fail_if(an_array_length(&array) != 1023,"Pop failed ?");
	for (i = 0; i < 1023; i++) {
		fail_if(!an_array_member(&array, (void*)i), "Could not find a valid member");
	}
	fail_if(an_array_member(&array, (void*)-1), "Found a member that does not belong");
}
END_TEST

START_TEST(remove_element)
{
	an_array_t array;
	ssize_t i;

	an_array_init(&array, 1);
	for (i = 0; i < 1024; i++)
		an_array_push(&array, (void *)i);

	for (i = 0; i < 1024; i++) {
		fail_if(!an_array_remove(&array, (void*) i) , "Could not find a valid member to remove");
		fail_if(an_array_member(&array, (void*)i), "Found a deleted member");
		fail_if(an_array_length(&array) != 1023-i,"Remove member failed");
	}
}
END_TEST

START_TEST(resize)
{
	an_array_t array;
	AN_ARRAY_INSTANCE(test) array2;

	an_array_init(&array, 16);
	AN_ARRAY_INIT(test, &array2, 16);

	fail_if(an_array_length(&array) != 0);
	an_array_resize(&array, 2);
	fail_if(an_array_length(&array) != 0);

	fail_if(AN_ARRAY_LENGTH(test, &array2) != 0);
	AN_ARRAY_RESIZE(test, &array2, 2);
	fail_if(AN_ARRAY_LENGTH(test, &array2) != 0);
}
END_TEST

START_TEST(int_set)
{
	AN_ARRAY_INSTANCE(test2) array;
	AN_ARRAY_INIT(test2, &array, 1);

	int_set_t *set = new_int_set(NULL, sizeof(int), 16);

	for (int i = 16; i > 0; i--) {
		add_int_to_set(set, i);
	}

	AN_ARRAY_COPY_INT_SET(test2, &array, set);

	for (int i = 0; i < 16; i++) {
		int *x = AN_ARRAY_VALUE(test2, &array, i);
		fail_if(*x != int_set_index(set, i));
	}
}
END_TEST


int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("common/an_array");
	TCase *tc = tcase_create("test_an_array");

	an_malloc_init();
	an_md_probe();
	common_type_register();

	tcase_add_test(tc, stack);
	tcase_add_test(tc, stack_generic);
	tcase_add_test(tc, find_element);
	tcase_add_test(tc, remove_element);
	tcase_add_test(tc, resize);
	tcase_add_test(tc, int_set);

	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_array.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
