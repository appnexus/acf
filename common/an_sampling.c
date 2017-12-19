#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "common/an_rand.h"
#include "common/an_sampling.h"

void
an_sampling_fixed_window_init(struct an_sampling_fixed_window *inst, size_t population_size,
	size_t sample_size)
{

	memset(inst, 0, sizeof(struct an_sampling_fixed_window));
	inst->population_size = population_size;
	inst->sample_size = sample_size;
	an_sampling_fixed_window_reset(inst);
}

void
an_sampling_fixed_window_deinit(struct an_sampling_fixed_window *inst)
{

	an_sampling_fixed_window_reset(inst);
}

void
an_sampling_fixed_window_reset(struct an_sampling_fixed_window *inst)
{

	inst->current_index = 0;
	inst->selected_count = 0;
}

bool
an_sampling_fixed_window_is_exhausted(struct an_sampling_fixed_window *inst)
{

	return (inst->current_index >= inst->population_size);
}

bool
an_sampling_fixed_window_next_is_selected(struct an_sampling_fixed_window *inst)
{
	bool is_selected = false;

	if (an_sampling_fixed_window_is_exhausted(inst)) {
		an_sampling_fixed_window_reset(inst);
	}

	if (inst->selected_count < inst->sample_size) {
		is_selected = (double)(inst->population_size - inst->current_index) * an_drandom() < (inst->sample_size -
			inst->selected_count);
	}

	if (is_selected) {
		inst->selected_count++;
	}
	inst->current_index++;

	return is_selected;
}
