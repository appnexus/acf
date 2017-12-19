/*
 * Copyright 2014 AppNexus, Inc.
 *
 * This is unpublished proprietary source code of AppNexus, Inc.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 *
 * Redistribution of this material is strictly prohibited.
 */

#include <check.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "common/an_md.h"
#include "common/an_sparse_bitmap.h"

START_TEST(test_insertion)
{
	struct an_sparse_bitmap bitmap;
	an_sparse_bitmap_init(&bitmap, 0);
	an_sparse_bitmap_insert_range(&bitmap, 2, 4);
	an_sparse_bitmap_insert_range(&bitmap, 4, 5);

	an_sparse_bitmap_insert_range(&bitmap, 10, 15);
	an_sparse_bitmap_insert_range(&bitmap, 14, 20);

	an_sparse_bitmap_insert_range(&bitmap, 25, 30);

	fail_if(an_sparse_bitmap_contains(&bitmap, 0));
	fail_if(an_sparse_bitmap_contains(&bitmap, 1));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 2));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 3));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 4));
	fail_if(an_sparse_bitmap_contains(&bitmap, 5));

	fail_if(an_sparse_bitmap_contains(&bitmap, 9));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 10));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 12));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 13));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 14));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 15));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 16));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 19));
	fail_if(an_sparse_bitmap_contains(&bitmap, 20));

	fail_if(an_sparse_bitmap_contains(&bitmap, 24));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 25));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 29));
	fail_if(an_sparse_bitmap_contains(&bitmap, 30));

	an_sparse_bitmap_remove_range(&bitmap, 10, 20);
	fail_if(an_sparse_bitmap_contains(&bitmap, 9));
	fail_if(an_sparse_bitmap_contains(&bitmap, 10));
	fail_if(an_sparse_bitmap_contains(&bitmap, 12));
	fail_if(an_sparse_bitmap_contains(&bitmap, 13));
	fail_if(an_sparse_bitmap_contains(&bitmap, 14));
	fail_if(an_sparse_bitmap_contains(&bitmap, 15));
	fail_if(an_sparse_bitmap_contains(&bitmap, 16));
	fail_if(an_sparse_bitmap_contains(&bitmap, 19));
	fail_if(an_sparse_bitmap_contains(&bitmap, 20));

	an_sparse_bitmap_deinit(&bitmap);
} END_TEST

START_TEST(test_deletion)
{
	struct an_sparse_bitmap bitmap;
	an_sparse_bitmap_init(&bitmap, 0);
	an_sparse_bitmap_insert_range(&bitmap, 0, 1UL << 20);

	an_sparse_bitmap_remove_range(&bitmap, 2, 4);
	an_sparse_bitmap_remove_range(&bitmap, 4, 5);

	an_sparse_bitmap_remove_range(&bitmap, 10, 15);
	an_sparse_bitmap_remove_range(&bitmap, 13, 20);

	an_sparse_bitmap_remove_range(&bitmap, 25, 30);

	fail_if(!an_sparse_bitmap_contains(&bitmap, 0));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 1));
	fail_if(an_sparse_bitmap_contains(&bitmap, 2));
	fail_if(an_sparse_bitmap_contains(&bitmap, 3));
	fail_if(an_sparse_bitmap_contains(&bitmap, 4));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 5));

	fail_if(!an_sparse_bitmap_contains(&bitmap, 9));
	fail_if(an_sparse_bitmap_contains(&bitmap, 10));
	fail_if(an_sparse_bitmap_contains(&bitmap, 12));
	fail_if(an_sparse_bitmap_contains(&bitmap, 13));
	fail_if(an_sparse_bitmap_contains(&bitmap, 14));
	fail_if(an_sparse_bitmap_contains(&bitmap, 15));
	fail_if(an_sparse_bitmap_contains(&bitmap, 16));
	fail_if(an_sparse_bitmap_contains(&bitmap, 19));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 20));

	fail_if(!an_sparse_bitmap_contains(&bitmap, 24));
	fail_if(an_sparse_bitmap_contains(&bitmap, 25));
	fail_if(an_sparse_bitmap_contains(&bitmap, 29));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 30));

	an_sparse_bitmap_insert_range(&bitmap, 10, 20);

	fail_if(!an_sparse_bitmap_contains(&bitmap, 9));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 10));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 12));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 13));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 14));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 15));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 16));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 19));
	fail_if(!an_sparse_bitmap_contains(&bitmap, 20));

	an_sparse_bitmap_deinit(&bitmap);
} END_TEST

static int
cmp_u32 (const void *x, const void *y)
{
	uint32_t a = *(uint32_t*)x, b = *(uint32_t*)y;

	if (a < b) {
		return -1;
	}

	if (a == b) {
		return 0;
	}

	return 1;
}

static void
fill_with_shuffled_indices(size_t *indices, size_t n)
{

	for (size_t i = 0; i < n; i++) {
		indices[i] = i;
	}

	/* Fisher-Yates shuffle */
	for (size_t i = 0; i < n; i++) {
		size_t j, temp;

		j = i + (n-i) * an_drandom();
		temp = indices[i];
		indices[i] = indices[j];
		indices[j] = temp;
	}
}

#define M (1024*1024)
#define N 1024
START_TEST(test_random)
{
	struct an_sparse_bitmap bitmap;
	char *reference = malloc(M);
	uint32_t *needles = calloc(N, sizeof(uint32_t));
	size_t *indices = calloc(M, sizeof(size_t));

	fill_with_shuffled_indices(indices, M);
	for (size_t i = 0; i <= M; i = (i < 16)? i+1 : i*((i < 256)? 2 : 16)) {
		size_t multi_intersect;
		size_t nmulti;
		size_t sb_multi_intersect;

		an_sparse_bitmap_init(&bitmap, i/2);
		memset(reference, 0, M);
		/* Random insert/remove */
		for (size_t j = 0; j < i; j++) {
			int hi, lo, op;
			size_t n;

			lo = an_random_below(M);
			hi = lo + log(1 + an_rand());
			op = an_rand() % 2;
			if (hi > M) {
				hi = M;
			}

			n = hi - lo;

			memset(reference+lo, op, n);
			if (op != 0) {
				an_sparse_bitmap_insert_range(&bitmap, lo, hi);
			} else {
				an_sparse_bitmap_remove_range(&bitmap, lo, hi);
			}
		}

		double ticks = an_md_rdtsc();
		/* performance test */
		for (size_t j = 0; j < M; j++) {
			size_t index = indices[j];
			an_sparse_bitmap_contains(&bitmap, index);
		}
		ticks = an_md_rdtsc()-ticks;
		printf("Length %u: avg %.3f ticks\n",
		    bitmap.intervals.n_entries, ticks/M);

		/* Exhaustive test for point containment */
		for (size_t j = 0; j < M; j++) {
			size_t index = indices[j];
			bool ref = reference[index];
			bool sb;

			sb = an_sparse_bitmap_contains(&bitmap, index);
			if (sb != ref) {
				printf("Length %u, index %zu: an_sparse_bitmap_contains"
				    " returned %s rather than %s\n",
				    bitmap.intervals.n_entries, index,
				    sb?"true":"false", ref?"true":"false");
			}

			fail_if(sb != ref);
		}

		/* Single randomised test for intersect */
		nmulti = an_random_below(N);
		multi_intersect = -1UL;
		for (size_t j = 0; j < nmulti; j++) {
			uint32_t x;

			x = an_rand();
			needles[j] = x;
		}

		qsort(needles, nmulti, sizeof(uint32_t), cmp_u32);
		for (size_t j = 0; j < nmulti; j++) {
			uint32_t x = needles[j];
			if ((x < M) && (reference[x] != 0) &&
			    (j < multi_intersect)) {
				multi_intersect = j;
			}
		}

		ticks = an_md_rdtsc();
		sb_multi_intersect = an_sparse_bitmap_intersects(&bitmap, needles, nmulti);
		ticks = an_md_rdtsc() - ticks;
		if (sb_multi_intersect != multi_intersect) {
			printf("sparse bitmap/int set intersection, Length %u %zu: "
			    "an_sparse_bitmap_intersects returned %zu rather than %zu\n",
			    bitmap.intervals.n_entries, nmulti,
			    sb_multi_intersect, multi_intersect);
		}

		fail_if(multi_intersect != sb_multi_intersect);
		printf("Intersection %u/%zu: %.f ticks\n",
		    bitmap.intervals.n_entries, nmulti, ticks);
		an_sparse_bitmap_deinit(&bitmap);
	}

	free(reference);
	free(needles);
	free(indices);
} END_TEST

static void
test_foreach(const struct an_sparse_bitmap *bitmap, const uint32_t *values, size_t size)
{
	uint32_t cursor;
	size_t i;

	i = 0;
	AN_SPARSE_BITMAP_FOREACH(bitmap, cursor) {
		fail_if(i > size);
		fail_if(cursor != values[i]);
		++i;
	}

	ck_assert_int_eq(i, size);
}

START_TEST(test_foreach_single_interval)
{
	static const uint32_t values[] = { 10, 11, 12, 13, 14 };
	struct an_sparse_bitmap bitmap;

	an_sparse_bitmap_init(&bitmap, 1);
	an_sparse_bitmap_insert_range(&bitmap, 10, 15);
	test_foreach(&bitmap, values, ARRAY_SIZE(values));
	an_sparse_bitmap_deinit(&bitmap);
} END_TEST

START_TEST(test_foreach_multiple_intervals)
{
	static const uint32_t values[] = { 10, 11, 12, 13, 14, 27, 28, 29 };
	struct an_sparse_bitmap bitmap;

	an_sparse_bitmap_init(&bitmap, 1);
	an_sparse_bitmap_insert_range(&bitmap, 10, 15);
	an_sparse_bitmap_insert_range(&bitmap, 27, 30);
	test_foreach(&bitmap, values, ARRAY_SIZE(values));
	an_sparse_bitmap_deinit(&bitmap);
} END_TEST

START_TEST(test_foreach_skip_interval)
{
	static const uint32_t values[] = { 10, 11, 27, 28, 29 };
	struct an_sparse_bitmap bitmap;
	uint32_t cursor;
	size_t i, skip_count;

	an_sparse_bitmap_init(&bitmap, 1);
	an_sparse_bitmap_insert_range(&bitmap, 10, 15);
	an_sparse_bitmap_insert_range(&bitmap, 27, 30);

	skip_count = 0;
	i = 0;
	AN_SPARSE_BITMAP_FOREACH(&bitmap, cursor) {
		if (cursor > 11 && cursor < 15) {
			++skip_count;
			AN_SPARSE_BITMAP_SKIP_INTERVAL;
		}

		fail_if(i > ARRAY_SIZE(values));
		fail_if(cursor != values[i]);
		++i;
	}

	fail_if(i != ARRAY_SIZE(values));
	fail_if(skip_count != 1);

	an_sparse_bitmap_deinit(&bitmap);
} END_TEST

START_TEST(test_foreach_null)
{

	test_foreach(NULL, NULL, 0);
} END_TEST

int
main(int argc, char **argv)
{
	Suite *suite = suite_create("common/check_an_sparse_bitmap");

	an_malloc_init();
	an_md_probe();
	common_type_register();

	TCase *tc = tcase_create("test_an_sparse_bitmap");
	tcase_set_timeout(tc, 0);
	tcase_add_test(tc, test_insertion);
	tcase_add_test(tc, test_deletion);
	tcase_add_test(tc, test_random);
	tcase_add_test(tc, test_foreach_single_interval);
	tcase_add_test(tc, test_foreach_multiple_intervals);
	tcase_add_test(tc, test_foreach_skip_interval);
	tcase_add_test(tc, test_foreach_null);
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_sparse_bitmap.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return num_failed;
}
