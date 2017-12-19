#ifndef AN_AVERAGE_H
#define AN_AVERAGE_H

#include <ck_pr.h>
#include <stdint.h>

#include "an_cc.h"
#include "util.h"

#define AN_AVERAGE_DENOM_BITS 8ULL

/**
 * Geometrically decaying average, in fixed precision rationals.
 *
 * values is AN_AVERAGE_DENOM_BITS for the denominator, and the rest
 * of an uint64_t for the numerator.
 *
 * The counter may lose updates, but the numerator and denominator will
 * always be consistent.
 */
struct an_average {
	uint64_t values;
};

/**
 * @brief read the current numerator and denominator for the running average.
 * @param average the an_average to read
 * @param num overwritten with the numerator
 * @param denom overwritten with the denominator
 */
static AN_CC_UNUSED void
an_average_read(const struct an_average *average, uint64_t *num, uint64_t *denom)
{
	uint64_t data, mask;

	data = ck_pr_load_64(&average->values);
	mask = (1ULL << AN_AVERAGE_DENOM_BITS) - 1;

	*num = data >> AN_AVERAGE_DENOM_BITS;
	*denom = data & mask;
	return;
}

/**
 * @brief insert a new observation in the running average
 * @param average the an_average to update
 * @param value the new observation
 */
static AN_CC_UNUSED void
an_average_insert(struct an_average *average, uint64_t value)
{
	uint64_t denom, limit, num, output;
	uint64_t mask = (1ULL << AN_AVERAGE_DENOM_BITS) - 1;

	an_average_read(average, &num, &denom);

	if (AN_CC_UNLIKELY(denom >= mask)) {
		/*
		 * Round to closest after multiplication by 3/4.
		 *
		 * 3/4 is arbitrary, and can be parameterised if
		 * necessary.  The rationale is that 1/2 is very
		 * uneven, and ratios closer to 1 increase the risk of
		 * consecutive decays within the same internal
		 * auction, which makes an_average_increment less
		 * accurate.
		 */
		num = (3 * num + 2) / 4;
		denom = (3 * denom + 2) / 4;
	}

	limit = (UINT64_MAX >> AN_AVERAGE_DENOM_BITS) - num;
	if (AN_CC_UNLIKELY(value > limit)) {
		output = UINT64_MAX / 2;
		output &= ~mask;
		output |= (denom / 2) + 1;
	} else {
		num += value;
		denom++;
		output = (num << AN_AVERAGE_DENOM_BITS) | denom;
	}

	ck_pr_store_64(&average->values, output);
	return;
}

/**
 * @brief increment a pre-existing observation in the running average
 * @param average the an_average to update
 * @param value the increment to add to a prior observation (denom is unaffected)
 */
static AN_CC_UNUSED void
an_average_increment(struct an_average *average, uint64_t value)
{
	uint64_t denom, limit, num, output;
	uint64_t mask = (1ULL << AN_AVERAGE_DENOM_BITS) - 1;

	an_average_read(average, &num, &denom);
	limit = (UINT64_MAX >> AN_AVERAGE_DENOM_BITS) - num;
	if (AN_CC_UNLIKELY(value > limit)) {
		output = UINT64_MAX / 2;
		output &= ~mask;
		output |= ((denom + 1) / 2);
	} else {
		num += value;
		output = (num << AN_AVERAGE_DENOM_BITS) | denom;
	}

	ck_pr_store_64(&average->values, output);
	return;
}

/**
 * @brief initialize the an_average struct
 * @param average the an_average struct
 */
static AN_CC_UNUSED void
an_average_init(struct an_average *average)
{

	memset(average, 0, sizeof(struct an_average));
}

#endif /* !defined(AN_AVERAGE_H) */
