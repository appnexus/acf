#ifdef NDEBUG
#undef NDEBUG
#endif

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>

#include "an_itoa.h"

static void
test_itoa(uint32_t x)
{
	char buf[23];
	unsigned long long test;
	char *end;

	memset(buf, 'X', 23);
	end = an_itoa(buf + 1, x);
	assert(end - (buf + 1) <= 10);
	assert(buf[0] == 'X');
	assert(buf[1] != 'X');
	assert(buf[1] != ' ');
	assert(*(end - 1) != 'X');
	assert('0' <= *(end - 1));
	assert(*(end - 1) <= '9');
	while (*end == '\0') end++;
	assert(*end == 'X');
	assert(end - (buf + 1) <= 10);
	*end = '\0';

	assert(sscanf(buf + 1, " %llu", &test) == 1);
	assert(test == x);
}

static void
test_ltoa(uint64_t x)
{
	char buf[23];
	unsigned long long test;
	char *end;

	memset(buf, 'X', 23);
	end = an_ltoa(buf + 1, x);
	assert(end - (buf + 1) <= 20);
	assert(buf[0] == 'X');
	assert(buf[1] != 'X');
	assert(buf[1] != ' ');
	assert(*(end - 1) != 'X');
	assert('0' <= *(end - 1));
	assert(*(end - 1) <= '9');
	while (*end == '\0') end++;
	assert(end - (buf + 1) <= 20);
	assert(*end == 'X');
	*end = '\0';

	assert(sscanf(buf + 1, " %llu", &test) == 1);
	assert(test == x);
}

int
main(int argc, char **argv)
{
	uint64_t hi;
	ssize_t range = 128;

	/* Fewer test by default. */
	if (argc > 1) {
		range = 1024 * 1024;
	}

#define TEST(I) do {				\
		uint64_t test_value = (I);	\
						\
		test_itoa(test_value);		\
		test_ltoa(test_value);		\
	} while (0)

	{
		char buf[23];
		char *end;

		end = an_itoa(buf, 0);
		assert(end == &buf[1]);
		assert(buf[0] == '0');

		end = an_ltoa(buf, 0);
		assert(end == &buf[1]);
		assert(buf[0] == '0');
	}

	/* Test around powers of 10. */
	hi = 1;
	for (size_t i = 0; i <= 20; i++, hi *= 10) {
		printf("Testing around power of 10: %"PRIu64"\n", hi);
		for (ssize_t j = -range; j <= range; j++) {
			TEST(hi + j);
			TEST(-(hi + j));
		}
	}

	/* Test around powers of 2. */
	hi = 1;
	for (size_t i = 0; i <= 64; i++, hi *= 2) {
		printf("Testing around power of 2: %"PRIu64"\n", hi);
		for (long j = -range; j <= range; j++) {
			TEST(hi + j);
			TEST(-(hi + j));
		}
	}

	/* No exhaustive test by default. */
	if (argc > 1) {
		size_t limit = UINT32_MAX;

		for (size_t i = 0; i <= limit; i++) {
			if (i % (8 * 1024 * 1024) == 0) {
				printf("Exhaustive testing: %05.2f%%\n",
				    100.0 * i / limit);
			}

			TEST(i);
			test_ltoa(-i);
		}
	}

#undef TEST

	return 0;
}
