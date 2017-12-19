#include "an_array.h"
#include "an_allocator.h"

ACF_EXPORT const struct an_allocator *an_array_allocator = &default_allocator;

ACF_EXPORT void
an_array_set_allocator(const struct an_allocator *allocator)
{
	an_array_allocator = allocator;
}
