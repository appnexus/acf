/*
 * Centralised location to poison identifiers away.  Included in util.h.
 */
#ifndef AN_POISON_H
#define AN_POISON_H

#if (defined(IS_BIDDER) || defined(IS_IMPBUS)) && !defined(IS_REPACKD)
#pragma GCC poison itoa ftoa ftop litoa strerror
/* Bad and thread-unsafe PRNG. */
#pragma GCC poison rand random drand48 lrand48 mrand48
#pragma GCC poison erand48 nrand48 jrand48
/* Bad PRNG. */
#pragma GCC poison rand_r drand48_r lrand48_r mrand48_r
#pragma GCC poison erand48_r nrand48_r jrand48_r
/* Seed for said bad PRNG */
#pragma GCC poison srand srandom srand48 seed48 lcong48
#pragma GCC poison srand48_r seed48_r lcong48_r

#endif

#endif /* AN_POISON_H */
