#include <assert.h>
#include <check.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common/an_array.h"
#include "common/an_streaming_quantile.h"
#include "common/an_malloc.h"
#include "common/an_thread.h"
#include "common/common_types.h"

START_TEST(same_val)
{
	struct an_streaming_qnt *qnt;

	qnt = an_streaming_qnt_estimate_create(0.99, 0, 1);
	for (size_t i = 0; i < 5000; i++) {
		an_streaming_qnt_update_mpmc(qnt, 100);
	}

	uint64_t result = an_streaming_qnt_observe(qnt);
	fail_if(result != 100);

	an_streaming_qnt_destroy(qnt);
}
END_TEST

START_TEST(same_val_static)
{
	struct an_streaming_qnt qnt = AN_STREAMING_QUANTILE_INITIALIZER(0.99, 0, 1);

	for (size_t i = 0; i < 5000; i++) {
		an_streaming_qnt_update_mpmc(&qnt, 100);
	}

	uint64_t result = an_streaming_qnt_observe(&qnt);
	fail_if(result != 100);
}
END_TEST

START_TEST(correct_esimate)
{
	int rand_arr[100] = { 0 };
	for (size_t i = 0; i < 100; i++) {
		rand_arr[i] = i + 1;
	}

	size_t error_count = 0;

	for (int i = 0; i < 100; i++) {
		struct an_streaming_qnt qnt = AN_STREAMING_QUANTILE_INITIALIZER(0.75, 75, 1);
		for (size_t j = 0; j < 1000; j++) {
			an_random_shuffle(rand_arr, 100);
			for (size_t k = 0; k < 100; k++) {
				an_streaming_qnt_update_mpmc(&qnt, rand_arr[k]);
			}
		}

		int result = an_streaming_qnt_observe(&qnt);
		int accepted = 75;
		double percent_error = (result - accepted) / (double) (accepted) * 100.0;
		if (fabs(percent_error) > 10.0) {
			error_count++;
		}
	}
	fail_if(error_count > 10);
}
END_TEST

START_TEST(zero_estimate)
{
	int rand_arr[100] = { 0 };
	for (size_t i = 0; i < 100; i++) {
		rand_arr[i] = i + 1;
	}

	size_t error_count = 0;

	for (int i = 0; i < 100; i++) {
		struct an_streaming_qnt qnt = AN_STREAMING_QUANTILE_INITIALIZER(0.75, 0, 1);
		for (size_t j = 0; j < 1000; j++) {
			an_random_shuffle(rand_arr, 100);
			for (size_t k = 0; k < 100; k++) {
				an_streaming_qnt_update_mpmc(&qnt, rand_arr[k]);
			}
		}

		int result = an_streaming_qnt_observe(&qnt);
		int accepted = 75;
		double percent_error = (result - accepted) / (double) (accepted) * 100.0;
		if (fabs(percent_error) > 10.0) {
			error_count++;
		}
	}
	fail_if(error_count > 10);
}
END_TEST

int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("common/an_streaming_quantile");
	TCase *tc = tcase_create("test_an_streaming_quantile");

	an_malloc_init();
	an_md_probe();
	an_thread_init();
	common_type_register();

	tcase_add_test(tc, same_val_static);
	tcase_add_test(tc, same_val);
	tcase_add_test(tc, correct_esimate);
	tcase_add_test(tc, zero_estimate);

	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/an_streaming_quantile.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
