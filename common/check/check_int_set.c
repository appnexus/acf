#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <check.h>

#include "common/an_malloc.h"
#include "common/common_types.h"
#include "common/int_set.h"
#include "common/util.h"

START_TEST(test_addremove) {
	static BTREE_CONTEXT_DEFINE(context, "addremove");

	for (int bytes = 2; bytes <= 8; bytes *= 2) {
		int_set_t *list = new_int_set(context, bytes, 32);

		// insert even numbers up to 100 (force a realloc)
		for (int i = 0; i < 100; i += 2) {
			add_int_to_set(list, i);
			fail_if(int_set_index(list, i / 2) != i);
		}

		// remove multiples of 10
		for (int i = 0; i < 100; i += 10) {
			remove_int_from_set(list, i);
		}

		// add odd numbers backward
		for (int i = 99; i > 0; i -= 2) {
			add_int_to_set(list, i);
		}

		// test
		for (int i = 0; i < 100; i++) {
			if (i % 2) {
				fail_if(!int_set_contains(list, i));
			} else if (i % 10 == 0) {
				fail_if(int_set_contains(list, i));
			} else {
				fail_if(!int_set_contains(list, i));
			}
		}

		free_int_set(list);
	}
}
END_TEST

START_TEST(test_uniq) {
	static BTREE_CONTEXT_DEFINE(context, "uniq");

	for (int bytes = 2; bytes <= 8; bytes *= 2) {
		int_set_t *list = NULL;
		add_int_to_set_init(&list, context, 1234, sizeof(int));
		fail_if(int_set_count(list) != 1);
		add_int_to_set(list, 1234);
		add_int_to_set(list, 1234);
		fail_if(int_set_count(list) != 1);
		remove_int_from_set(list, 1234);
		fail_if(int_set_count(list) != 0);

		while (int_set_count(list) < 32) {
			add_int_to_set(list, an_rand());
		}
		int prev_element = int_set_index(list, 0);
		for (int i = 1; i < 32; i++) {
			int elem = int_set_index(list, i);
			fail_if(elem <= prev_element);
			prev_element = elem;
		}

		free_int_set(list);
	}
}
END_TEST

#ifdef TEST_PERFORMANCE
enum perf_type {PERF_RAND, PERF_SORTED, PERF_REVERSED};

static void run_perf_test(enum perf_type type) {
	static BTREE_CONTEXT_DEFINE(perf_context, "perf");
	uint64_t s, e;

	for (int bytes = 2; bytes <= 8; bytes *= 2) {
		for (int num_elements = 16; num_elements <= 60000; num_elements *= 2) {
			int_set_t *list = new_int_set(perf_context, bytes, num_elements);

			s = an_md_rdtsc();
			for (int i = 0; i < num_elements; i++) {
				if (type == PERF_RAND) {
					add_int_to_set(list, an_rand());
				} else if (type == PERF_SORTED) {
					add_int_to_set(list, i);
				} else {
					add_int_to_set(list, num_elements - i);
				}
			}
			e = an_md_rdtsc();

			uint64_t elapsed_us = e - s;
			clear_int_set(list);

			int_set_postpone_sorting(list, 0);
			s = an_md_rdtsc();
			for (int i = 0; i < num_elements; i++) {
				if (type == PERF_RAND) {
					add_int_to_set(list, an_rand());
				} else if (type == PERF_SORTED) {
					add_int_to_set(list, i);
				} else {
					add_int_to_set(list, num_elements - i);
				}
			}
			e = an_md_rdtsc();
			int_set_resume_sorting(list);

			uint64_t elapsed_us_post = e - s;

			s = an_md_rdtsc();
			for (int i = 0; i < num_elements; i++) {
				int_set_contains(list, -1);
				__asm__ __volatile__("" ::: "memory");
				int_set_contains(list, -1);
				__asm__ __volatile__("" ::: "memory");
				int_set_contains(list, -1);
				__asm__ __volatile__("" ::: "memory");
				int_set_contains(list, -1);
				__asm__ __volatile__("" ::: "memory");
			}
			e = an_md_rdtsc();

			uint64_t elapsed_contains = (e - s) / 4;

			printf(" Sorted %d-bit int array with %d elements took %" PRIu64 " ticks "
			    "/  %" PRIu64 " ticks bulk / %" PRIu64 " ticks search\n",
			    bytes * 8, num_elements, elapsed_us,
			    elapsed_us_post,
			    elapsed_contains);
			free_int_set(list);
		}
	}
}

START_TEST(test_perf) {
	printf("List of random ints\n");
	run_perf_test(PERF_RAND);
	printf("List of sort ints\n");
	run_perf_test(PERF_SORTED);
	printf("List of reversed ints\n");
	run_perf_test(PERF_REVERSED);
}
END_TEST

#endif

START_TEST(test_qsort) {
	int num_elements = 10000;
	int list[num_elements];
	struct timeval start_tv;
	gettimeofday(&start_tv, NULL);
	for (int i = 0; i < num_elements; i++) {
		list[i] = an_rand();
	}
	qsort(list, num_elements, sizeof(int), int_comparator_asc);
	struct timeval end_tv;
	gettimeofday(&end_tv, NULL);
	double elapsed_us = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 + end_tv.tv_usec -
			start_tv.tv_usec;
	printf("Create qsorted int array with %d elements took %.2f ms\n",
			num_elements, elapsed_us / 1000.0);
}
END_TEST

START_TEST(test_append) {
	static BTREE_CONTEXT_DEFINE(context, "append");

	for (int bytes = 2; bytes <= 8; bytes *= 2) {
		int_set_t *list = new_int_set(context, bytes, 16);
		for (int i = 0; i < 8; i++) {
			add_int_to_set(list, i);
		}
		fail_if(int_set_count(list) != 8);
		bstring str = bfromcstr("");
		bappend_int_set(str, list);
		char *ss = bdata(str);
		fail_if(strcmp(ss, "0,1,2,3,4,5,6,7"));
		bdestroy(str);
		struct evbuffer *buf = evbuffer_new();
		evbuffer_append_int_set(buf, list);
		fail_if(strcmp((char *)EVBUFFER_DATA(buf), "0,1,2,3,4,5,6,7"));
		evbuffer_free(buf);
		free_int_set(list);
	}
}
END_TEST

START_TEST(test_append_bulk_dups) {
	static BTREE_CONTEXT_DEFINE(context, "append_bulk_dups");

	for (int bytes = 2; bytes <= 8; bytes *= 2) {
		int_set_t *list = new_int_set(context, bytes, 16);
		for (int i = 0; i < 8; i++) {
			add_int_to_set(list, i);
		}
		fail_if(int_set_count(list) != 8);
		bstring str = bfromcstr("");
		bappend_int_set(str, list);
		char *ss = bdata(str);
		fail_if(strcmp(ss, "0,1,2,3,4,5,6,7"));
		bdestroy(str);
		struct evbuffer *buf = evbuffer_new();
		evbuffer_append_int_set(buf, list);
		fail_if(strcmp((char *)EVBUFFER_DATA(buf), "0,1,2,3,4,5,6,7"));
		evbuffer_free(buf);

		int_set_postpone_sorting(list, 0);
		add_int_to_set(list, 4);
		add_int_to_set(list, 15);
		add_int_to_set(list, 13);
		add_int_to_set(list, 15);
		add_int_to_set(list, 11);
		int_set_resume_sorting(list);
		str = bfromcstr("");
		bappend_int_set(str, list);
		ss = bdata(str);
		fail_if(strcmp(ss, "0,1,2,3,4,5,6,7,11,13,15"));
		bdestroy(str);
		buf = evbuffer_new();
		evbuffer_append_int_set(buf, list);
		fail_if(strcmp((char *)EVBUFFER_DATA(buf), "0,1,2,3,4,5,6,7,11,13,15"));
		evbuffer_free(buf);

                int_set_postpone_sorting(list, 0);
                add_int_to_set(list, 2);
                add_int_to_set(list, 9);
                add_int_to_set(list, 8);
                add_int_to_set(list, 8);
                add_int_to_set(list, 21);
                int_set_resume_sorting(list);
                str = bfromcstr("");
                bappend_int_set(str, list);
                ss = bdata(str);
                fail_if(strcmp(ss, "0,1,2,3,4,5,6,7,8,9,11,13,15,21"));
                bdestroy(str);
                buf = evbuffer_new();
                evbuffer_append_int_set(buf, list);
                fail_if(strcmp((char *)EVBUFFER_DATA(buf), "0,1,2,3,4,5,6,7,8,9,11,13,15,21"));
                evbuffer_free(buf);

		free_int_set(list);
	}
}
END_TEST

START_TEST(test_intersect_behavior) {
	static BTREE_CONTEXT_DEFINE(context, "test_intersect_behavior");
	int_set_t *set1;
	int_set_t *set2;

	// teset success
	for ( int i= 0; i < 3; i++) {
		set1 = new_int_set(context,2 << i , 16);
		set2 = new_int_set(context,2 << i , 16);
		fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
		for ( int i = 0; i < 16; i++) {
			add_int_to_set(set1, (8-i) * 100);
			add_int_to_set(set2, (16-i) * 100);
		}
		fail_if(!int_set_intersect(set1,set2),"Intset should intersect ");
		fail_if(!int_set_intersect(set2,set1),"Intset should intersect ");
		free_int_set(set1);
		free_int_set(set2);
	}
	// test failure
	for ( int i= 0; i < 3; i++) {
		set1 = new_int_set(context,2 << i , 16);
		set2 = new_int_set(context,2 << i , 16);
		fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
		for ( int i = 0; i < 16; i++) {
			add_int_to_set(set1, (8-i) * 100);
			add_int_to_set(set2, (16-i) * 100 + 1);
		}
		fail_if(int_set_intersect(set1,set2),"Intset should not intersect ");
		fail_if(int_set_intersect(set2,set1),"Intset should not intersect ");
		free_int_set(set1);
		free_int_set(set2);
	}
	// test failure  for non overlapping
	for ( int i= 0; i < 3; i++) {
		set1 = new_int_set(context,2 << i , 16);
		set2 = new_int_set(context,2 << i , 16);
		fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
		for ( int i = 0; i < 16; i++) {
			add_int_to_set(set1, (8-i) * 100);
			add_int_to_set(set2, (16 * 100) + 1);
		}
		fail_if(int_set_intersect(set1,set2),"Intset should not intersect ");
		fail_if(int_set_intersect(set2,set1),"Intset should not intersect ");
		free_int_set(set1);
		free_int_set(set2);
	}
       // test  intersect on first
        for ( int i= 0; i < 3; i++) {
                set1 = new_int_set(context,2 << i , 16);
                set2 = new_int_set(context,2 << i , 16);
                fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
                for ( int i = 0; i < 15; i++) {
                        add_int_to_set(set1, i * 100);
                        add_int_to_set(set2, 16 * 10000 );
                }
	        add_int_to_set(set1, int_set_index(set2,0));
                fail_if(!int_set_intersect(set1,set2),"Intset should not intersect ");
                fail_if(!int_set_intersect(set2,set1),"Intset should not intersect ");
                free_int_set(set1);
                free_int_set(set2);
        }
	 // test  intersect on last
        for ( int i= 0; i < 3; i++) {
                set1 = new_int_set(context,2 << i , 16);
                set2 = new_int_set(context,2 << i , 16);
                fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
                for ( int i = 0; i < 15; i++) {
                        add_int_to_set(set1, i * 100);
                        add_int_to_set(set2, 16 * 10000 );
                }
                add_int_to_set(set2, int_set_index(set1,14));
                fail_if(!int_set_intersect(set1,set2),"Intset should not intersect ");
                fail_if(!int_set_intersect(set2,set1),"Intset should not intersect ");
                free_int_set(set1);
                free_int_set(set2);
        }
	// test	 bad parameters
	{
                set1 = new_int_set(context,sizeof(long) , 16);
                set2 = new_int_set(context,sizeof(long) , 16);
                fail_if(int_set_intersect(set1,set2),"Intset should not intersect ");
                fail_if(int_set_intersect(set1,NULL),"Intset should not intersect ");
                fail_if(int_set_intersect(NULL,set2),"Intset should not intersect ");
                fail_if(int_set_intersect(NULL,NULL),"Intset should not intersect ");
                fail_if(int_set_intersect(set1,set1),"Intset should not intersect ");
		add_int_to_set(set1, 0);
		add_int_to_set(set2, 1);
                fail_if(int_set_intersect(set1,set2),"Intset should not intersect ");
                fail_if(int_set_intersect(set1,NULL),"Intset should not intersect ");
                fail_if(int_set_intersect(NULL,set2),"Intset should not intersect ");
                fail_if(int_set_intersect(NULL,NULL),"Intset should not intersect ");
                fail_if(!int_set_intersect(set1,set1),"Intset should intersect ");
                free_int_set(set1);
                free_int_set(set2);
        }

}
END_TEST
/*
 * 256K max integer size and max integer set size
 * Unclear that more range is needed to test and larger ranges
 * preclude verification with a trivial bitmap.
 */
#define INT_SET_MAX		(1 << 18)
#define INTERSECT_ITER	4

START_TEST(test_intersect) {
	static BTREE_CONTEXT_DEFINE(context, "intersect");
	uint64_t s, e;
	int iter, int_set_num;
	bool passed = true;

	printf("Actually testing intersect.\n");
	for (int_set_num = 16; int_set_num < INT_SET_MAX; int_set_num *= 2) {
		for (iter = 0; iter < INTERSECT_ITER; ++iter) {
			bool bitmap_status, int_set_status;
			int i;
			unsigned char *bitmap_1, *bitmap_2;
			int_set_t *set_1, *set_2;

			bitmap_1 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_1 == NULL) {
				fprintf(stderr, "bitmap_1 allocation failure in %s\n", __FUNCTION__);
				return;
			}
			bitmap_2 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_2 == NULL) {
				fprintf(stderr, "bitmap_2 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_1;
			}
			set_1 = new_int_set(context, sizeof(long), 16);
			if (set_1 == NULL) {
				fprintf(stderr, "set_1 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_2;
			}
			set_2 = new_int_set(context, sizeof(long), 16);
			if (set_2 == NULL) {
				fprintf(stderr, "set_2 allocation failure in %s\n", __FUNCTION__);
				goto free_set_1;
			}
			/* FIXME: get a better seed */
			an_srand(7485539959361970041UL);
			for (i = 0; i < int_set_num; ++i) {
				long n;
				ldiv_t quotrem;

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_1[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_1, n);

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_2[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_2, n);
			}
			bitmap_status = false;
			for (i = 0; i < INT_SET_MAX / CHAR_BIT; ++i) {
				if (bitmap_1[i] & bitmap_2[i]) {
					bitmap_status = true;
					break;
				}
			}
			s = an_md_rdtsc();
			int_set_status = int_set_intersect(set_1, set_2);
			e = an_md_rdtsc();
			printf("iter %d, intersect: %" PRIu64 " ticks\n", iter, e-s);
			if (int_set_status != bitmap_status) {
				INT_SET_FOREACH(set_1, x) {
					printf("iter %d, set_1: %ld\n", iter, x);
				}

				INT_SET_FOREACH(set_2, y) {
					printf("iter %d, set_2: %ld\n", iter, y);
				}
				printf("iter %d: bitmap said they %s intersect, int_set_t said they %s intersect\n",
				    iter,
				    bitmap_status ? "do" : "don't",
				    int_set_status ? "do" : "don't");
				passed = false;
			}
			free(bitmap_1);
			free(bitmap_2);
			free_int_set(set_1);
			free_int_set(set_2);
			fail_if(int_set_status != bitmap_status);
			continue;
free_set_1:
			free_int_set(set_1);
free_bitmap_2:
			free(bitmap_2);
free_bitmap_1:
			free(bitmap_1);
			return;
		}
	}
	if (passed) {
		printf("Intersection test passed.\n");
	} else {
		printf("Intersection test failed.\n");
	}
}
END_TEST

START_TEST(test_init_deinit) {
	static BTREE_CONTEXT_DEFINE(context, "test_init_deinit");
	int_set_t *set = NULL;

	add_int_to_set_init(&set, context, 1, sizeof(int));
	fail_if((int_set_index(set, 0) != 1), "intset should be created");
	add_int_to_set_init(&set, context, 2, sizeof(int));
	fail_if((int_set_index(set, 1) != 2), "simply append to int set");

	remove_int_from_set_deinit(&set, 1);
	fail_if((int_set_index(set, 0) != 2), "simply remove from int set");

	remove_int_from_set_deinit(&set, 2);
	fail_if(set != NULL, "intset should be freed");

}
END_TEST

#define CHECK_IN_PLACE(INTER, X, Y) do {				\
		int_set_t *copy;					\
									\
		copy = copy_int_set(X);					\
		int_set_intersection_dst(copy, copy, Y);		\
		fail_if(int_set_count(copy) != int_set_count(INTER) && "foo"); \
		fail_if(INTER != NULL &&				\
		    memcmp(copy->base, INTER->base, copy->num * copy->size) != 0); \
									\
		int_set_intersection_dst(copy, X, Y);			\
		fail_if(int_set_count(copy) != int_set_count(INTER) && "bar"); \
		fail_if(INTER != NULL &&				\
		    memcmp(copy->base, INTER->base, copy->num * copy->size) != 0); \
		free_int_set(copy);					\
									\
		copy = copy_int_set(Y);					\
		int_set_intersection_dst(copy, X, copy);		\
		fail_if(int_set_count(copy) != int_set_count(INTER) && "baz"); \
		fail_if(INTER != NULL &&				\
		    memcmp(copy->base, INTER->base, copy->num * copy->size) != 0); \
		free_int_set(copy);					\
	} while (0)

START_TEST(test_intersection_behavior) {
	static BTREE_CONTEXT_DEFINE(context, "test_intersection_behavior");
	int_set_t *set1;
	int_set_t *set2;
	int_set_t *intersection;

	// teset success
	for ( int i= 0; i < 3; i++) {
		set1 = new_int_set(context,2 << i , 16);
		set2 = new_int_set(context,2 << i , 16);
		fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
		for ( int i = 0; i < 16; i++) {
			add_int_to_set(set1, (8-i) * 100);
			add_int_to_set(set2, (16-i) * 100);
		}
		intersection = int_set_intersection(set1, set2);
		fail_if((int_set_is_empty(intersection)), "Intset should intersect");
		CHECK_IN_PLACE(intersection, set1, set2);
		free_int_set(intersection);

		intersection = int_set_intersection(set2, set1);
		fail_if((int_set_is_empty(intersection)), "Intset should intersect");
		CHECK_IN_PLACE(intersection, set2, set1);
		free_int_set(intersection);

		free_int_set(set1);
		free_int_set(set2);
	}
	// test failure
	for ( int i= 0; i < 3; i++) {
		set1 = new_int_set(context,2 << i , 16);
		set2 = new_int_set(context,2 << i , 16);
		fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
		for ( int i = 0; i < 16; i++) {
			add_int_to_set(set1, (8-i) * 100);
			add_int_to_set(set2, (16-i) * 100 + 1);
		}
		intersection = int_set_intersection(set1, set2);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		CHECK_IN_PLACE(intersection, set1, set2);
		free_int_set(intersection);

		intersection = int_set_intersection(set2, set1);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		CHECK_IN_PLACE(intersection, set2, set1);
		free_int_set(intersection);

		free_int_set(set1);
		free_int_set(set2);
	}
	// test failure  for non overlapping
	for ( int i= 0; i < 3; i++) {
		set1 = new_int_set(context,2 << i , 16);
		set2 = new_int_set(context,2 << i , 16);
		fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
		for ( int i = 0; i < 16; i++) {
			add_int_to_set(set1, (8-i) * 100);
			add_int_to_set(set2, (16 * 100) + 1);
		}
		intersection = int_set_intersection(set1, set2);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		CHECK_IN_PLACE(intersection, set1, set2);
		free_int_set(intersection);

		intersection = int_set_intersection(set2, set1);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		CHECK_IN_PLACE(intersection, set2, set1);
		free_int_set(intersection);

		free_int_set(set1);
		free_int_set(set2);
	}
       // test  intersect on first
        for ( int i= 0; i < 3; i++) {
                set1 = new_int_set(context,2 << i , 16);
                set2 = new_int_set(context,2 << i , 16);
                fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
                for ( int i = 0; i < 15; i++) {
                        add_int_to_set(set1, i * 100);
                        add_int_to_set(set2, 16 * 10000 );
                }
	        add_int_to_set(set1, int_set_index(set2,0));

		intersection = int_set_intersection(set1, set2);
		fail_if((int_set_is_empty(intersection)), "Intset should intersect");
		CHECK_IN_PLACE(intersection, set1, set2);
		free_int_set(intersection);

		intersection = int_set_intersection(set2, set1);
		fail_if((int_set_is_empty(intersection)), "Intset should intersect");
		CHECK_IN_PLACE(intersection, set2, set1);
		free_int_set(intersection);

                free_int_set(set1);
                free_int_set(set2);
        }
	 // test  intersect on last
        for ( int i= 0; i < 3; i++) {
                set1 = new_int_set(context,2 << i , 16);
                set2 = new_int_set(context,2 << i , 16);
                fail_if(set1 == NULL || set2 == NULL, "Could not create intset" );
                for ( int i = 0; i < 15; i++) {
                        add_int_to_set(set1, i * 100);
                        add_int_to_set(set2, 16 * 10000 );
                }
                add_int_to_set(set2, int_set_index(set1,14));

		intersection = int_set_intersection(set1, set2);
		fail_if((int_set_is_empty(intersection)), "Intset should intersect");
		CHECK_IN_PLACE(intersection, set1, set2);
		free_int_set(intersection);

		intersection = int_set_intersection(set2, set1);
		fail_if((int_set_is_empty(intersection)), "Intset should intersect");
		CHECK_IN_PLACE(intersection, set2, set1);
		free_int_set(intersection);

                free_int_set(set1);
                free_int_set(set2);
        }
	// test	 bad parameters
	{
                set1 = new_int_set(context,sizeof(long) , 16);
                set2 = new_int_set(context,sizeof(long) , 16);

		intersection = int_set_intersection(set1, set2);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		CHECK_IN_PLACE(intersection, set1, set2);
		free_int_set(intersection);

		intersection = int_set_intersection(set1, NULL);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		free_int_set(intersection);

		intersection = int_set_intersection(NULL, set2);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		free_int_set(intersection);

		intersection = int_set_intersection(NULL, NULL);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		free_int_set(intersection);

		intersection = int_set_intersection(set1, set1);
		fail_if((int_set_count(intersection) != int_set_count(set1)),
			"self-intersection should be an identity");
		CHECK_IN_PLACE(intersection, set1, set1);
		free_int_set(intersection);

		add_int_to_set(set1, 0);
		add_int_to_set(set2, 1);

		intersection = int_set_intersection(set1, set2);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		CHECK_IN_PLACE(intersection, set1, set2);
		free_int_set(intersection);

		intersection = int_set_intersection(set1, NULL);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		free_int_set(intersection);

		intersection = int_set_intersection(NULL, set2);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		free_int_set(intersection);

		intersection = int_set_intersection(NULL, NULL);
		fail_if((int_set_count(intersection) > 0), "Intset should not intersect");
		free_int_set(intersection);

		intersection = int_set_intersection(set1, set1);
		fail_if((int_set_count(intersection) != int_set_count(set1)),
			"self-intersection should be an identity");
		CHECK_IN_PLACE(intersection, set1, set1);
		free_int_set(intersection);

                free_int_set(set1);
                free_int_set(set2);
        }

}
END_TEST

START_TEST(test_intersection) {
	static BTREE_CONTEXT_DEFINE(context, "intersect");
	uint64_t s, e;
	size_t iter, int_set_num;
	bool passed = true;

	printf("Actually testing intersection.\n");
	for (int_set_num = 16; int_set_num < INT_SET_MAX; int_set_num *= 2) {
		for (iter = 0; iter < INTERSECT_ITER; ++iter) {
			size_t i, nintersect;
			unsigned char *bitmap_1, *bitmap_2;
			int_set_t *set_1, *set_2, *intersection;
			bool current_passed = true;

			bitmap_1 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_1 == NULL) {
				fprintf(stderr, "bitmap_1 allocation failure in %s\n", __FUNCTION__);
				return;
			}
			bitmap_2 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_2 == NULL) {
				fprintf(stderr, "bitmap_2 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_1;
			}
			set_1 = new_int_set(context, sizeof(long), 16);
			if (set_1 == NULL) {
				fprintf(stderr, "set_1 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_2;
			}
			set_2 = new_int_set(context, sizeof(long), 16);
			if (set_2 == NULL) {
				fprintf(stderr, "set_2 allocation failure in %s\n", __FUNCTION__);
				goto free_set_1;
			}

			an_srand(7485539959361970041UL);
			for (i = 0; i < int_set_num; ++i) {
				long n;
				ldiv_t quotrem;

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_1[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_1, n);

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_2[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_2, n);
			}
			nintersect = 0;
			for (i = 0; i < INT_SET_MAX / CHAR_BIT; ++i) {
				nintersect += __builtin_popcount(bitmap_1[i] & bitmap_2[i]);
			}
			s = an_md_rdtsc();
			intersection = int_set_intersection(set_1, set_2);
			e = an_md_rdtsc();
			printf("iter %zu, intersection: %" PRIu64 " ticks\n", iter, e-s);

			if (nintersect != int_set_count(intersection)) {
				printf("iter %zu, bitmap has %zu elements, but int_set %zu\n",
				    iter, nintersect, int_set_count(intersection));
				current_passed = false;
			}

			for (i = 0; i < int_set_count(intersection); i++) {
				size_t x = int_set_index(intersection, i);
				size_t byte = x/CHAR_BIT, bit = x%CHAR_BIT;
				if (0 == (bitmap_1[byte] & bitmap_2[byte] & (1UL << bit))) {
					printf("iter %zu, int_set intersection contains %"PRIu64
					    ", but not bitmap\n",
					    iter, x);
					current_passed = false;
				}
			}

			CHECK_IN_PLACE(intersection, set_1, set_2);
			free(bitmap_1);
			free(bitmap_2);
			free_int_set(set_1);
			free_int_set(set_2);
			free_int_set(intersection);
			fail_if(current_passed == false);
			passed = passed && current_passed;
			continue;
free_set_1:
			free_int_set(set_1);
free_bitmap_2:
			free(bitmap_2);
free_bitmap_1:
			free(bitmap_1);
			return;
		}
	}
	if (passed) {
		printf("Intersection test passed.\n");
	} else {
		printf("Intersection test failed.\n");
	}
}
END_TEST

START_TEST(test_union) {
	static BTREE_CONTEXT_DEFINE(context, "union");
	int_set_t *one;
	int_set_t *two;

	for (int bytes = 2; bytes <= 8; bytes *= 2) {
		// A 16-element set
		one = new_int_set(context, bytes, 16);
		for (int i = 0; i < 16; i++) {
			if (i % 2 == 0) {
				add_int_to_set(one, i);
			}
		}
		// Make a bigger set
		two = new_int_set(context, bytes, 32);
		for (int i = 0; i < 32; i++) {
			if (i % 5 == 0) {
				add_int_to_set(two, i);
			}
		}

		// Check union with a null set
		int_set_t *union_set = int_set_union(one, NULL);
		for (int i = 0; i < 16; i++) {
			if (i % 2 == 0) {
				fail_if(!int_set_contains(union_set, i));
			} else {
				fail_if(int_set_contains(union_set, i));
			}
		}
		free_int_set(union_set);

		// Union a 16-element set with a 32-entry set
		union_set = int_set_union(one, two);
		for (int i = 0; i < 32; i++) {
			if ((i % 2 == 0 && i < 16) || i % 5 == 0) {
				fail_if(!int_set_contains(union_set, i));
			} else {
				fail_if(int_set_contains(union_set, i));
			}
		}

		// Ensure that things are properly sorted after the union and the right size
		fail_if(int_set_count(union_set) != 13);
		int last = -1;
		for (size_t i = 0; i < int_set_count(union_set); i++) {
			int current = int_set_index(union_set, i);
			fail_if(current <= last); // should be sorted and NOT have dups
			last = current;
		}
		free_int_set(union_set);
		free_int_set(one);
		free_int_set(two);
	}
}
END_TEST

START_TEST(test_union_perf) {
	static BTREE_CONTEXT_DEFINE(context, "union");
	uint64_t s, e;
	size_t iter, int_set_num;
	bool passed = true;

	printf("Actually testing union.\n");
	for (int_set_num = 16; int_set_num < INT_SET_MAX; int_set_num *= 2) {
		for (iter = 0; iter < INTERSECT_ITER; ++iter) {
			size_t i, nunion;
			unsigned char *bitmap_1, *bitmap_2;
			int_set_t *set_1, *set_2, *union_set;
			bool current_passed = true;

			bitmap_1 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_1 == NULL) {
				fprintf(stderr, "bitmap_1 allocation failure in %s\n", __FUNCTION__);
				return;
			}
			bitmap_2 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_2 == NULL) {
				fprintf(stderr, "bitmap_2 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_1;
			}
			set_1 = new_int_set(context, sizeof(long), 16);
			if (set_1 == NULL) {
				fprintf(stderr, "set_1 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_2;
			}
			set_2 = new_int_set(context, sizeof(long), 16);
			if (set_2 == NULL) {
				fprintf(stderr, "set_2 allocation failure in %s\n", __FUNCTION__);
				goto free_set_1;
			}

			an_srand(7485539959361970041UL);
			for (i = 0; i < int_set_num; ++i) {
				long n;
				ldiv_t quotrem;

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_1[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_1, n);

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_2[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_2, n);
			}
			nunion = 0;
			for (i = 0; i < INT_SET_MAX / CHAR_BIT; ++i) {
				nunion += __builtin_popcount(bitmap_1[i] | bitmap_2[i]);
			}
			s = an_md_rdtsc();
			union_set = int_set_union(set_1, set_2);
			e = an_md_rdtsc();
			printf("iter %zu, union: %" PRIu64 " ticks\n", iter, e-s);

			if (nunion != int_set_count(union_set)) {
				printf("iter %zu, bitmap has %zu elements, but int_set %zu\n",
				    iter, nunion, int_set_count(union_set));
				current_passed = false;
			}

			for (i = 0; i < int_set_count(union_set); i++) {
				size_t x = int_set_index(union_set, i);
				size_t byte = x/CHAR_BIT, bit = x%CHAR_BIT;
				if (0 == ((bitmap_1[byte] | bitmap_2[byte]) & (1UL << bit))) {
					printf("iter %zu, int_set union contains %"PRIu64
					    ", but not bitmap\n",
					    iter, x);
					current_passed = false;
				}
			}
			free(bitmap_1);
			free(bitmap_2);
			free_int_set(set_1);
			free_int_set(set_2);
			free_int_set(union_set);
			fail_if(current_passed == false);
			passed = passed && current_passed;
			continue;
free_set_1:
			free_int_set(set_1);
free_bitmap_2:
			free(bitmap_2);
free_bitmap_1:
			free(bitmap_1);
			return;
		}
	}
	if (passed) {
		printf("Union test passed.\n");
	} else {
		printf("Union test failed.\n");
	}
}
END_TEST

START_TEST(test_in_place_union_perf) {
	static BTREE_CONTEXT_DEFINE(context, "union");
	uint64_t s, e;
	size_t iter, int_set_num;
	bool passed = true;

	printf("Testing in-place union.\n");
	for (int_set_num = 16; int_set_num < INT_SET_MAX; int_set_num *= 2) {
		for (iter = 0; iter < INTERSECT_ITER; ++iter) {
			size_t i, nunion;
			unsigned char *bitmap_1, *bitmap_2;
			int_set_t *set_1, *set_2, *union_set;
			bool current_passed = true;

			bitmap_1 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_1 == NULL) {
				fprintf(stderr, "bitmap_1 allocation failure in %s\n", __FUNCTION__);
				return;
			}
			bitmap_2 = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
			if (bitmap_2 == NULL) {
				fprintf(stderr, "bitmap_2 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_1;
			}
			set_1 = new_int_set(context, sizeof(long), 16);
			if (set_1 == NULL) {
				fprintf(stderr, "set_1 allocation failure in %s\n", __FUNCTION__);
				goto free_bitmap_2;
			}
			set_2 = new_int_set(context, sizeof(long), 16);
			if (set_2 == NULL) {
				fprintf(stderr, "set_2 allocation failure in %s\n", __FUNCTION__);
				goto free_set_1;
			}

			an_srand(7485539959361970041UL);
			for (i = 0; i < int_set_num; ++i) {
				long n;
				ldiv_t quotrem;

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_1[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_1, n);

				n = an_random_below(INT_SET_MAX);
				quotrem = ldiv(n, CHAR_BIT);
				bitmap_2[quotrem.quot] |= 1 << quotrem.rem;
				add_int_to_set(set_2, n);
			}
			nunion = 0;
			for (i = 0; i < INT_SET_MAX / CHAR_BIT; ++i) {
				nunion += __builtin_popcount(bitmap_1[i] | bitmap_2[i]);
			}
			s = an_md_rdtsc();
			union_set = set_1;
			int_set_union_dst(union_set, set_1, set_2);
			e = an_md_rdtsc();
			printf("iter %zu, in-place union: %" PRIu64 " ticks\n", iter, e-s);

			if (nunion != int_set_count(union_set)) {
				printf("iter %zu, bitmap has %zu elements, but int_set %zu\n",
				    iter, nunion, int_set_count(union_set));
				current_passed = false;
			}

			for (i = 0; i < int_set_count(union_set); i++) {
				size_t x = int_set_index(union_set, i);
				size_t byte = x/CHAR_BIT, bit = x%CHAR_BIT;
				if (0 == ((bitmap_1[byte] | bitmap_2[byte]) & (1UL << bit))) {
					printf("iter %zu, in-place int_set union contains %"PRIu64
					    ", but not bitmap\n",
					    iter, x);
					current_passed = false;
				}
			}
			free(bitmap_1);
			free(bitmap_2);
			free_int_set(set_1);
			free_int_set(set_2);
			fail_if(current_passed == false);
			passed = passed && current_passed;
			continue;
free_set_1:
			free_int_set(set_1);
free_bitmap_2:
			free(bitmap_2);
free_bitmap_1:
			free(bitmap_1);
			return;
		}
	}
	if (passed) {
		printf("In-place union test passed.\n");
	} else {
		printf("In-place union test failed.\n");
	}
}
END_TEST

#define UNION_ALL_WAY 5

START_TEST(test_int_set_union_all) {
	static BTREE_CONTEXT_DEFINE(context, "union_all");
	uint64_t s, e;
	size_t iter, int_set_num;
	bool passed = true;

	printf("Testing n-way union.\n");
	for (int_set_num = 16; int_set_num < INT_SET_MAX; int_set_num *= 2) {
		for (iter = 0; iter < INTERSECT_ITER; ++iter) {
			size_t nunion;
			unsigned char *bitmap[UNION_ALL_WAY] = { NULL };
			int_set_t *set[UNION_ALL_WAY] = { NULL };
			int_set_t *union_set;
			bool current_passed = true;

			for (size_t i = 0; i < UNION_ALL_WAY; i++) {
				bitmap[i] = calloc(INT_SET_MAX / CHAR_BIT, sizeof(unsigned char));
				if (bitmap[i] == NULL) {
					fprintf(stderr, "bitmap allocation failure in %s\n",
					    __FUNCTION__);
					goto cleanup;
				}
			}

			for (size_t i = 0; i < UNION_ALL_WAY; i++) {
				set[i] = new_int_set(context, sizeof(long), 16);
				if (set[i] == NULL) {
					fprintf(stderr, "set allocation failure in %s\n",
					    __FUNCTION__);
					goto cleanup;
				}
			}

			an_srand(7485539959361970041UL);
			for (size_t i = 0; i < int_set_num; ++i) {
				long n;
				ldiv_t quotrem;

				for (size_t j = 0; j < UNION_ALL_WAY; j++) {
					n = an_random_below(INT_SET_MAX);
					quotrem = ldiv(n, CHAR_BIT);
					bitmap[j][quotrem.quot] |= 1 << quotrem.rem;
					add_int_to_set(set[j], n);
				}
			}
			nunion = 0;
			for (int i = 0; i < INT_SET_MAX / CHAR_BIT; ++i) {
				unsigned char sum = 0;

				for (size_t j = 0; j < UNION_ALL_WAY; j++) {
					sum |= bitmap[j][i];
				}

				nunion += __builtin_popcount(sum);
			}
			s = an_md_rdtsc();
			union_set = int_set_union_all((const int_set_t **)set, UNION_ALL_WAY);
			e = an_md_rdtsc();
			printf("iter %zu, n-way union: %" PRIu64 " ticks\n", iter, e-s);

			if (nunion != int_set_count(union_set)) {
				printf("iter %zu, bitmap has %zu elements, but int_set %zu\n",
				    iter, nunion, int_set_count(union_set));
				current_passed = false;
			}

			for (size_t i = 0; i < int_set_count(union_set); i++) {
				size_t x = int_set_index(union_set, i);
				size_t byte = x/CHAR_BIT, bit = x%CHAR_BIT;
				unsigned char sum = 0;

				for (size_t j = 0; j < UNION_ALL_WAY; j++) {
					sum |= bitmap[j][byte];
				}

				if (0 == (sum & (1UL << bit))) {
					printf("iter %zu, n-way int_set union contains %"PRIu64
					    ", but not bitmap\n",
					    iter, x);
					current_passed = false;
				}
			}

			for (size_t i = 0; i < UNION_ALL_WAY; i++) {
				free(bitmap[i]);
				free(set[i]);
			}
			fail_if(current_passed == false);
			passed = passed && current_passed;
			continue;

cleanup:
			for (size_t i = 0; i < UNION_ALL_WAY; i++) {
				free(bitmap[i]);
				free(set[i]);
			}
			return;
		}
	}

	if (passed) {
		printf("n-way union test passed.\n");
	} else {
		printf("n-way union test failed.\n");
	}
}
END_TEST

static void test_union_array_16() {
	int16_t bytes = sizeof(int16_t);
	int_set_t* one;

	one = new_int_set(NULL, bytes, 8);
	for (int i=0; i<16; i+=2) {
		add_int_to_set(one,i);
	}
	int16_t* array = malloc(sizeof(int16_t)*16);
	for(int i=0; i<16; ++i) {
		array[i] = i;
	}
	//standard NULL pass checks
	int_set_t *union_set = int_set_union_array(NULL, (int64_t*)NULL, 0, bytes);
	fail_if(union_set);
	union_set = int_set_union_array(one, (int64_t*)NULL, 20, bytes);
	fail_if(int_set_count(union_set) != int_set_count(one));
	free_int_set(union_set);
	union_set = int_set_union_array(one, (int64_t*)1, 0, bytes);
	fail_if(int_set_count(union_set) != int_set_count(one));
	free_int_set(union_set);
	union_set = int_set_union_array(NULL, (int64_t*)array, 16, bytes);
	for(int i=0; i<16; ++i) { //should be exact copy of array
		fail_if(array[i] != int_set_index(union_set, i));
	}
	free_int_set(union_set);
	//Test add only unique elements to set
	union_set = int_set_union_array(one, (int64_t* )array, 16, bytes);
	fail_if(int_set_count(union_set) != 16);
	int last = -1;
	for (size_t i = 0; i < int_set_count(union_set); i++) {
		int current = int_set_index(union_set, i);
		fail_if(current <= last);
		last = current;
	}
	free_int_set(union_set);
	//Duplicates in array, only add unique (to both array and set) to int_set
	int x = 0;
	for (int i=0; i<16; i++) {
		array[i++] = x;
		array[i] = x;
		x+=2;
	}
	union_set = int_set_union_array(one, (int64_t*)array, 16, bytes);
	fail_if(int_set_count(union_set) != 8);
	last=-1;
	for (size_t i = 0; i < int_set_count(union_set); i++) {
		int current = int_set_index(union_set, i);
		fail_if(current <= last);
		last = current;
	}
	free_int_set(union_set);
	free(array);
	int16_t* array2 = malloc(sizeof(int16_t)*16);
	last = 2;
	for (int i=0;i<16;i++) {
		if (i<3) {
			array2[i] = 16;
		} else if (i<6) {
			array2[i] = -2;
		} else if (i<11) {
			array2[i] = 18;
		}  else {
			array2[i] = last;
			last+=2;
		}
	}
	union_set = int_set_union_array(one, (int64_t*)array2, 16, bytes);
	//Check more entries beyond duplicates, both below/above min and max, and unique elements
	fail_if(int_set_count(union_set) != 11);
	int j=-2;
	last = -4;
	for (size_t i = 0; i < int_set_count(union_set) && j<=18; i++) {
		int current = int_set_index(union_set,i);
		fail_if(current <= last);
		last = current;
		fail_if(!int_set_contains(union_set, j));
		j+=2;
	}
	free(array2);
	free_int_set(union_set);
}

static void test_union_array_32() {
	int16_t bytes = sizeof(int32_t);
	int_set_t* one;

	one = new_int_set(NULL, bytes, 8);
	for (int i=0; i<16; i+=2) {
		add_int_to_set(one,i);
	}
	int32_t* array = malloc(sizeof(int32_t)*16);
	for(int i=0; i<16; ++i) {
		array[i] = i;
	}
	//standard NULL pass checks
	int_set_t* union_set = int_set_union_array(NULL, (int64_t*)NULL, 0, bytes);
	fail_if(union_set);
	union_set = int_set_union_array(one, (int64_t*)NULL, 20, bytes);
	fail_if(int_set_count(union_set) != int_set_count(one));
	free_int_set(union_set);
	union_set = int_set_union_array(one, (int64_t*)1, 0, bytes);
	fail_if(int_set_count(union_set) != int_set_count(one));
	free_int_set(union_set);
	union_set = int_set_union_array(NULL, (int64_t*)array, 16, bytes);
	for(int i=0; i<16; ++i) { //should be exact copy of array
		fail_if(array[i] != int_set_index(union_set, i));
	}
	free_int_set(union_set);
	//Test add only unique elements to set
	union_set = int_set_union_array(one, (int64_t* )array, 16, bytes);
	fail_if(int_set_count(union_set) != 16);
	int last = -1;
	for (size_t i = 0; i < int_set_count(union_set); i++) {
		int current = int_set_index(union_set, i);
		fail_if(current <= last);
		last = current;
	}
	free_int_set(union_set);
	//Duplicates in array, only add unique (to both array and set) to int_set
	int x = 0;
	for (int i=0; i<16; i++) {
		array[i++] = x;
		array[i] = x;
		x+=2;
	}
	union_set = int_set_union_array(one, (int64_t*)array, 16, bytes);
	fail_if(int_set_count(union_set) != 8);
	last=-1;
	for (size_t i = 0; i < int_set_count(union_set); i++) {
		int current = int_set_index(union_set, i);
		fail_if(current <= last);
		last = current;
	}
	free_int_set(union_set);
	free(array);
	int32_t* array2 = malloc(sizeof(int32_t)*16);
	last = 2;
	for (int i=0;i<16;i++) {
		if (i<3) {
			array2[i] = 16;
		} else if (i<6) {
			array2[i] = -2;
		} else if (i<11) {
			array2[i] = 18;
		}  else {
			array2[i] = last;
			last+=2;
		}
	}
	union_set = int_set_union_array(one, (int64_t*)array2, 16, bytes);
	//Check more entries beyond duplicates, both below/above min and max, and unique elements
	fail_if(int_set_count(union_set) != 11);
	int j=-2;
	last = -4;
	for (size_t i = 0; i < int_set_count(union_set) && j <= 18; i++) {
		int current = int_set_index(union_set, i);
		fail_if(current <= last);
		last = current;
		fail_if(!int_set_contains(union_set, j));
		j+=2;
	}
	free(array2);
	free_int_set(union_set);
}

static void test_union_array_64() {
	int16_t bytes = sizeof(int64_t);
	int_set_t* one;

	one = new_int_set(NULL, bytes, 8);
	for (int i=0; i<16; i+=2) {
		add_int_to_set(one,i);
	}
	int64_t* array = malloc(sizeof(int64_t)*16);
	for(int i=0; i<16; ++i) {
		array[i] = i;
	}
	//standard NULL pass checks
	int_set_t* union_set = int_set_union_array(NULL, (int64_t*)NULL, 0, bytes);
	fail_if(union_set);
	union_set = int_set_union_array(one, (int64_t*)NULL, 20, bytes);
	fail_if(int_set_count(union_set) != int_set_count(one));
	free_int_set(union_set);
	union_set = int_set_union_array(one, (int64_t*)1, 0, bytes);
	fail_if(int_set_count(union_set) != int_set_count(one));
	free_int_set(union_set);
	union_set = int_set_union_array(NULL, (int64_t*)array, 16, bytes);
	for(int i=0; i<16; ++i) { //should be exact copy of array
		fail_if(array[i] != int_set_index(union_set, i));
	}
	free_int_set(union_set);
	//Test add only unique elements to set
	union_set = int_set_union_array(one, (int64_t* )array, 16, bytes);
	fail_if(int_set_count(union_set) != 16);
	int last = -1;
	for (size_t i = 0; i < int_set_count(union_set); i++) {
		int current = int_set_index(union_set, i);
		fail_if(current <= last);
		last = current;
	}
	free_int_set(union_set);
	//Duplicates in array, only add unique (to both array and set) to int_set
	int x = 0;
	for (int i=0; i<16; i++) {
		array[i++] = x;
		array[i] = x;
		x+=2;
	}
	union_set = int_set_union_array(one, (int64_t*)array, 16, bytes);
	fail_if(int_set_count(union_set) != 8);
	last=-1;
	for (size_t i = 0; i < int_set_count(union_set); i++) {
		int current = int_set_index(union_set, i);
		fail_if(current <= last);
		last = current;
	}
	free_int_set(union_set);
	free(array);
	int64_t* array2 = malloc(sizeof(int64_t)*16);
	last = 2;
	for (int i=0;i<16;i++) {
		if (i<3) {
			array2[i] = 16;
		} else if (i<6) {
			array2[i] = -2;
		} else if (i<11) {
			array2[i] = 18;
		}  else {
			array2[i] = last;
			last+=2;
		}
	}
	union_set = int_set_union_array(one, (int64_t*)array2, 16, bytes);
	//Check more entries beyond duplicates, both below/above min and max, and unique elements
	fail_if(int_set_count(union_set) != 11);
	int j=-2;
	last = -4;
	for (size_t i = 0; i < int_set_count(union_set) && j <= 18; i++) {
		int current = int_set_index(union_set,i);
		fail_if(current <= last);
		last = current;
		fail_if(!int_set_contains(union_set, j));
		j+=2;
	}
	free(array2);
	free_int_set(union_set);
}

START_TEST(test_union_array) {
	test_union_array_16();
	test_union_array_32();
	test_union_array_64();
}
END_TEST

static void
dump_int_set(const pair_int_t *pi, void *arg, bool is_first)
{

	printf(" %d - %d\n", pi->a, pi->b);
}

START_TEST(test_pair) {
	static BTREE_CONTEXT_DEFINE(context, "pair");

	an_srand(time(NULL));
	printf("Test pair ints\n");
	pair_int_set_t* set = new_pair_int_set(context, 4);

	add_pair_int_to_set(set, 1, 5);
	add_pair_int_to_set(set, 2, 5);
	add_pair_int_to_set(set, 3, 5);
	add_pair_int_to_set(set, 3, 10);
	add_pair_int_to_set(set, 3, 15);
	add_pair_int_to_set(set, 4, 10);
	add_pair_int_to_set(set, 5, 15);
	btree_foreach(set, dump_int_set, const pair_int_t, NULL);
	pair_int_t* pi = pair_int_set_lookup(set, 3, 10);
	fail_if(pi == NULL);
	remove_pair_int_from_set(set, 3, 10);
	pi = pair_int_set_lookup(set, 3, 10);
	fail_if(pi != NULL);
}
END_TEST

#define MAX_INT_SET_MEMBERS 10

struct int_set_contains_64_test_t {
	int64_t set_members[MAX_INT_SET_MEMBERS];
	size_t num_members;
	int64_t search;
	bool match;
};

struct int_set_contains_64_test_t test_int_set_contains_64_data[] = {
		{ { 6106277, 2351057252, 0, 0, 0, 0, 0, 0, 0, 0 }, 2, 6106277, true },
		{ { 6106277, 2351057252, 0, 0, 0, 0, 0, 0, 0, 0 }, 2, 2351057252, true },
		{ { 6106277, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1, 6106277, true },
		{ { 6106277, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1, 2351057252, false },
		{ { 2351057252, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1, 6106277, false },
		{ { 2351057252, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 1, 2351057252, true },
		{ { 2351057252, 6106277, 0, 0, 0, 0, 0, 0, 0, 0 }, 2, 6106277, true },
		{ { 2351057252, 6106277, 0, 0, 0, 0, 0, 0, 0, 0 }, 2, 2351057252, true },
};

START_TEST(test_int_set_contains_64) {
	static BTREE_CONTEXT_DEFINE(context, "test_int_set_contains");
	size_t j;
	int_set_t* x;

	int16_t bytes = sizeof(int64_t);
	x = new_int_set(NULL, bytes, 8);

	for (j = 0; j < test_int_set_contains_64_data[_i].num_members; j++) {
		fail_if(j > test_int_set_contains_64_data[_i].num_members);
		add_int_to_set(x, test_int_set_contains_64_data[_i].set_members[j]);
	}

	bool found = int_set_contains(x, test_int_set_contains_64_data[_i].search);
	if (found != test_int_set_contains_64_data[_i].match) {
		fail_if(test_int_set_contains_64_data[_i].match == false, "Failed Int Set Contains 64: [%zu] %ld found but not present int set.",
				_i, test_int_set_contains_64_data[_i].search);
		fail_if(test_int_set_contains_64_data[_i].match == true, "Failed Int Set Contains 64: [%zu] %ld not found but present int set.",
				_i, test_int_set_contains_64_data[_i].search);
	}
}
END_TEST

#define INT_SET_EQUALS(set, arr)				     \
	do {							     \
		fail_if(int_set_count(set) != ARRAY_SIZE(arr));	     \
		for (size_t i = 0; i < int_set_count(x); i++) {	     \
			fail_if(arr[i] != int_set_index_32(set, i)); \
		}						     \
	} while (0)

START_TEST(test_remove_range) {
	static BTREE_CONTEXT_DEFINE(context, "test_int_set_remove_range");
	int_set_t *x;

	x = new_int_set(NULL, sizeof(int), 64);

	for (size_t i = 0; i < 10; i++) {
		add_int_to_set(x, i);  /* x should contain 0, 1, 2, ... , 9 */
	}

	/* Remove [3, 7) */
	btree_delete_index_range(x, 3, 7);

	/* set should now contain 0, 1, 2, 7, 8, 9 */
	const int32_t elems[] = {0, 1, 2, 7, 8, 9};
	INT_SET_EQUALS(x, elems);

	/* Remove first 3 elements [0, 3) */
	btree_delete_index_range(x, 0, 3);

	/* set should now contain 7, 8, 9 */
	const int32_t elems2[] = {7, 8, 9};
	INT_SET_EQUALS(x, elems2);

	/* Remove last 2 elements */
	btree_delete_index_range(x, 1, 3);

	/* set should now contain 7 */
	const int32_t elems3[] = {7};
	INT_SET_EQUALS(x, elems3);

	/* Remove last element */
	btree_delete_index_range(x, 0, 1);
	fail_if(int_set_count(x) != 0);
}
END_TEST

int main(int argc, char **argv) {
	Suite *suite = suite_create("common/check_int_set");

	an_md_probe();
	an_malloc_init();
	common_type_register();

	TCase *tc = tcase_create("test_int_set");
	tcase_set_timeout(tc, 0);
	tcase_add_test(tc, test_addremove);
	tcase_add_test(tc, test_uniq);
	tcase_add_test(tc, test_qsort);
#ifdef TEST_PERFORMANCE
	tcase_add_test(tc, test_perf);
#endif
	tcase_add_test(tc, test_append);
	tcase_add_test(tc, test_append_bulk_dups);
	tcase_add_test(tc, test_intersect_behavior);
	tcase_add_test(tc, test_intersect);
	tcase_add_test(tc, test_intersection_behavior);
	tcase_add_test(tc, test_intersection);
	tcase_add_test(tc, test_union);
	tcase_add_test(tc, test_union_perf);
	tcase_add_test(tc, test_in_place_union_perf);
	tcase_add_test(tc, test_int_set_union_all);
	tcase_add_test(tc, test_union_array);
	tcase_add_test(tc, test_pair);
	tcase_add_test(tc, test_remove_range);
	tcase_add_test(tc, test_init_deinit);
	tcase_add_loop_test(tc, test_int_set_contains_64, 0, ARRAY_SIZE(test_int_set_contains_64_data));
	suite_add_tcase(suite, tc);

	SRunner *sr = srunner_create(suite);
	srunner_set_xml(sr, "check/check_int_set.xml");
	srunner_set_fork_status(sr, CK_FORK);
	srunner_run_all(sr, CK_NORMAL);
	int num_failed = srunner_ntests_failed(sr);
	srunner_free(sr);
	return num_failed;
}
