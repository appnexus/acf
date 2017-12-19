#include "common/an_malloc.h"
#include "common/an_rand.h"
#include "common/an_streaming_quantile.h"
#include "common/an_thread.h"

static AN_MALLOC_DEFINE(streaming_qnt_token,
    .string = "streaming_qnt",
    .mode = AN_MEMORY_MODE_FIXED,
    .size = sizeof(struct an_streaming_qnt));

struct an_streaming_qnt *
an_streaming_qnt_estimate_create(double quantile, uint64_t initial_value, double adjustment_value)
{
	struct an_streaming_qnt *new_qnt;

	new_qnt = an_calloc_object(streaming_qnt_token);
	new_qnt->quantile = quantile;
	new_qnt->estimate = initial_value;
	new_qnt->adjustment_value = adjustment_value;

	return new_qnt;
};

void
an_streaming_qnt_update_mpmc(struct an_streaming_qnt *streaming_qnt, uint64_t sample)
{
	bool is_below_quantile = an_random_indicator(streaming_qnt->quantile);
	uint64_t curr_estimate = ck_pr_load_64(&streaming_qnt->estimate);

	if (sample < curr_estimate) {
		if (is_below_quantile == false) {
			/* only update if curr_estimate hasn't changed since before first if check.
			 * if it has changed, don't update. we're ok with the occasional missed update,
			 * rather than looping until it's safe to update */
			ck_pr_cas_64(&streaming_qnt->estimate, curr_estimate, curr_estimate - streaming_qnt->adjustment_value);
		}
	} else if (sample > curr_estimate) {
		if (is_below_quantile == true) {
			ck_pr_cas_64(&streaming_qnt->estimate, curr_estimate, curr_estimate + streaming_qnt->adjustment_value);
		}
	}
}

void
an_streaming_qnt_update_spmc(struct an_streaming_qnt *streaming_qnt, uint64_t sample)
{
	uint64_t curr_estimate = streaming_qnt->estimate;
	bool is_below_quantile = an_random_indicator(streaming_qnt->quantile);

	if (sample < curr_estimate) {
		if (is_below_quantile == false) {
			streaming_qnt->estimate -= streaming_qnt->adjustment_value;
		}
	} else {
		if (is_below_quantile == true) {
			streaming_qnt->estimate += streaming_qnt->adjustment_value;
		}
	}
}

uint64_t
an_streaming_qnt_observe(const struct an_streaming_qnt *streaming_qnt)
{

	return ck_pr_load_64(&streaming_qnt->estimate);
}

void
an_streaming_qnt_destroy(struct an_streaming_qnt *streaming_qnt)
{

	an_free(streaming_qnt_token, streaming_qnt);
}
