#ifndef LOG_LINEAR_BIN_H
#define LOG_LINEAR_BIN_H
/**
 * jemalloc/HdrHistogram-style binning. See
 * http://pvk.ca/Blog/2015/06/27/linear-log-bucketing-fast-versatile-simple/.
 *
 * This is a generalisation of jemalloc's binning strategy:
 *
 * The range [0, 2^linear_lb] is subdivided evenly in 2^subrange_lb subranges.
 * Then, every range [2^k + 1, 2^(k + 1)] is subdivided evenly in 2^subrange_lb subranges.
 *
 * IMPORTANT NOTE: This means subrange_lb <= linear_lb.
 *
 * The implementation is a bit gnarly; the goal is to make the code
 * fast and flexible enough that it can be used for almost any
 * purpose.  Writing bespoke binning code is annoying/error-prone.
 *
 * The hybrid linear + subdivided geometric strategy means we don't
 * waste too much encoding space on small values, and guarantees that
 * subranges' widths are only a fixed fraction of the values in that
 * subrange.  For example, with subrange_lb = 4, we can map back from
 * bin to size with at most 1/2^4 = 6.25% error.
 *
 * log_linear_bin_of maps values to bins by rounding up to the next
 * bin boundary.  rounded_size is the max value for the bin, and size
 * <= rounded_size.  bin_size tells us the range of values that map to
 * the bin: (rounded_size - bin_size, bin_size].  0 is special and
 * gets its own bin.  Rounding up is useful for things like resource
 * allocations, where it's ok to hand out more resources than
 * necessary, but not less.
 *
 * log_linear_bin_down_of maps values to bins by rounding down to a
 * bin boundary.  The bin corresponds to values in [rounded_size,
 * rounded_size + bin_size).  Rounding down is overall simpler, and
 * useful for things like histogramming.
 *
 * Instead of rounding up, one could also round down and add the bin
 * size.  That's not as effective: it's more computation, and wastes
 * space on "nice" values like powers of two.  OTOH, finding the bin
 * size when rounding up is more complex than when rounding down. For
 * some fast path operations that require the bin size, rounding down
 * to a bin value and adding the bin size might be appropriate.
 */

#include <stddef.h>
#include <stdint.h>

#include "common/util.h"

/**
 * @brief maps @a size to a bin by rounding up, given the binning
 * strategy where the first 2**@a linear_lb integers are subdivided
 * evenly in 2**@a subrange_lb bins, and subsequence power-of-two
 * aligned ranges subdivided in 2**@a subrange_lb bins.
 *
 * @param size the value to bin
 * @param rounded_size if non-NULL, overwritten with the
 * representative value of the bin: that's the largest value that maps
 * to the bin.
 * @param bin_size if non-NULL, overwritten with the number of values
 * in the bin.
 * @param linear_lb log_2 of the size of the initial range that's
 * subdivided linearly
 * @param subrange_lb log_2 of the number of bins in each range
 * (either the linear range, or subsequent (2^i, 2^i - 1]).
 */
static inline size_t
log_linear_bin_of(size_t size, size_t *rounded_size, size_t *bin_size,
    unsigned int linear_lb, unsigned int subrange_lb)
{
	size_t mask, range, rounded, sub_index;
	unsigned int n_bits, shift;

	n_bits = log2_floor(size | (1ULL << linear_lb));
	shift = n_bits - subrange_lb;
	mask = (1ULL << shift) - 1;
	rounded = size + mask; /* XXX: overflow. */
	sub_index = rounded >> shift;
	range = n_bits - linear_lb;

	if (rounded_size != NULL) {
		*rounded_size = rounded & ~mask;
	}

	if (bin_size != NULL) {
		size_t prev = (size > (1ULL << linear_lb)) ? (size - 1) : -1ULL;
		size_t nonzero_mask = (size == 0) ? 0 : -1ULL;

		/*
		 * We have an off by one for powers of two greater
		 * than (1ULL << linear_lb), and the bin size is 1 for
		 * size == 0.
		 */
		*bin_size = 1ULL << ((shift - ((size & prev) == 0)) & nonzero_mask);
	}

	return (range << subrange_lb) + sub_index;
}

/**
 * @brief maps @a size to a bin by rounding down, given the binning
 * strategy where the first 2**@a linear_lb integers are subdivided
 * evenly in 2**@a subrange_lb bins, and subsequence power-of-two
 * aligned ranges subdivided in 2**@a subrange_lb bins.
 *
 * @param size the value to bin
 * @param rounded_size if non-NULL, overwritten with the
 * representative value of the bin: that's the smallest value that
 * maps to the bin.
 * @param bin_size if non-NULL, overwritten with the number of values
 * in the bin.
 * @param linear_lb log_2 of the size of the initial range that's
 * subdivided linearly
 * @param subrange_lb log_2 of the number of bins in each range
 * (either the linear range, or subsequent [2^i, 2^i - 1)).
 */
static inline size_t
log_linear_bin_down_of(size_t size, size_t *rounded_size, size_t *bin_size,
    unsigned int linear_lb, unsigned int subrange_lb)
{
	size_t range, sub_index;
	unsigned int n_bits, shift;

	n_bits = log2_floor(size | (1ULL << linear_lb));
	shift = n_bits - subrange_lb;
	sub_index = size >> shift;
	range = n_bits - linear_lb;

	if (rounded_size != NULL) {
		*rounded_size = sub_index << shift;
	}

	if (bin_size != NULL) {
		*bin_size = 1ULL << shift;
	}

	return (range << subrange_lb) + sub_index;
}
#endif /* !LOG_LINEAR_BIN_H */
