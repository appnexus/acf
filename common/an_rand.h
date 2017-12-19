#ifndef AN_RAND_H
#define AN_RAND_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/an_cc.h"

/**
 * A linear (in GF(2)) Marsaglia xor-shift PRNG with non-linear (in
 * GF(2)) mixing.  It's stronger and as fast as/faster than libc
 * PRNGs.  Use this function directly if you absolutely need an
 * independent random stream.
 *
 * See <http://xorshift.di.unimi.it/xorshift128plus.c> (Public Domain)
 * and
 *
 * Sebastiano Vigna. Further scramblings of Marsaglia's xorshift
 * generators. CoRR, abs/1404.0390, 2014
 *
 * Sebastiano Vigna. An experimental exploration of Marsaglia's
 * xorshift generators, scrambled. CoRR, abs/1402.6246, 2014.
 */
static inline uint64_t
an_xorshift_plus(uint64_t state[static 2])
{
	uint64_t s1 = state[0];
	const uint64_t s0 = state[1];

	state[0] = s0;
	s1 ^= s1 << 23;
	s1 = (s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26));
	state[1] = s1;
	return s1 + s0;
}

/**
 * @brief Initialise a xorshift_plus state.
 */
void an_xorshift_plus_seed(uint64_t state[static 2], uint64_t seed);

/**
 * @brief Initialise the default an_rand state.
 */
void an_srand(uint64_t);

/**
 * @return random 64 bit number.
 */
uint64_t an_rand64(void);

/* @return random value in [0, 2^31). */
static inline int
an_rand()
{

	return an_rand64() % (1ULL << 31);
}

/* @return random value in [0, 2^32). */
static inline uint32_t
an_rand32()
{

	return an_rand64() % (1ULL << 32);
}

/**
 * @return random double in [0, 1).
 *
 * Looks like a lot of code, but is actually 8 (should be 7)
 * instructions on gcc4.4.
 */
static inline double
an_drandom()
{
	double d;
	uint64_t exponent_bits;
	uint64_t rnd;

	rnd = an_rand64();
	/*
	 * use the top 52 bits to generate a random double in
	 * [1.0, 2.0).
	 *
	 * This memcpy is constant folded.
	 */
	d = 1.0;
	memcpy(&exponent_bits, &d, sizeof(uint64_t));
	/*
	 * IEEE-754 doubles have 52 bits of significand, and
	 * recommends a sign-magnitude bitwise representation.
	 *
	 * Check this code sequence when porting to a new
	 * architecture, but this should be de facto portable, if not
	 * de jure.
	 */
	exponent_bits &= -1ULL << 52;
	exponent_bits |= rnd >> 12;

	/*
	 * memcpy is magic and not subject to strict aliasing.
	 * Really.  string.h can't be implemented without compiler
	 * support.
	 */
	memcpy(&d, &exponent_bits, sizeof(uint64_t));

	/* Bring that down to [0.0, 1.0) */
	return d - 1;
}

/**
 * @return returns true with p = probability
 */
static inline bool
an_random_indicator(double probability)
{

	return an_drandom() < probability;
}

/**
 * @brief get a random integer within a range
 *
 * @param min minimum value allowed in range
 * @param max maximum value allowed in range
 * @return random value
 */
static inline int32_t
an_random_within_range(int32_t min, int32_t max)
{
	uint64_t range = max - min + 1;
	uint64_t r;

	/* Multiply by [an_rand32() / 2^32] in fixed point and truncate. */
	r = an_rand32();
	r = (r * range) >> 32;

	return min + r;
}

/**
 * @return a random integer in [0, limit).
 */
static inline uint32_t
an_random_below(uint32_t limit)
{
	uint64_t r;

	r = an_rand32();
	r *= (uint64_t)limit;
	return r >> 32;
}

/**
 * @return a random 64 bits integer in [0, limit).
 */
static inline uint64_t
an_random64_below(uint64_t limit)
{
	unsigned __int128 r;

	r = an_rand64();
	r *= (unsigned __int128)limit;
	return r >> 64;
}

/**
 * @brief get a random double float value within a range
 *
 * @param min minimum value allowed in range
 * @param max maximum value allowed in range
 * @return random value
 */
static inline double
an_drandom_within_range(double min, double max)
{

	return an_drandom() * (max - min) + min;
}

/**
 * @brief Shuffle an array of nmemb size-byte values.
 *
 * @param array the array to shuffle
 * @param nmemb the number of values
 * @param size the size of each value
 */
AN_CC_UNUSED static void
an_random_shuffle_impl(void *array, size_t nmemb, size_t size)
{
	char buf[128];
	char *elems = array;
	void *alloc = NULL;
	void *temp = NULL;

	if (nmemb <= 1) {
		return;
	}

	if (size <= sizeof(buf)) {
		temp = buf;
	} else {
		alloc = malloc(size);
		temp = alloc;
	}

	for (size_t i = nmemb; i > 1; ) {
		size_t j;

		j = an_random_below(i--);
		if (j != i) {
			memcpy(temp, elems + size * i, size);
			memcpy(elems + size * i, elems + size * j, size);
			memcpy(elems + size * j, temp, size);
		}
	}

	free(alloc);
	return;
}

#define an_random_shuffle(ARRAY, N) (an_random_shuffle_impl((ARRAY), (N), sizeof(*(ARRAY))))
#endif /* AN_RAND_H */
