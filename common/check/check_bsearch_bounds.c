#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <check.h>
#include <time.h>
#include <sys/stat.h>

#include "common/an_malloc.h"
#include "common/btree.h"
#include "common/common_types.h"
#include "common/util.h"


BTREE_CONTEXT_DEFINE(test_btree_ctx, "test_bsearch_bounds_ctx");

struct key
{
	int k1, k2;
};

static inline int
key_cmp(const struct key *lhs, const struct key *rhs)
{
	int rc = lhs->k1 - rhs->k1;

	if (rc != 0) {
		return rc;
	}
	return lhs->k2 - rhs->k2;
}

static int
key_k1_cmp(const struct key *lhs, const struct key *rhs)
{

	return lhs->k1 - rhs->k1;
}

BSEARCH_BOUNDS_DEFINE(key, key, key_k1_cmp);

void
test_bounds_val(struct binary_tree *tree, const struct key *k, size_t lower_idx, size_t upper_idx,
				const char *ctx, int line)
{
	size_t array_sz;

	printf("%s: testing key {%d, %d} bound indexes\n", __FUNCTION__, k->k1, k->k2);
	const struct key *array = btree_array_const_get(tree, &array_sz);
	const struct key *lbound = BSEARCH_LOWER_BOUND_CONST(key, array, array_sz, k);
	ck_assert_msg(lbound == &array[lower_idx], "lower bound is expected at index %d, observed at %d (%s:%d)",
				  lower_idx, lbound - array, ctx, line);

	const struct key *ubound = BSEARCH_UPPER_BOUND_CONST(key, array, array_sz, k);
	ck_assert_msg(ubound == &array[upper_idx], "upper bound is expected at index %d, observed at %d (%s:%d)",
				  upper_idx, ubound - array, ctx, line);
	printf("%s: key {%d, %d} bound indexes: (%zu, %zu)\n", __FUNCTION__, k->k1, k->k2, lbound - array, ubound - array);

	{
		struct key *array = btree_array_get(tree, &array_sz);
		BSEARCH_EQUAL_RANGE_RC_TYPE(key) eq_range = BSEARCH_EQUAL_RANGE(key, array, array_sz, k);
		ck_assert_msg(eq_range.lower_bound == lbound, "equal_range.lower_bound must equal BSEARCH_LOWER_BOUND outcome, but observed result is %d entrie(s) away (%s:%d)",
			eq_range.lower_bound - lbound, ctx, line);
		ck_assert_msg(eq_range.upper_bound == ubound, "equal_range.upper_bound must equal BSEARCH_UPPER_BOUND outcome, but observed result is %d entrie(s) away (%s:%d)",
			eq_range.upper_bound - ubound, ctx, line);
	}

	{
		const struct key *array = btree_array_const_get(tree, &array_sz);
		BSEARCH_EQUAL_RANGE_RC_CONST_TYPE(key) eq_range = BSEARCH_EQUAL_RANGE_CONST(key, array, array_sz, k);
		ck_assert_msg(eq_range.lower_bound == lbound, "equal_range.lower_bound must equal BSEARCH_LOWER_BOUND outcome, but observed result is %d entrie(s) away (%s:%d)",
			eq_range.lower_bound - lbound, ctx, line);
		ck_assert_msg(eq_range.upper_bound == ubound, "equal_range.upper_bound must equal BSEARCH_UPPER_BOUND outcome, but observed result is %d entrie(s) away (%s:%d)",
			eq_range.upper_bound - ubound, ctx, line);
	}
}

static void dump_btree(const struct binary_tree *tree)
{
	struct key *cursor;

	printf("%s: Testing btree array (size:%zu):\n", __FUNCTION__, btree_item_count(tree));
	BTREE_FOREACH(tree, cursor) {
		printf( "\t[%zu]: {%d, %d}\n", _i, cursor->k1, cursor->k2);
	}
}

START_TEST(test_bsearch_bounds)
{
	struct binary_tree tree;

	btree_init(&tree, test_btree_ctx, struct key, 5, key_cmp, NULL);

	dump_btree(&tree);
	test_bounds_val(&tree, &(struct key){.k1 = 1}, 0, 0, "{.k1=1}", __LINE__);

	btree_insert(&tree, &(struct key){.k1 = 0, .k2 = 0});
	dump_btree(&tree);
	test_bounds_val(&tree, &(struct key){.k1 = -1}, 		 0, 0, "{.k1=-1}", __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 0},			 0, 1, "{.k1=0}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 1},			 1, 1, "{.k1=1}",  __LINE__);

	btree_insert(&tree, &(struct key){.k1 = 1, .k2 = 11});
	dump_btree(&tree);
	test_bounds_val(&tree, &(struct key){.k1 = -1}, 		 0, 0, "{.k1=-1}", __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 0},			 0, 1, "{.k1=0}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 1},			 1, 2, "{.k1=1}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 2},			 2, 2, "{.k1=2}",  __LINE__);

	btree_insert(&tree, &(struct key){.k1 = 1, .k2 = 12});
	btree_insert(&tree, &(struct key){.k1 = 3, .k2 = 3});

	dump_btree(&tree);
	test_bounds_val(&tree, &(struct key){.k1 = -1}, 		 0, 0, "{.k1=-1}", __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = -1, .k2 = 5}, 0, 0, "{.k1=-1}", __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 0},			 0, 1, "{.k1=0}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 1},			 1, 3, "{.k1=1}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 1, .k2 = 5},	 1, 3, "{.k1=1}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 2},			 3, 3, "{.k1=2}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 3},			 3, 4, "{.k1=3}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 3, .k2 = 5},	 3, 4, "{.k1=3}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 5},			 4, 4, "{.k1=5}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 5, .k2 = 5},  4, 4, "{.k1=5}",  __LINE__);

	btree_insert(&tree, &(struct key){.k1 = 3, .k2 = 32});
	btree_insert(&tree, &(struct key){.k1 = 3, .k2 = 33});
	btree_insert(&tree, &(struct key){.k1 = 5, .k2 = 51});
	btree_insert(&tree, &(struct key){.k1 = 5, .k2 = 52});
	dump_btree(&tree);

	test_bounds_val(&tree, &(struct key){.k1 = 3}, 3, 6, "{.k1=3}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 4}, 6, 6, "{.k1=3}",  __LINE__);
	test_bounds_val(&tree, &(struct key){.k1 = 5}, 6, 8, "{.k1=3}",  __LINE__);
}
END_TEST

int
main(int argc, char** argv)
{
	Suite* suite = suite_create("common/check_bsearch_bounds");

	an_malloc_init();
	common_type_register();

	TCase* tc = tcase_create("test_keyval_info");
	tcase_add_test(tc, test_bsearch_bounds);
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_bsearch_bounds");
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return num_failed;
}
