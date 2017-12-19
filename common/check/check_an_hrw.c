#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "common/an_malloc.h"
#include "common/an_thread.h"
#include "common/an_hrw.h"
#include "common/common_types.h"
#include "common/server_config.h"
#include "common/util.h"

#define TEST_KEY_INLINE_LEN 30

struct hrw_test_struct_one {
	int id;
	char name[TEST_KEY_INLINE_LEN];
};

struct hrw_test_struct_two {
	int id;
	struct {
		int32_t field_one;
		int32_t field_two;
	} name;
};

AN_HRW_DICT(test_one_dict, struct hrw_test_struct_one, id, name);
AN_HRW_DICT(test_two_dict, struct hrw_test_struct_two, id, name);

#define COUNT 150
_Static_assert(COUNT >= 3, "check_an_hrw COUNT macro must be at least 3");

START_TEST(test_one)
{
	struct an_dict_test_one_dict dict;
	an_dict_init_test_one_dict(&dict, COUNT);
	struct hrw_test_struct_one *results[COUNT];

	for (size_t i = 0; i < COUNT; i++) {
		struct hrw_test_struct_one *ins = malloc(sizeof(struct hrw_test_struct_one));

		ins->id = i;
		sprintf(ins->name, "RESOURCE NUMBER %zd", i);

		an_dict_insert_test_one_dict(&dict, ins);
	}

	const char *key = "example_string_key";
	/* Get the weighted hash results */
	for (size_t i = 0; i < COUNT; i++) {
		struct hrw_test_struct_one *entry;
		entry = an_hrw_single_test_one_dict(&dict, key, strlen(key));

		results[i] = entry;

		/* Remove the dict entry */
		an_dict_remove_test_one_dict(&dict, entry);
	};

	/* Add back the lowest weight and highest, make sure the highest wins */
	an_dict_insert_test_one_dict(&dict, results[0]);
	an_dict_insert_test_one_dict(&dict, results[COUNT - 1]);

	struct hrw_test_struct_one *entry;
	entry = an_hrw_single_test_one_dict(&dict, key, strlen(key));
	fail_if(entry != results[0]);

	/* Add second lowest score, make sure highest still wins */
	an_dict_insert_test_one_dict(&dict, results[COUNT - 2]);
	entry = an_hrw_single_test_one_dict(&dict, key, strlen(key));
	fail_if(entry != results[0]);

	/* Remove the highest rank, make sure second-lowest wins */
	an_dict_remove_test_one_dict(&dict, results[0]);
	entry = an_hrw_single_test_one_dict(&dict, key, strlen(key));
	fail_if(entry != results[COUNT - 2]);

	/* Remove the lowest rank, make sure second-lowest wins */
	an_dict_remove_test_one_dict(&dict, results[COUNT - 1]);
	entry = an_hrw_single_test_one_dict(&dict, key, strlen(key));
	fail_if(entry != results[COUNT - 2]);

	/* Add back the highest rank, make sure it wins */
	an_dict_insert_test_one_dict(&dict, results[0]);
	entry = an_hrw_single_test_one_dict(&dict, key, strlen(key));
	fail_if(entry != results[0]);

	/* Free and shutdown */
	for (size_t i = 0; i < COUNT; i++) {
		free(results[i]);
	}

	an_dict_deinit_test_one_dict(&dict);
}
END_TEST

START_TEST(test_two)
{
	struct an_dict_test_two_dict dict;
	an_dict_init_test_two_dict(&dict, COUNT);
	struct hrw_test_struct_two *results[COUNT];

	for (size_t i = 0; i < COUNT; i++) {
		struct hrw_test_struct_two *ins = malloc(sizeof(struct hrw_test_struct_two));

		ins->id = i;
		ins->name.field_one = i % 8;
		ins->name.field_two = i;

		an_dict_insert_test_two_dict(&dict, ins);
	}

	const char *key = "example_string_key";
	/* Get the weighted hash results */
	for (size_t i = 0; i < COUNT; i++) {
		struct hrw_test_struct_two *entry;
		entry = an_hrw_single_test_two_dict(&dict, key, strlen(key));

		results[i] = entry;

		/* Remove the dict entry */
		an_dict_remove_test_two_dict(&dict, entry);
	};

	/* Add back the lowest weight and highest, make sure the highest wins */
	an_dict_insert_test_two_dict(&dict, results[0]);
	an_dict_insert_test_two_dict(&dict, results[COUNT - 1]);

	struct hrw_test_struct_two *entry;
	entry = an_hrw_single_test_two_dict(&dict, key, strlen(key));
	fail_if(entry != results[0]);

	/* Add second lowest score, make sure highest still wins */
	an_dict_insert_test_two_dict(&dict, results[COUNT - 2]);
	entry = an_hrw_single_test_two_dict(&dict, key, strlen(key));
	fail_if(entry != results[0]);

	/* Remove the highest rank, make sure second-lowest wins */
	an_dict_remove_test_two_dict(&dict, results[0]);
	entry = an_hrw_single_test_two_dict(&dict, key, strlen(key));
	fail_if(entry != results[COUNT - 2]);

	/* Remove the lowest rank, make sure second-lowest wins */
	an_dict_remove_test_two_dict(&dict, results[COUNT - 1]);
	entry = an_hrw_single_test_two_dict(&dict, key, strlen(key));
	fail_if(entry != results[COUNT - 2]);

	/* Add back the highest rank, make sure it wins */
	an_dict_insert_test_two_dict(&dict, results[0]);
	entry = an_hrw_single_test_two_dict(&dict, key, strlen(key));
	fail_if(entry != results[0]);

	/* Free and shutdown */
	for (size_t i = 0; i < COUNT; i++) {
		free(results[i]);
	}

	an_dict_deinit_test_two_dict(&dict);
}
END_TEST

START_TEST(test_three)
{
	struct an_dict_test_two_dict dict;
	an_dict_init_test_two_dict(&dict, COUNT);
	struct hrw_test_struct_two *results[COUNT];

	for (size_t i = 0; i < COUNT; i++) {
		struct hrw_test_struct_two *ins = malloc(sizeof(struct hrw_test_struct_two));

		ins->id = i;
		ins->name.field_one = i;
		ins->name.field_two = i % 8;

		an_dict_insert_test_two_dict(&dict, ins);
	}

	const uint32_t key_val = 0x11223344;
	const uint32_t * const key = &key_val;
	/* Get the weighted hash results */
	for (size_t i = 0; i < COUNT; i++) {
		struct hrw_test_struct_two *entry;
		entry = an_hrw_single_test_two_dict(&dict, key, sizeof(key));

		results[i] = entry;

		/* Remove the dict entry */
		an_dict_remove_test_two_dict(&dict, entry);
	};

	/* Add back the lowest weight and highest, make sure the highest wins */
	an_dict_insert_test_two_dict(&dict, results[0]);
	an_dict_insert_test_two_dict(&dict, results[COUNT - 1]);

	struct hrw_test_struct_two *entry;
	entry = an_hrw_single_test_two_dict(&dict, key, sizeof(key));
	fail_if(entry != results[0]);

	/* Add second lowest score, make sure highest still wins */
	an_dict_insert_test_two_dict(&dict, results[COUNT - 2]);
	entry = an_hrw_single_test_two_dict(&dict, key, sizeof(key));
	fail_if(entry != results[0]);

	/* Remove the highest rank, make sure second-lowest wins */
	an_dict_remove_test_two_dict(&dict, results[0]);
	entry = an_hrw_single_test_two_dict(&dict, key, sizeof(key));
	fail_if(entry != results[COUNT - 2]);

	/* Remove the lowest rank, make sure second-lowest wins */
	an_dict_remove_test_two_dict(&dict, results[COUNT - 1]);
	entry = an_hrw_single_test_two_dict(&dict, key, sizeof(key));
	fail_if(entry != results[COUNT - 2]);

	/* Add back the highest rank, make sure it wins */
	an_dict_insert_test_two_dict(&dict, results[0]);
	entry = an_hrw_single_test_two_dict(&dict, key, sizeof(key));
	fail_if(entry != results[0]);

	/* Free and shutdown */
	for (size_t i = 0; i < COUNT; i++) {
		free(results[i]);
	}

	an_dict_deinit_test_two_dict(&dict);
}
END_TEST

int
main(int argc, char **argv)
{
	server_config_init("check_an_hrw_hash", argc, argv);
	load_server_config(NULL, true);

	Suite *suite = suite_create("common/check_an_hrw_hash");
	TCase *tc = tcase_create("test_an_hrw_hash");

	tcase_add_test(tc, test_one);
	tcase_add_test(tc, test_two);
	tcase_add_test(tc, test_three);
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_an_hrw_hash.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return num_failed;
}
