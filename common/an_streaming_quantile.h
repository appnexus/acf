#ifndef AN_STREAMING_QUANTILE_H
#define AN_STREAMING_QUANTILE_H
/**
 * Implements "Frugal Streaming for Estimating Quantiles" --Ma, et al
 * Useful for getting quantile estimates on a streaming input source.
 * To use, either initialize or create an an_streaming_qnt, and sample new values with
 * an_streaming_qnt_update_mpmc or an_streaming_qnt_update_spmc, depending on use case.
 * To obtain an estimate of the specified quantile, use an_streaming_qnt_observe.
 *
 * The reasoning behind this frugal implementation is that when estimating any x quantile,
 * if the current estimate is at the stream's true x quantile, we expect to see items larger
 * than the current estimate with probability 1 - x.
 */

#include <stdint.h>

struct an_streaming_qnt {
	double quantile;	/** quantile to keep track of. value should be between 0 and 1 */
	uint64_t estimate;	/** estimate for quantile */
	double adjustment_value; /** value by which current quantile estimate will be adjusted (+/-) at a time */
};

/**
 * @brief static initializer for an_streaming_qnt struct.
 * Example usage: struct an_streaming_qnt qnt = AN_STREAMING_QUANTILE_INITIALIZER(0.99, 0, 1);
 * @param QUANTILE quantile to keep track of
 * @param INITIAL_VALUE initial estimate
 * @param ADJUSTMENT_VALUE value by which current quantile estimate will be adjusted (+/-) at a time
 */
#define AN_STREAMING_QUANTILE_INITIALIZER(QUANTILE, INITIAL_VALUE, ADJUSTMENT_VALUE)	\
    { .quantile = (QUANTILE), .estimate = (INITIAL_VALUE), .adjustment_value = (ADJUSTMENT_VALUE) }

/**
 * @brief Create an_streaming_qnt object to monitor quantile estimates
 * @param quant quantile to monitor (e.g. 0.75 for 75th quantile)
 * @param initial_value initial estimate
 * @param adjustment_value value by which current quantile estimate will be adjusted (+/-) at a time
 * @return pointer to created an_streaming_qnt object
 */
struct an_streaming_qnt *an_streaming_qnt_estimate_create(double quantile, uint64_t initial_value, double adjustment_value);

/**
 * @brief free @streaming_qnt
 */
void an_streaming_qnt_destroy(struct an_streaming_qnt *streaming_qnt);

/**
 * @brief insert new value for an_streaming_qnt obj. _mpmc indicates atomic updates
 * @param streaming_qnt object to sample for
 * @param sample new value
 */
void an_streaming_qnt_update_mpmc(struct an_streaming_qnt *streaming_qnt, uint64_t sample);

/**
 * @brief insert new value for an_streaming_qnt obj. _spmc indicates non-atomic updates
 * @param streaming_qnt object to sample for
 * @param sample new value
 */
void an_streaming_qnt_update_spmc(struct an_streaming_qnt *streaming_qnt, uint64_t sample);

/**
 * @brief get quantile estimate for @streaming_qnt
 */
uint64_t an_streaming_qnt_observe(const struct an_streaming_qnt *streaming_qnt);

#endif /* AN_STREAMING_QUANTILE_H */
