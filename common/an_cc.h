#ifndef COMMON_AN_CC_H
#define COMMON_AN_CC_H

#include <acf/an_cc.h>
#include <ck_pr.h>
#include <stdint.h>

/*
 * This makes it look like we're passing three arguments to
 * ck_pr_store_ptr, but the first "two" (the second line of the
 * #defined val) is actually a single argument, which is a comma
 * expression (similar to "for (i = 0, j = 0;..."). Both clauses of
 * the comma expression will be "evaluated", but the trick here is
 * that the first clause will be compiled to a constant and then
 * optimized away.
 *
 * *(DST) = (VAL) does exactly what it says on the tin: it assigns VAL
 * to *DST. The compiler knows what DST's type is, and what it points
 * to (if it is indeed a pointer). This means we get type checking on
 * the assignment, but we don't want the assignment to actually be
 * _done_ at runtime, so we add a sizeof, which will compile to a
 * constant (an assignment is also a value, with a size), and the
 * (void) cast declares that we don't want the result of the
 * expression, because otherwise the left-hand clause of the comma
 * expression is declared as having no effect (which it
 * doesn't--that's the beauty of it).
 */
#define an_pr_store_ptr(DST, VAL)					\
	ck_pr_store_ptr(						\
	    ((void)(sizeof(*(DST) = (VAL))), (DST)),			\
	    (VAL))

/*
 * Cast the result of the dereference to the type of SRC--the type of
 * which we expect to get back.
 * Not strictly C99 but all reasonable compilers should really support this.
 */
#define an_pr_load_ptr(SRC) ((__typeof__(*(SRC)))ck_pr_load_ptr((SRC)))

_Static_assert(sizeof(float) == sizeof(uint32_t),
    "sizeof float not equal to sizeof uint32_t");

static inline float
an_pr_load_float(const float *value)
{
	union {
		float dst;
		uint32_t dummy;
	} temp;

	/* coverity[incompatible_cast : FALSE] */
	temp.dummy = ck_pr_load_32((const uint32_t *)value);

	return temp.dst;
}

static inline void
an_pr_store_float(float *target, float value)
{
	union {
		float dummy;
		uint32_t dst;
	} temp;

	temp.dummy = value;
	ck_pr_store_32((uint32_t *)target, temp.dst);

	return;
}

#endif /* COMMON_AN_CC_H */
