#include <assert.h>
#include <check.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "common/an_interval.h"
#include "common/an_malloc.h"
#include "common/an_rand.h"
#include "common/common_types.h"
#include "common/int_set.h"
#include "common/util.h"

#if 0
/* This runs for about 2 hours :x */
# define CK_ASSERT(X) assert((X))
# define EXHAUSTIVE_RANGE 7
#else
# define CK_ASSERT(X) fail_if(!(X))
# define EXHAUSTIVE_RANGE 4
#endif

struct pair {
	uint64_t begin;
	uint64_t end;
};

static double max_ratio = 0;
static double sum_ratio = 0;
static size_t count_ratio = 0;

static struct an_interval
build(const struct pair *pairs, size_t n, uint64_t mask, uint64_t offset, uint64_t *max_hit)
{
	struct an_interval ret;
	struct an_interval_builder *builder;
	size_t count = 0;

	*max_hit = 0;
	builder = an_interval_builder_create();
	for (size_t i = 0; i < n; i++) {
		if ((mask & (1UL << i)) == 0) {
			continue;
		}

		count++;
		an_interval_builder_insert(builder, offset + pairs[i].begin,
		    pairs[i].end - pairs[i].begin, i);
		*max_hit = max(*max_hit, offset + pairs[i].end);
	}

	an_interval_init(&ret, builder, 2, 2);
	if (count != 0) {
		double ratio = 1.0 * ret.n_entries / count;

		max_ratio = max(max_ratio, ratio);
		sum_ratio += ratio;
		count_ratio++;
	}

	return ret;
}

static size_t
stabbing_count(const struct an_interval *interval, uint64_t key)
{
	uint64_t bitmap = 0;
	size_t count = 0;
	uint32_t value;

	AN_INTERVAL_FOREACH(interval, NULL, key, value) {
		count++;
		if (value < 64) {
			CK_ASSERT((bitmap & (1UL << value)) == 0);
			bitmap |= 1UL << value;
		}
	}

	return count;
}

static size_t
slow_count(const struct pair *pairs, size_t n_pairs, uint64_t key)
{
	size_t count = 0;

	for (size_t i = 0; i < n_pairs; i++) {
		count += pairs[i].begin <= key && key <= pairs[i].end;
	}

	return count;
}

static size_t
slow_count_mask(const struct pair *pairs, size_t n_pairs, uint64_t mask, uint64_t key)
{
	size_t count = 0;

	for (size_t i = 0; i < n_pairs; i++) {
		count += (mask & (1UL << i)) != 0 &&
			pairs[i].begin <= key && key <= pairs[i].end;
	}

	return count;
}

START_TEST(small_exhaustive)
{
	struct pair pairs[EXHAUSTIVE_RANGE * (EXHAUSTIVE_RANGE + 1) / 2];
	size_t n_pairs = 0;

	for (size_t i = 0; i < EXHAUSTIVE_RANGE; i++) {
		for (size_t j = i; j < EXHAUSTIVE_RANGE; j++) {
			pairs[n_pairs++] = (struct pair) { .begin = i, .end = j };
			assert(n_pairs <= ARRAY_SIZE(pairs));
		}
	}

	for (uint64_t mask = 1; mask <= (1UL << n_pairs) - 1; mask++) {
		uint64_t offsets[] = {
			0,
			2,
			1000,
			UINT64_MAX - EXHAUSTIVE_RANGE - 1,
			(UINT64_MAX - EXHAUSTIVE_RANGE) + 1
		};

		if ((mask % 10000) == 0) {
			printf("n: %" PRIu64 " (%.2f %%) %.3f %.3f\n",
			    mask, 100.0 * mask / ((1UL << n_pairs) - 1),
			    max_ratio, sum_ratio / count_ratio);
		}

		for (size_t i = 0; i < ARRAY_SIZE(offsets); i++) {
			struct an_interval interval;
			uint64_t offset = offsets[i];
			uint64_t max_hit;

			interval = build(pairs, ARRAY_SIZE(pairs), mask, offset, &max_hit);
			for (size_t j = 0; j < EXHAUSTIVE_RANGE; j++) {
				CK_ASSERT(stabbing_count(&interval, j + offset) ==
				    slow_count_mask(pairs, ARRAY_SIZE(pairs), mask, j));
			}

			if (offset >= 2) {
				CK_ASSERT(stabbing_count(&interval, 0) == 0);
				CK_ASSERT(stabbing_count(&interval, 1) == 0);
				CK_ASSERT(stabbing_count(&interval, offset - 1) == 0);
				CK_ASSERT(stabbing_count(&interval, offset - 2) == 0);
			}

			if (max_hit < UINT64_MAX) {
				CK_ASSERT(stabbing_count(&interval, max_hit + 1) == 0);
				CK_ASSERT(stabbing_count(&interval, UINT64_MAX) == 0);
			}

			if (max_hit < UINT64_MAX - 1) {
				CK_ASSERT(stabbing_count(&interval, max_hit + 2) == 0);
				CK_ASSERT(stabbing_count(&interval, UINT64_MAX - 1) == 0);
			}

			an_interval_deinit(&interval);
		}
	}

	if (count_ratio > 0) {
		printf("Ratio: %.3f %.3f\n", max_ratio, sum_ratio / count_ratio);
	}

} END_TEST

START_TEST(points)
{
	int_set_t points;
	struct an_interval interval;
	struct an_interval_cursor cursor = AN_INTERVAL_CURSOR_INIT;
	size_t n = 25 * _i;

	int_set_init(&points, NULL, sizeof(int32_t), 32);

	{
		struct an_interval_builder *builder;

		builder = an_interval_builder_create();
		for (size_t i = 0; i < n; i++) {
			uint32_t rnd;

			rnd = an_random_below(3000);
			add_int_to_set(&points, rnd);
			an_interval_builder_insert(builder, rnd, 0, i);
		}

		an_interval_init(&interval, builder, 2, 2);
	}

	for (size_t i = 0; i < 4000; i++) {
		fail_if(int_set_contains(&points, i) != an_interval_contains(&interval, NULL, i));
	}


	for (size_t i = 0; i < 4000; i++) {
		fail_if(int_set_contains(&points, i) != an_interval_contains(&interval, &cursor, i));
	}

	an_interval_deinit(&interval);
	int_set_deinit(&points);
} END_TEST

START_TEST(ranges)
{
	struct an_interval interval;
	struct pair *pairs;
	size_t n_pairs = _i * 100;

	pairs = calloc(n_pairs, sizeof(struct pair));

	{
		struct an_interval_builder *builder;

		builder = an_interval_builder_create();
		for (size_t i = 0; i < n_pairs; i++) {
			uint64_t base;
			uint32_t width;

			base = an_rand64();
			if (base < UINT64_MAX - UINT32_MAX) {
				width = an_rand32();
			} else {
				width = an_random_below(UINT64_MAX - base);
			}

			pairs[i].begin = base;
			pairs[i].end = base + width;
			an_interval_builder_insert(builder, base, width, i);
		}

		an_interval_init(&interval, builder, 2, 2);
	}

	for (size_t i = 0; i < n_pairs; i++) {
		uint64_t begin = pairs[i].begin;
		uint64_t end = pairs[i].end;
		uint32_t width = end - begin;

		fail_if(stabbing_count(&interval, begin) != slow_count(pairs, n_pairs, begin));
		for (size_t j = 0; j < 10; j++) {
			uint64_t x = begin + an_random_below(width) + 1;

			fail_if(stabbing_count(&interval, x) != slow_count(pairs, n_pairs, x));
		}

		if (i > 0) {
			end = begin;
			begin = pairs[i - 1].end;
			width = min((uint64_t)UINT32_MAX, end - begin);

			fail_if(stabbing_count(&interval, begin) != slow_count(pairs, n_pairs, begin));
			for (size_t j = 0; j < 10; j++) {
				uint64_t x = begin + an_random_below(width) + 1;

				fail_if(stabbing_count(&interval, x) != slow_count(pairs, n_pairs, x));
			}
		}
	}

	an_interval_deinit(&interval);
	free(pairs);
} END_TEST

START_TEST(ranges_small)
{
	struct an_interval interval;
	struct pair *pairs;
	size_t n_pairs = _i * 100;

	pairs = calloc(n_pairs, sizeof(struct pair));

	{
		struct an_interval_builder *builder;

		builder = an_interval_builder_create();
		for (size_t i = 0; i < n_pairs; i++) {
			uint64_t begin, end;

			begin = an_random_below(4000);
			end = an_random_below(4000);
			if (begin > end) {
				uint64_t temp = begin;

				begin = end;
				end = temp;
			}

			pairs[i].begin = begin;
			pairs[i].end = end;
			an_interval_builder_insert(builder, begin, end - begin, i);
		}

		an_interval_init(&interval, builder, 2, 2);
	}

	for (size_t i = 0; i < 4000; i++) {
		fail_if(stabbing_count(&interval, i) != slow_count(pairs, n_pairs, i));
	}

	an_interval_deinit(&interval);
	free(pairs);
} END_TEST

START_TEST(ranges_points)
{
	int_set_t points;
	struct an_interval interval;
	struct an_interval_cursor cursor = AN_INTERVAL_CURSOR_INIT;
	size_t n = 25 * _i;
	size_t n_pairs = _i * 100;

	int_set_init(&points, NULL, sizeof(int32_t), 32);

	{
		struct an_interval_builder *builder;

		builder = an_interval_builder_create();
		for (size_t i = 0; i < n; i++) {
			uint32_t rnd;

			rnd = an_random_below(3000);
			add_int_to_set(&points, rnd);
			an_interval_builder_insert(builder, rnd, 0, i);
		}

		for (size_t i = 0; i < n_pairs; i++) {
			uint64_t begin, end;

			begin = an_random_below(4000);
			end = an_random_below(4000);
			if (begin > end) {
				uint64_t temp = begin;

				begin = end;
				end = temp;
			}

			an_interval_builder_insert(builder, begin, end - begin, i + 10000);
		}

		an_interval_init(&interval, builder, 2, 2);
	}

	INT_SET_FOREACH(&points, x) {
		fail_if(!an_interval_contains(&interval, NULL, x));
	}

	INT_SET_FOREACH(&points, x) {
		fail_if(!an_interval_contains(&interval, &cursor, x));
	}

	an_interval_deinit(&interval);
	int_set_deinit(&points);
} END_TEST

int
main(int argc, char *argv[])
{
	SRunner *sr;
	Suite *suite = suite_create("common/an_interval");
	TCase *tc = tcase_create("test_an_interval");

	an_malloc_init();
	common_type_register();

	tcase_add_test(tc, small_exhaustive);
	tcase_add_loop_test(tc, points, 1, 100);
	tcase_add_loop_test(tc, ranges, 1, 40);
	tcase_add_loop_test(tc, ranges_small, 1, 40);
	tcase_add_loop_test(tc, ranges_points, 1, 40);

	suite_add_tcase(suite, tc);

	sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_interval.xml");
	srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all(sr, CK_NORMAL);

	return srunner_ntests_failed(sr);
}
