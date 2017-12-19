#include <check.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "common/an_average.h"
#include "common/util.h"

START_TEST(test_small) {
	struct an_average avg;
	double sum = 0, count = 0;

	an_average_init(&avg);
	for (size_t i = 0; i < 1UL << 10; i++) {
		double approx, delta, exact;
		uint64_t num, denom;
		size_t x;

		x = an_random_below(100);
		an_average_insert(&avg, x);
		sum += x;
		count++;

		an_average_read(&avg, &num, &denom);
		approx = 1.0 * num / denom;
		exact = sum / count;

		if (denom > 200) {
			delta = fabs(approx - exact) / (fabs(exact) + 1);
			fail_if(delta > 1e-1);
		}
	}
}
END_TEST

START_TEST(test_large) {
	struct an_average avg;
	double sum = 0, count = 0;
	uint64_t offset = 1ULL << 61;

	an_average_init(&avg);
	for (size_t i = 0; i < 1UL << 10; i++) {
		double approx, delta, exact;
		uint64_t num, denom;
		size_t x;

		x = an_random_below(100);
		an_average_insert(&avg, offset + x);
		sum += x;
		count++;

		an_average_read(&avg, &num, &denom);
		approx = 1.0 * num / denom;
		exact = offset + (sum / count);

		if (denom > 200) {
			delta = fabs(approx - exact) / (fabs(exact) + 1);
			fail_if(delta > 1e-1);
		}
	}
}
END_TEST

START_TEST(test_inc) {
	struct an_average avg;
	double sum = 0, count = 0;

	an_average_init(&avg);
	for (size_t i = 0; i < 1UL << 10; i++) {
		double approx, delta, exact;
		uint64_t num, denom;
		size_t x;

		x = an_random_below(100);
		an_average_insert(&avg, x);
		sum += x;
		count++;

		x = an_random_below(10000);
		an_average_increment(&avg, x);
		sum += x;

		an_average_read(&avg, &num, &denom);
		approx = 1.0 * num / denom;
		exact = sum / count;

		if (denom > 200) {
			delta = fabs(approx - exact) / (fabs(exact) + 1);
			fail_if(delta > 1e-1);
		}
	}
}
END_TEST

int
main(int argc, char **argv)
{
	int num_failed;
	Suite *suite = suite_create("common/check_an_average");

	TCase *tc = tcase_create("test_an_average");
	tcase_add_test(tc, test_small);
	tcase_add_test(tc, test_large);
	tcase_add_test(tc, test_inc);
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_average.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);

	num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return num_failed;
}
