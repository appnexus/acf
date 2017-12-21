#include <assert.h>
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "an_util.h"

static bool
is_power_of_2(uint64_t x)
{
	return ((x != 0) && !(x & (x - 1)));
}

START_TEST(test_an_next_power_of_2)
{

	for (uint64_t i = 1; i < INT16_MAX; i++) {
		uint64_t v = an_next_power_of_2(i);
		ck_assert_msg(is_power_of_2(v));
		ck_assert(v >= i);
		ck_assert(v/2 < i);
	}
}
END_TEST

START_TEST(test_an_safe_fill)
{
	char buf[32];
	memset(buf, '1', sizeof(buf));
	an_safe_fill(buf, NULL, sizeof(buf));
	ck_assert(strlen(buf) == 0);

	memset(buf, '1', sizeof(buf));
	an_safe_fill(buf, "\0", sizeof(buf));
	ck_assert(strlen(buf) == 0);

	memset(buf, '1', sizeof(buf));
	an_safe_fill(buf, "one", 0);
	ck_assert(strlen(buf) == 0);

	memset(buf, '1', sizeof(buf));
	an_safe_fill(buf, "NULL", sizeof(buf));
	ck_assert(strlen(buf) == 0);

	memset(buf, '1', sizeof(buf));
	an_safe_fill(buf, "one", sizeof(buf));
	ck_assert(strlen(buf) == 3);

	char src[sizeof(buf) * 2];
	memset(src, '1', sizeof(src));
	memset(buf, '1', sizeof(buf));
	an_safe_fill(buf, src, sizeof(buf));
	ck_assert(strlen(buf) == sizeof(buf)-1);
}
END_TEST

START_TEST(test_an_safe_strncpy)
{
	char buf[32];
	memset(buf, '1', sizeof(buf));
	an_safe_strncpy(buf, NULL, sizeof(buf));
	ck_assert(strlen(buf) == 0);

	memset(buf, '1', sizeof(buf));
	an_safe_strncpy(buf, "\0", sizeof(buf));
	ck_assert(strlen(buf) == 0);

	memset(buf, '1', sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	an_safe_strncpy(buf, "one", 0);
	ck_assert(strlen(buf) == sizeof(buf) - 1);

	memset(buf, '1', sizeof(buf));
	an_safe_strncpy(buf, "one", sizeof(buf));
	ck_assert(strlen(buf) == 3);

	char src[sizeof(buf) * 2];
	memset(src, '1', sizeof(src));
	memset(buf, '1', sizeof(buf));
	an_safe_strncpy(buf, src, sizeof(buf));
	ck_assert(strlen(buf) == sizeof(buf)-1);
}
END_TEST

START_TEST(test_an_time_print)
{
	char buf[1024];
	struct tm tm;

	time_t x = 1429887925;
	gmtime_r(&x, &tm);

	memset(buf, 0, sizeof(buf));
	an_time_print(&tm, buf, sizeof(buf));
	ck_assert(strcmp(buf, "2015-04-24 15:05:25") == 0);
}
END_TEST

START_TEST(test_an_time_to_str)
{
	char buf[1024];
	char buf2[1024];
	time_t x = 1429887925;

	memset(buf, 0, sizeof(buf));
	an_time_to_str(x, buf, sizeof(buf));

	memset(buf2, 0, sizeof(buf2));
	an_time_to_str(x, buf2, sizeof(buf));
	ck_assert(strcmp(buf, buf2) == 0);
}
END_TEST


int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("an_util");
	TCase *tc = tcase_create("test_an_util");

	tcase_add_test(tc, test_an_next_power_of_2);
	tcase_add_test(tc, test_an_safe_fill);
	tcase_add_test(tc, test_an_safe_strncpy);
	tcase_add_test(tc, test_an_time_print);
	tcase_add_test(tc, test_an_time_to_str);

	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_util.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
