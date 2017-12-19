#include <check.h>
#include <stdbool.h>
#include <stdio.h>

#include "an_bitmap.h"

START_TEST(test_sanity) {
	fprintf(stdout, "\n\nBegin test_sanity\n");
	fail_unless(1==1, "sanity test suite");
}
END_TEST

START_TEST(test_an_static_bitmap_set) {
	fprintf(stdout, "\n\nBegin test_an_static_bitmap_set\n");
	AN_STATIC_BITMAP_T(bitmap, 31415);
	AN_STATIC_BITMAP_CLEAR(bitmap, 31415);
	fail_if(an_static_bitmap_is_set(bitmap, 68));
	an_static_bitmap_set(bitmap, 68);
	fail_if(!an_static_bitmap_is_set(bitmap, 68));
}
END_TEST

START_TEST(test_an_static_bitmap_unset) {
	fprintf(stdout, "\n\nBegin test_an_static_bitmap_unset\n");
	AN_STATIC_BITMAP_T(bitmap, 31415);
	AN_STATIC_BITMAP_CLEAR(bitmap, 31415);
	an_static_bitmap_set(bitmap, 68);
	fail_if(!an_static_bitmap_is_set(bitmap, 68));
	an_static_bitmap_unset(bitmap, 68);
	fail_if(an_static_bitmap_is_set(bitmap, 68));
}
END_TEST

START_TEST(test_an_static_clear) {
	fprintf(stdout, "\n\nBegin test_an_static_clear\n");
	AN_STATIC_BITMAP_T(bitmap, 31415);
	an_static_bitmap_set(bitmap, 68);
	an_static_bitmap_set(bitmap, 168);
	an_static_bitmap_set(bitmap, 30068);
	fail_if(!an_static_bitmap_is_set(bitmap, 68));
	fail_if(!an_static_bitmap_is_set(bitmap, 168));
	fail_if(!an_static_bitmap_is_set(bitmap, 30068));
	AN_STATIC_BITMAP_CLEAR(bitmap, 31415);
	for (unsigned i = 0; i < 31415; i++) {
		fail_if(an_static_bitmap_is_set(bitmap, i));
	}
	fail_if(!an_static_bitmap_is_empty(bitmap, 31415));
}
END_TEST

START_TEST(test_an_static_front2back) {
	fprintf(stdout, "\n\nBegin test_an_static_front2back\n");

	// A non-aligned bitmap of reasonably large size
	AN_STATIC_BITMAP_T(bitmap, 20000);
	AN_STATIC_BITMAP_CLEAR(bitmap, 20000);
	for (unsigned i = 0; i < 20000; i++) {
		if (i % 1023 == 0) {
			an_static_bitmap_set(bitmap, i);
		}
	}
	for (unsigned i = 0; i < 20000; i++) {
		if (i % 1023 == 0) {
			fail_unless(an_static_bitmap_is_set(bitmap, i));
		}
		else {
			fail_unless(!an_static_bitmap_is_set(bitmap, i));
		}
	}
	an_static_bitmap_unset(bitmap, 0);
	an_static_bitmap_unset(bitmap, 2046);
	for (unsigned i = 0; i < 20000; i++) {
		if (i == 0 || i == 2046 || i % 1023 != 0) {
			fail_unless(!an_static_bitmap_is_set(bitmap, i));
		}
		else {
			fail_unless(an_static_bitmap_is_set(bitmap, i));
		}
	}
}
END_TEST

int
main(int argc, char **argv)
{
	Suite* suite = suite_create("common/check_bitmap");

	TCase* tc = tcase_create("test_bitmap");
	tcase_add_test(tc, test_sanity);

	tcase_add_test(tc, test_an_static_bitmap_set);
	tcase_add_test(tc, test_an_static_bitmap_unset);
	tcase_add_test(tc, test_an_static_clear);
	tcase_add_test(tc, test_an_static_front2back);
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_bitmap.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);

	srunner_free(sr);

	return num_failed;
}
