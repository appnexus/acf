#include <check.h>
#include <getopt.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "common/an_smr.h"
#include "common/an_malloc.h"
#include "common/an_thread.h"
#include "common/common_types.h"
#include "common/server_config.h"
#include "common/uuid_manager.h"

#include <ck_stack.h>

struct node {
	int value;
	ck_stack_entry_t stack_entry;
};

static ck_stack_t stack = CK_STACK_INITIALIZER;
static int *check_free_array;

CK_STACK_CONTAINER(struct node, stack_entry, stack_container)
static struct node **node_array;

static unsigned int n_nodes;
static unsigned int n_writers;
static unsigned int n_readers;
static unsigned int n_threads;

/*
 * This should be a valid destructor callback in both
 * old and new implementations
 */
static void
node_destructor(struct node *n)
{

	check_free_array[n->value] = 1;
	free(n);
	return;
}

/*
 * Allocates and initializes the stack
 */
static void
alloc_and_init_stack(void)
{

	unsigned int i;
	node_array = (struct node**) malloc(sizeof(struct node*) * n_nodes);
	for (i = 0; i < n_nodes; i++) {
		node_array[i] = malloc(sizeof(struct node));
		if (node_array[i] == NULL) {
			abort();
		}
		node_array[i]->value = (int) i;
		ck_stack_push_upmc(&stack, &node_array[i]->stack_entry);
	}
}

static void
setup_test(int nodes, int readers, int writers)
{
	n_nodes = nodes;
	n_readers = readers;
	n_writers = writers;
	n_threads = readers + writers;
	alloc_and_init_stack();

	check_free_array = calloc(n_nodes, sizeof(int));
}

static void
cleanup_test(void)
{
	n_nodes = 0;
	n_readers = 0;
	n_writers = 0;
	n_threads = 0;
	free(node_array);
	free(check_free_array);
	/* TODO is there some way to reset the global epoch and
	 * check whether there is anything pending and actually
	 */
}

/*
 * Attempts to read as much of the stack as possible
 * and returns the number of elements read
 * Needs to be executed by a an_thread
 */
static int
read_from_stack(void)
{
	int n_read;
	//int val;
	an_smr_section_t section;
	ck_stack_entry_t *cursor;

	n_read = 0;

	for(;;) {
		an_smr_begin(&section);
		CK_STACK_FOREACH(&stack, cursor) {
			if (cursor == NULL) {
				break;
			}
			n_read += 1;
		}

		an_smr_end(&section);
		/* Stop reading if either the stack is empty or there are
		 * no concurrent writers (npsc case)
		 */
		if (CK_STACK_ISEMPTY(&stack) == true || n_writers == 0) {
			break;
		}
	}
	return n_read;
}

/*
 * Removes elements from the stack one at a time
 */
static int
delete_from_stack(void)
{
	struct node *e;
	ck_stack_entry_t *s;
	unsigned int i, n_deleted;
	an_smr_section_t section;
	n_deleted = 0;
	for (i = 0; i < n_nodes; i++) {
		an_smr_begin(&section);
		s = ck_stack_pop_upmc(&stack);
		an_smr_end(&section);

		e = stack_container(s);
		if (s == NULL || e == NULL) {
			break;
		}

		n_deleted++;
		an_smr_call(e, node_destructor);
		an_smr_poll();
	}

	while (an_smr_n_pending(&current->smr) != 0) {
		an_smr_poll();
	}

	return n_deleted;
}

static void
create_an_thread(void)
{
	struct an_thread* thread;
	thread = an_thread_create();
	an_thread_put(thread);
}

static void *
npsc_test_thread_func(void *n)
{
	(void) n;
	setup_test(100000, 1, 0);
	unsigned int ret;
	create_an_thread();
	ret = read_from_stack();
	printf("Read returns %u n_nodes: %u \n", ret, n_nodes);
	fail_if(ret != n_nodes);
	ret = delete_from_stack();
	printf("Delete returns %u n_nodes: %u \n", ret, n_nodes);
	fail_if(ret != n_nodes);
	cleanup_test();
	return (NULL);
}

static void *
read_thread_func(void *unused)
{
	(void) unused;
	uintptr_t ret;
	create_an_thread();
	ret = (uintptr_t) read_from_stack();
	return (void *) ret;

}

static void *
delete_thread_func(void *unused)
{
	(void) unused;
	uintptr_t ret;
	create_an_thread();
	ret = (uintptr_t) delete_from_stack();
	return (void *) ret;

}

static void
check_frees(void)
{
	unsigned int n_freed = 0;
	for(unsigned int i = 0; i < n_nodes; i++) {
		if (check_free_array[i] == 1) {
			n_freed++;
		}
	}

	printf("Num actually freed: %u \n", n_freed);
	fail_if(n_freed != n_nodes);
}

/*
 * Single thread first populates a linked list with nodes, then
 * reads them one by one (each in separate epooch)
 */
void npsc_test()
{
	printf("test the single consumer no concurrent producer case\n");
	pthread_t t1;
	pthread_create(&t1, NULL, npsc_test_thread_func, NULL);
	pthread_join(t1, NULL);
}

static void *
sleeper_func(void *arg)
{

	(void)arg;
#ifdef USE_RTBR
	an_rtbr_self();
	{
		AN_RTBR_SCOPE(test, "test");
	}
#endif

	while (1) {
		sleep(1000);
	}

	return NULL;
}

/*
 * General multithreaded test case
 * with arbitrary numbers of reading and writing threads
 */
void general_test(int nodes, int readers, int writers)
{
	unsigned int deleted, i;
	uintptr_t ret;
	deleted = ret = 0;
	printf("test the general case %d readers (c) %d writers (p) %d nodes\n", readers, writers, nodes);
	setup_test(nodes, readers, writers);
	pthread_t* pthread_arr = (pthread_t *) malloc(sizeof(pthread_t) * n_threads);
	pthread_t sleeper;

	for (i = 0; i < n_readers; i++) {
		pthread_create(&pthread_arr[i], NULL, read_thread_func, NULL);
	}

	for (i = n_readers; i < n_threads; i++) {
		pthread_create(&pthread_arr[i], NULL, delete_thread_func, NULL);
	}

	pthread_create(&sleeper, NULL, sleeper_func, NULL);

	for(i = 0; i < n_threads; i++) {
		pthread_join(pthread_arr[i], (void **) &ret);
		if (i >= n_readers) {
			deleted += (unsigned int) ret;
		}
	}

	pthread_cancel(sleeper);
	pthread_join(sleeper, NULL);
	printf("Total nodes deleted %u n_nodes: %u \n", deleted, n_nodes);
	fail_if(deleted != n_nodes);
	check_frees();
	cleanup_test();
}

START_TEST(test_smr) {
	npsc_test();

	general_test(10000, 3, 1);
	general_test(10000, 6, 6);
}
END_TEST

int main(int argc, char **argv)
{
	server_config_init("check_an_smr", argc, argv);
	Suite *suite = suite_create("common/an_smr");

	TCase *tc = tcase_create("test_an_smr");
	tcase_add_test(tc, test_smr);

	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_smr.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return num_failed;
}
