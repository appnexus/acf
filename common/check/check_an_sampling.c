#include <check.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "common/server_config.h"
#include "common/an_sampling.h"


#define ASSERT_EQ(observed, expected, prop, ctx, line_no) \
	ck_assert_msg(observed.prop == expected.prop, "Expected " #prop " size to be %zu, got %zu. Ctx: \"%s (line %d)\"", expected.prop, observed.prop, ctx, line_no)

static void
check_fw_state(const struct an_sampling_fixed_window observed, const struct an_sampling_fixed_window expected, const char *ctx, int line_no)
{
	ASSERT_EQ(observed, expected, population_size, ctx, line_no);
	ASSERT_EQ(observed, expected, sample_size, ctx, line_no);
	ASSERT_EQ(observed, expected, current_index, ctx, line_no);
	ASSERT_EQ(observed, expected, selected_count, ctx, line_no);
}

START_TEST(test_fw)
{
	const size_t population_size = 1000;
	const size_t sample_size = 10;

	printf("test_sr: testing fixed window algorithm\n");

	struct an_sampling_fixed_window inst;
	an_sampling_fixed_window_init(&inst, population_size, sample_size);
	check_fw_state(inst, (struct an_sampling_fixed_window) {.population_size = population_size, .sample_size = sample_size, .current_index = 0, .selected_count = 0}, "init", __LINE__);
	ck_assert_msg(an_sampling_fixed_window_is_exhausted(&inst) == false, "Population is not expected to be exhausted (%d)", __LINE__);

	size_t selected = 0;
	for (size_t i=0; i<population_size; ++i) {
		if (an_sampling_fixed_window_next_is_selected(&inst)) {
			++selected;
		}
		check_fw_state(inst, (struct an_sampling_fixed_window) {.population_size = population_size, .sample_size = sample_size, .current_index = i + 1, .selected_count = selected}, "insertion 1", __LINE__);
		ck_assert_msg(an_sampling_fixed_window_is_exhausted(&inst) == (i == population_size - 1), "Population is %sexpected to be exhausted (%d)", (i == population_size - 1) ? "" : "not ",  __LINE__);
	}

	selected = 0;
	for (size_t i=0; i<population_size; ++i) {
		if (an_sampling_fixed_window_next_is_selected(&inst)) {
			++selected;
		}

		check_fw_state(inst, (struct an_sampling_fixed_window) {.population_size = population_size, .sample_size = sample_size, .current_index = i + 1, .selected_count = selected}, "insertion 2", __LINE__);
		ck_assert_msg(an_sampling_fixed_window_is_exhausted(&inst) == (i == population_size - 1), "Population is %sexpected to be exhausted (%d)", (i == population_size - 1) ? "" : "not ",  __LINE__);
	}

	an_sampling_fixed_window_reset(&inst);
	check_fw_state(inst, (struct an_sampling_fixed_window) {.population_size = population_size, .sample_size = sample_size, .current_index = 0, .selected_count = 0}, "reset", __LINE__);

	#define RR 1000
	size_t s[RR] = { 0 };
	size_t selected_total = 0;
	for (size_t r = 0; r < RR; ++r) {
		selected = 0;

		for (size_t i=0; i<population_size; ++i) {
			if (an_sampling_fixed_window_next_is_selected(&inst)) {
				++selected;
			}

			check_fw_state(inst, (struct an_sampling_fixed_window) {.population_size = population_size, .sample_size = sample_size, .current_index = i + 1, .selected_count = selected}, "insertion 2", __LINE__);
			ck_assert_msg(an_sampling_fixed_window_is_exhausted(&inst) == (i == population_size - 1), "Population is %sexpected to be exhausted (%d)", (i == population_size - 1) ? "" : "not ",  __LINE__);
		}

		selected_total += selected;
		s[r] = selected;
	}

	double v;
	for (size_t i=0; i<RR; ++i) {
		v = pow( (double)s[i] - ((double)selected_total / RR), 2);
	}
	v = v / (RR + 1);
	printf("test_sr: sample mean: %f, variance: %f\n", (double)selected_total / RR, v);
	ck_assert_msg(selected_total / RR == sample_size, "Mean of count of selected items (%zu) should match sample size (%zu) (Line %d)", selected_total / RR, sample_size, __LINE__);
	ck_assert_msg(fabs(v) <= 1, "Variance of count of selected items (%f) should be <1 (%d)", v, __LINE__);

	an_sampling_fixed_window_deinit(&inst);
	check_fw_state(inst, (struct an_sampling_fixed_window) {.population_size = population_size, .sample_size = sample_size, .current_index = 0, .selected_count = 0}, "deinit", __LINE__);
}
END_TEST


int
main(int argc, char **argv)
{
	int num_failed;

	struct timeval tv;
	gettimeofday(&tv, NULL);
	an_srand((getpid() << 16) ^ getuid() ^ tv.tv_sec ^ tv.tv_usec);
	gettimeofday(&tv, NULL);
	for (int i = (tv.tv_sec ^ tv.tv_usec) & 0x1F; i > 0; i--) {
		an_rand();
	}
	an_malloc_init();
	an_thread_set_epoch_malloc(current, true);

	Suite *suite = suite_create("common/check_an_sampling");

	TCase *tc = tcase_create("test_an_sampling");
	tcase_set_timeout(tc, 0.0);
	tcase_add_test(tc, test_fw);
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_sampling.xml");

	srunner_run_all(sr, CK_NORMAL);

	num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return num_failed;
}
