#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <check.h>
#include <time.h>
#include <sys/stat.h>

#include "an_charset.h"

void
test_stat(const char *str, size_t len, const struct an_utf8_stats e, const char *ctx, int line)
{
	struct an_utf8_stats o = an_utf8_stats_get(str, len);

	printf("%s: testing [%s], len:%zu\n", __FUNCTION__, str, len);
	ck_assert_msg(o.is_valid == e.is_valid, "stats.is_valid expected to be %s, but observed otherwise (%s:%d)",
		e.is_valid ? "true" : "false", ctx, line);
	ck_assert_msg(o.total_code_point_count == e.total_code_point_count, "stats.total_code_point_count expected to be %zu, "
		"but observed %zu (%s:%d)", e.total_code_point_count, o.total_code_point_count, ctx, line);
	ck_assert_msg(o.wide_code_point_count == e.wide_code_point_count, "stats.wide_code_point_count expected to be %zu, "
		"but observed %zu (%s:%d)", e.wide_code_point_count, o.wide_code_point_count, ctx, line);
	ck_assert_msg(o.parsed_length == e.parsed_length, "stats.parsed_length expected to be %zu, "
		"but observed %zu (%s:%d)", e.parsed_length, o.parsed_length, ctx, line);
}

START_TEST(test_stats)
{
	const char *s;

	s = "\u0430\u0410abc";
	test_stat(s, strlen(s), (struct an_utf8_stats){ .is_valid = true, .total_code_point_count = 5,
								.wide_code_point_count = 2, .parsed_length = strlen(s) }, __FUNCTION__, __LINE__);

	s = "\u0430\u0410";
	test_stat(s, strlen(s), (struct an_utf8_stats){ .is_valid = true, .total_code_point_count = 2,
								.wide_code_point_count = 2, .parsed_length = strlen(s) }, __FUNCTION__, __LINE__);

	s = "ABCabc";
	test_stat(s, strlen(s), (struct an_utf8_stats){ .is_valid = true, .total_code_point_count = 6,
								.wide_code_point_count = 0, .parsed_length = strlen(s) }, __FUNCTION__, __LINE__);

	s = "\xED\xA0\xBF"; /* invalid surrogate */
	test_stat(s, strlen(s), (struct an_utf8_stats){ .is_valid = false, .total_code_point_count = 0,
								.wide_code_point_count = 0, .parsed_length = 0 }, __FUNCTION__, __LINE__);

	s = "\u0430A\xED\xA0\xBF"; /* invalid surrogate */
	test_stat(s, strlen(s), (struct an_utf8_stats){ .is_valid = false, .total_code_point_count = 2,
								.wide_code_point_count = 1, .parsed_length = 3 }, __FUNCTION__, __LINE__);
}
END_TEST

int
main(int argc, char** argv)
{
	Suite* suite = suite_create("common/check_charset");

	TCase* tc = tcase_create("test_charset");
	tcase_add_test(tc, test_stats);
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_charset");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return num_failed;
}
