#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "common/an_interpolation_table.h"
#include "common/server_config.h"

struct node {
	uint64_t key;
	uint64_t not_a_key;
	char also_not_a_key;
};

struct other_node {
	uint64_t key;
};

static inline int
node_comparator(const struct node *a, const struct node *b)
{

	if (a->key > b->key) {
		return 1;
	}

	if (a->key < b->key) {
		return -1;
	}

	return 0;
}

static inline int
other_node_comparator(const struct other_node *a, const struct other_node *b)
{

	if (a->key > b->key) {
		return 1;
	}

	if (a->key < b->key) {
		return -1;
	}

	return 0;
}

BSEARCH_DEFINE(node_bsearch, node, node_comparator);
BSEARCH_DEFINE(other_node_bsearch, other_node, other_node_comparator);

int
comp(const uint64_t *a, const uint64_t *b)
{
	if (*a == *b) {
		return 0;
	}
	return (*a > *b) ? 1 : -1;
}

/**
 * @brief key function that maps arbitrary structs to uint64_t
 * assumes that the first element is a uint64_t key :)
 */
uint64_t
key_fn(const void *thing)
{

	return *(uint64_t *)thing;
}

#define MAX_N_ELEM 10

struct an_interpolation_table_test {
	uint64_t sorted[MAX_N_ELEM];
	int n_buckets;
	uint64_t search;
	bool exists;
	bool empty_or_single_val;
	bool too_low;
	bool too_high;
};

struct an_interpolation_table_test test_an_interpolation_table_data[] = {
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 10, 600, true, true, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 10, 10, true, false, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 10, 550, false, true, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 10, 7, false, false, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 10, 800, true, true, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 500, 500, 800 }, 7, 800, true, true, false, false },
	{ { 5, 10, 100, 200, 300, 400, 500, 600, 700, 805 }, 10, 0, false, true, true, false },
	{ { 5, 10, 100, 200, 300, 400, 500, 600, 700, 805 }, 10, 1000, false, true, false, true },
	{ { 0, 10, 100, 200, 202, 400, 500, 600, 700, 800 }, 10, 202, true, false, false, false },
	{ { 0, 10, 100, 200, 202, 400, 500, 600, 700, 800 }, 10, 300, false, true, false, false },
	{ { 0, 10, 100, 200, 202, 400, 500, 500, 500, 800 }, 10, 500, true, true, false, false },
	{ { 0, 10, 100, 200, 202, 400, 500, 500, 500, 800 }, 10, 700, false, true, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 1000, 600, true, true, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 1000, 10, true, true, false, false },
	{ { 0, 10, 100, 200, 300, 400, 500, 600, 700, 800 }, 0, 10, true, false, false, false },
	{ { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 }, 10, 100, true, true, false, false },
	{ { 100, 100, 100, 100, 100, 100, 100, 101, 101, 101 }, 10, 100, true, false, false, false },
	{ { 0, 100, 1000, 20000, 300000, 400000, 500000, 60000000, 700000000000, UINT64_MAX }, 10, 10, false, false, false, false },
	{ { 0, 100, 1000, 20000, 300000, 400000, 500000, 60000000, 700000000000, UINT64_MAX }, 10, UINT64_MAX, true, true, false, false },
	{ { 0, 100, 1000, 20000, 300000, 400000, 500000, 60000000, 700000000000, UINT64_MAX }, 10, 0, true, false, false, false },
};

START_TEST(test_an_interpolation_table_basic_cases) {
	struct an_interpolation_table at;
	struct other_node search;

	struct an_interpolation_table_test test = test_an_interpolation_table_data[_i];

	an_interpolation_table_init(&at, test.sorted, MAX_N_ELEM, test.n_buckets, key_fn);

	uint32_t ret[2];

	fail_if(an_interpolation_table_get_indices(&at, test.search, &ret[0], &ret[1]) != test.empty_or_single_val);
	search.key = test_an_interpolation_table_data[_i].search;
	struct other_node *result = BSEARCH(other_node_bsearch, (struct other_node *)&test.sorted[ret[0]], ret[1] - ret[0], &search);
	fail_if(result == NULL && test.exists != false);
	fail_if(result != NULL && test.exists == false);

	if (test.too_low || test.too_high) {
		fail_if(result != NULL);
		fail_if(ret[0] != ret[1]);
		fail_if(test.too_low == true && ret[1] != 0);
		fail_if(test.too_high == true && ret[0] != MAX_N_ELEM);
	}

	if (result != NULL) {
		fail_if(result->key != test.search);
	}

	an_interpolation_table_deinit(&at);
}
END_TEST

START_TEST(test_an_interpolation_table_null_case) {
	struct an_interpolation_table at;

	an_interpolation_table_init(&at, NULL, 10, 10, key_fn);
	fail_if(at.delta != UINT64_MAX);
	an_interpolation_table_deinit(&at);

	an_interpolation_table_init(&at, (void *)1, 0, 10, key_fn);
	fail_if(at.delta != UINT64_MAX);
	an_interpolation_table_deinit(&at);
}
END_TEST

/**
 * @brief tests an_interpolation_table with a struct other_node (just a uint64) that may have
 * empty buckets, or also single value buckets
 */
void
check_an_interpolation_table(struct other_node *sorted, uint32_t n_elem, uint64_t n_buckets)
{
	struct an_interpolation_table at;
	struct other_node search;
	an_interpolation_table_init(&at, sorted, n_elem, n_buckets, key_fn);
	uint32_t ret[2];

	/* Check that all values known to be in the sorted array are found correctly */
	for (uint32_t i = 0; i < n_elem; i++) {
		an_interpolation_table_get_indices(&at, sorted[i].key, &ret[0], &ret[1]);

		/* verify that the interpolation table gives a range */
		fail_if(ret[0] == ret[1] && (ret[0] == 0 || ret[0] == n_elem));

		/* actually search the range and verify that it is correct */
		search.key = sorted[i].key;
		struct other_node *binary_search_result = BSEARCH(other_node_bsearch, &sorted[ret[0]], ret[1] - ret[0], &search);
		fail_if(binary_search_result == NULL);

		fail_if(binary_search_result->key != search.key);
	}
	/* Check a subset of possible keys to make sure that we don't report false positives */
	for (uint64_t i = 0; i < 100000; i++) {
		an_interpolation_table_get_indices(&at, i, &ret[0], &ret[1]);
		search.key = i;

		struct other_node *interpolation_search_result = BSEARCH(other_node_bsearch, &sorted[ret[0]], ret[1] - ret[0], &search);
		struct other_node *binary_search_result = BSEARCH(other_node_bsearch, sorted, n_elem, &search);
		/* Check whether one was NULL and the other was not */
		fail_if(((interpolation_search_result == NULL) ^ (binary_search_result == NULL)) != 0 );

		/* Check that both point to the same value */
		if (interpolation_search_result == binary_search_result) {
			continue;
		}
		fail_if(interpolation_search_result->key != binary_search_result->key);
	}

	an_interpolation_table_deinit(&at);
}


void
check_an_interpolation_table_arbitrary_types(struct node *sorted, uint32_t n_elem, uint64_t n_buckets)
{
	struct an_interpolation_table at;
	struct node search;
	an_interpolation_table_init(&at, sorted, n_elem, n_buckets, key_fn);
	uint32_t ret[2];

	/* Check that all values known to be in the sorted array are found correctly */
	for (uint32_t i = 0; i < n_elem; i++) {
		an_interpolation_table_get_indices(&at, sorted[i].key, &ret[0], &ret[1]);

		/* verify that the interpolation table gives a range */
		fail_if(ret[0] == ret[1] && (ret[0] == 0 || ret[0] == n_elem));

		/* actually search the range and verify that it is correct */
		search.key = sorted[i].key;
		struct node *binary_search_result = BSEARCH(node_bsearch, &sorted[ret[0]], ret[1] - ret[0], &search);
		fail_if(binary_search_result == NULL);

		fail_if(binary_search_result->key != search.key);
	}
	/* Check a subset of possible keys to make sure that we don't report false positives */
	for (uint64_t i = 0; i < 100000; i++) {
		if (an_interpolation_table_get_indices(&at, i, &ret[0], &ret[1]) == true) {
			/* this should be an empty bucket only */
			fail_if(ret[0] != ret[1]);
		} else {
			/* should be a non-empty, many value bucket */
			fail_if(ret[0] == ret[1]);
		}

		search.key = i;

		struct node *interpolation_search_result = BSEARCH(node_bsearch, &sorted[ret[0]], ret[1] - ret[0], &search);
		struct node *binary_search_result = BSEARCH(node_bsearch, sorted, n_elem, &search);
		/* Check whether one was NULL and the other was not */
		fail_if(((interpolation_search_result == NULL) ^ (binary_search_result == NULL)) != 0 );

		/* Check that both point to the same value */
		if (interpolation_search_result == binary_search_result) {
			continue;
		}
		fail_if(interpolation_search_result->key != binary_search_result->key);
	}

	an_interpolation_table_deinit(&at);
}

START_TEST(test_an_interpolation_table) {

	uint32_t n_elem = 1000;
	uint64_t *sorted = calloc(n_elem, sizeof(uint64_t));

	for (uint32_t i = 0; i < n_elem; i++) {
		sorted[i] = an_random_below(1000000);
	}

	qsort(sorted, n_elem, sizeof(uint64_t), AN_CC_CAST_COMPARATOR(comp, uint64_t));
	check_an_interpolation_table((struct other_node *)sorted, n_elem, 10);
	check_an_interpolation_table((struct other_node *)sorted, n_elem, 31);
	check_an_interpolation_table((struct other_node *)sorted, n_elem, 0);

	/* Check empty buckets case */
	for (uint32_t i = 0; i < n_elem; i++) {
		if (i % 2 == 0) {
			sorted[i] = an_random_below(600000);
		} else {
			sorted[i] = (an_random_below(100000) + 900000);
		}
	}

	qsort(sorted, n_elem, sizeof(uint64_t), AN_CC_CAST_COMPARATOR(comp, uint64_t));
	check_an_interpolation_table((struct other_node *)sorted, n_elem, 0);
	check_an_interpolation_table((struct other_node *)sorted, n_elem, 55);
	check_an_interpolation_table((struct other_node *)sorted, n_elem, 10);

	free(sorted);

	struct node *narray = calloc(n_elem, sizeof(struct node));

	for (uint32_t i = 0; i < n_elem; i++) {
		narray[i].key = an_random_below(1000000);
	}

	qsort(narray, n_elem, sizeof(struct node), AN_CC_CAST_COMPARATOR(comp, uint64_t));
	check_an_interpolation_table_arbitrary_types(narray, n_elem, 10);
	check_an_interpolation_table_arbitrary_types(narray, n_elem, 79);
	check_an_interpolation_table_arbitrary_types(narray, n_elem, 0);

}
END_TEST

int main(int argc, char **argv)
{
	server_config_init("check_an_interpolation_table", argc, argv);
	Suite *suite = suite_create("common/an_interpolation_table");


	TCase *tc = tcase_create("test_an_interpolation_table");
	tcase_add_loop_test(tc, test_an_interpolation_table_basic_cases, 0, ARRAY_SIZE(test_an_interpolation_table_data));
	tcase_add_test(tc, test_an_interpolation_table_null_case);
	tcase_add_test(tc, test_an_interpolation_table);

	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_interpolation_table.xml");
	srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return num_failed;
}
