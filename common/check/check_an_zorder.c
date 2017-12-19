#include <assert.h>
#include <check.h>
#include <stdint.h>
#include <stdio.h>

#include "common/an_zorder.h"

#define LOW 2048U
#define HIGH (65536U - 2048U)
#define PRINT_MODULO 256U

static uint32_t
simple_zorder(uint16_t x, uint16_t y)
{
	uint32_t result = 0;

	for (size_t i = 0; i < 16; i++) {
		uint32_t x_bit = (x >> i) & 1;
		uint32_t y_bit = (y >> i) & 1;

		result |= x_bit << (i * 2);
		result |= y_bit << (i * 2 + 1);
	}

	return result;
}

static void
assert_zorder_valid(uint16_t x, uint16_t y)
{
	uint32_t an_result, simple_result;

	an_result = an_zorder(x, y);
	simple_result = simple_zorder(x, y);

	ck_assert_msg(an_result == simple_result, "Assertion 'an_zorder(x, y)==simple_zorder(x, y)'"
	    " failed for x = 0x%x and y = 0x%x: an_zorder(x, y)==0x%x, simple_zorder(x, y)==0x%x",
	    x, y, an_result, simple_result);

	return;
}

static void
print_range(uint32_t x_low, uint32_t x_high, uint32_t y_low, uint32_t y_high)
{

	printf("Testing zorder(x, y) for x in [0x%x, 0x%x) and y in [0x%x, 0x%x)\n",
	    x_low, x_high, y_low, y_high);
	return;
}

START_TEST(zorder_low)
{
	for (uint32_t x = 0; x < LOW; x++) {
		if (x % PRINT_MODULO == 0) {
			print_range(x, x + PRINT_MODULO, 0, LOW);
		}

		for (uint32_t y = 0; y < LOW; y++) {
			assert_zorder_valid(x, y);
		}
	}
} END_TEST

START_TEST(zorder_high)
{
	for (uint32_t x = HIGH; x <= UINT16_MAX; x++) {
		if (x % PRINT_MODULO == 0) {
			print_range(x, x + PRINT_MODULO, HIGH, UINT16_MAX + 1);
		}

		for (uint32_t y = HIGH; y <= UINT16_MAX; y++) {
			assert_zorder_valid(x, y);
		}
	}
} END_TEST

START_TEST(zorder_powers_of_two)
{
	uint32_t from = (1 << _i) - 64;
	uint32_t to = (1 << _i) + 64;

	print_range(from, to, from, to);

	for (uint32_t x = from; x < to; x++) {
		for (uint32_t y = from; y < to; y++) {
			assert_zorder_valid(x, y);
		}
	}
} END_TEST

int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("common/an_zorder");
	TCase *tc = tcase_create("test_an_zorder");

	tcase_add_test(tc, zorder_low);
	tcase_add_test(tc, zorder_high);
	tcase_add_loop_test(tc, zorder_powers_of_two, 11, 16);
	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_zorder.xml");
	srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
