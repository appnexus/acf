#ifndef AN_SAMPLING_H
#define AN_SAMPLING_H

#include <stdbool.h>
#include <stddef.h>

/**
 * an_sampling_fixed_window: fixed-window algoritm: samples n elements from population of size N so that
 * each of the population elements have the equal chance to be selected.
 * Gives instant decision (selected/rejected) for the next population element.
 *
 * Usage:
 * 1) Call an_sampling_fixed_window_init to initialize instance object once
 * 2) Call an_sampling_fixed_window_next_is_selected() every time the item from the population arrives.
 *		The return value would indicate if item has been sampled.
 *		After the population (N) has all been enumerated, the next call to an_sampling_fixed_window_next_is_selected
 *		will reset the state to account for the new population/new sample is started.
 * 3) Cleanup instance with an_sampling_fixed_window_deinit call
 *
 * Implementation detail: implements Selection-Rejection algorithm, Fan et al (1962)
 * N - population size
 * n - sample size
 * m - number of items already selected on previous steps (0 initially)
 * t - record index, t: [0,N-1];
 * t+1 record is selected with probability (n-m)/(N-t), if m items has already been selected
 * When t=0 there is no physical record, but we make decision for the first physical record (t+1)
 */

/** Algorithm core state */
struct an_sampling_fixed_window {
	size_t population_size;	/**< Size of the population */
	size_t sample_size;		/**< Size of the sample to be chosen from the population */
	size_t current_index;	/**< Internal - current index within the population, 0-based */
	size_t selected_count;	/**< Internal - number of items selected in sample so far */
};

/**
 * @brief Initializes algorithm state
 * @param inst State instance
 * @param population_size Size of the population
 * @param sample_size Size of the sample to be selected from the population
 */
void an_sampling_fixed_window_init(struct an_sampling_fixed_window *inst, size_t population_size,
	size_t sample_size);

/**
 * @brief Uninitializes algorithm state
 * @param inst State instance
 */
void an_sampling_fixed_window_deinit(struct an_sampling_fixed_window *inst);

/**
 * @brief Resets algorithm state
 * @param inst State instance
 */
void an_sampling_fixed_window_reset(struct an_sampling_fixed_window *inst);

/**
 * @brief Checks if current population is exhausted
 * @param inst State instance
 * @return True if the population has been exhausted, false otherwise
 */
bool an_sampling_fixed_window_is_exhausted(struct an_sampling_fixed_window *inst);

/**
 * @brief Calculates if current element from the population is sampled
 *		  Automatically resets state when current population is exhausted
 * @param inst State instance
 */
bool
an_sampling_fixed_window_next_is_selected(struct an_sampling_fixed_window *inst);

#endif /* AN_SAMPLING_H */
