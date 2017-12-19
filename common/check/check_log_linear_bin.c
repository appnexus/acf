#include <assert.h>
#include <check.h>

#include "common/log_linear_bin.h"

/* Check doesn't like having millions of test cases... */
#if 1
#define must(X) assert((X))
#else
#define must(X) fail_if(!(X))
#endif

#define N_TEST_LB 10UL

START_TEST(round_up) {
	unsigned int linear = _i;

	for (unsigned subbin = 0; subbin <= linear; subbin++) {
		size_t last_bin, last_bin_size, last_rounded;

		last_bin = log_linear_bin_of(0, &last_rounded, &last_bin_size,
		    linear, subbin);

		must(last_bin == 0);
		must(last_bin_size == 1);
		must(last_rounded == 0);

		for (size_t i = 0; i < 1UL << N_TEST_LB; i++) {
			size_t bin, bin_size, rounded;

			bin = log_linear_bin_of(i, &rounded, &bin_size, linear, subbin);
			must(bin >= last_bin);
			must(bin_size > 0);
			must(i <= rounded);
			must(i + bin_size > rounded);

			if (bin == last_bin) {
				must(last_rounded == rounded);
				must(last_bin_size == bin_size);
			} else {
				must(rounded > last_rounded);
				must(bin_size >= last_bin_size);
				must(last_rounded + 1 == i);

				last_bin = bin;
				last_rounded = rounded;
				last_bin_size = bin_size;
			}
		}

		fail_if(last_bin > 1 + (N_TEST_LB << subbin));
	}
}
END_TEST

START_TEST(round_down) {
	unsigned int linear = _i;

	for (unsigned subbin = 0; subbin <= linear; subbin++) {
		size_t last_bin, last_bin_size, last_rounded;

		last_bin = log_linear_bin_down_of(0, &last_rounded, &last_bin_size,
		    linear, subbin);

		must(last_bin == 0);
		must(last_bin_size > 0);
		must(last_rounded == 0);

		for (size_t i = 0; i < 1UL << N_TEST_LB; i++) {
			size_t bin, bin_size, rounded;

			bin = log_linear_bin_down_of(i, &rounded, &bin_size, linear, subbin);

			must(bin >= last_bin);
			must(i >= rounded);
			must(rounded + bin_size > i);
			if (bin == last_bin) {
				must(last_rounded == rounded);
				must(last_bin_size == bin_size);
			} else {
				must(rounded > last_rounded);
				must(bin_size >= last_bin_size);
				must(last_rounded + last_bin_size == i);

				last_bin = bin;
				last_rounded = rounded;
				last_bin_size = bin_size;
			}
		}

		fail_if(last_bin > (N_TEST_LB << subbin));
	}
}
END_TEST

int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("common/log_linear_bin");
	TCase *tc = tcase_create("test_log_linear_bin");

	tcase_add_loop_test(tc, round_up, 0, 32);
	tcase_add_loop_test(tc, round_down, 0, 32);
	(void)round_up;

	suite_add_tcase(suite, tc);
	sr = srunner_create(suite);

	srunner_set_xml(sr, "check/check_log_linear_bin.xml");
	srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all(sr, CK_NORMAL);

	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return num_failed;
}
