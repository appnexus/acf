#include <check.h>
#include <getopt.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "common/an_malloc.h"
#include "common/an_thread.h"
#include "common/common_types.h"
#include "common/server_config.h"
#include "common/uuid_manager.h"

void
tls_key_dtor(void *data)
{
	int *i = data;
	printf("tls_key_dtor called for %d!\n", *i);
	free(i);
}

void *
thread_func(void *n)
{

	for(int i=0; i < 50; i++) {
		printf("creating key %d\n", i);
		an_thread_key_t key;
		fail_if(an_thread_key_create(&key, n) != 0);
		int *j = malloc(sizeof(int));
		*j = i;
		an_thread_setspecific(key, j);
		void *x = an_thread_getspecific(key);

		fail_if(x == NULL);
	}
	return NULL;
}

START_TEST(test_tls) {
        printf("test tls destruction order\n");

	pthread_t t1;
	pthread_create(&t1, NULL, thread_func, tls_key_dtor);

	pthread_join(t1, NULL);
}
END_TEST

int main(int argc, char **argv) {

	server_config_init("check_an_thread", argc, argv);

	Suite *suite = suite_create("common/an_thread");

	TCase *tc = tcase_create("test_an_thread");
	tcase_add_test(tc, test_tls);

	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_thread.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return num_failed;
}
