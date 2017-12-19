#include "common/an_rand.h"
#include "common/an_thread.h"

/* Two arbitrary non-zero values. */
static uint64_t xorshift_state[2] = {
	0x123456789ABCDEFULL,
	/* ASCII-encoded "appnexus" */
	0x737578656E707061ULL
};

void
an_xorshift_plus_seed(uint64_t state[static 2], uint64_t seed)
{

	/* MurmurHash 3 finalizer (as suggested by Vigna) */
	for (size_t i = 0; i < 2; i++) {
		seed ^= (seed >> 33);
		seed *= 0xFF51AFD7ED558CCDULL;
		seed ^= (seed >> 33);
		seed *= 0xC4CEB9FE1A85EC53ULL;
		seed ^= (seed >> 33);

		if (seed == 0) {
			/* Avalanching can't get us out of 0.  Arbitrary value. */
			seed = 0x123456789ABCDEFULL;
		}

		state[i] = seed;
	}

	return;
}

void
an_srand(uint64_t seed)
{

	an_xorshift_plus_seed(xorshift_state, seed);
}

uint64_t
an_rand64(void)
{

	return an_xorshift_plus(AN_CC_UNLIKELY(current == NULL) ?
	    xorshift_state :
	    current->xorshift_state);
}
