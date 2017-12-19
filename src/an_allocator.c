#include "an_allocator.h"

#include <stdlib.h>
#include <string.h>

static void *
an_default_malloc(const void *ctx, size_t size, void *return_addr)
{
	(void)ctx;
	(void)return_addr;

	return malloc(size);
}

static void *
an_default_calloc(const void *ctx, size_t nmemb, size_t size, void *return_addr)
{
	(void)ctx;
	(void)return_addr;

	return calloc(nmemb, size);
}

static void *
an_default_realloc(const void *ctx, void *address, size_t size_from, size_t size_to, void *return_addr)
{
	(void)ctx;
	(void)return_addr;
	(void)size_from;

	return realloc(address, size_to);
}

static void
an_default_free(const void *ctx, void *ptr, void *return_addr)
{
	(void)ctx;
	(void)return_addr;

	free(ptr);
}

ACF_EXPORT const struct an_allocator default_allocator = {
	.malloc = an_default_malloc,
	.calloc = an_default_calloc,
	.realloc = an_default_realloc,
	.free = an_default_free
};

ACF_EXPORT const struct an_allocator *
an_default_allocator()
{

	return &default_allocator;
}

ACF_EXPORT char *
an_allocator_strdup(const struct an_allocator *a, const char *s)
{
	if (s == NULL) {
		return NULL;
	}

	size_t len = strlen(s);
	char *r = AN_MALLOC(a, len + 1);
	memcpy(r, s, len);
	r[len] = '\0';

	return r;
}

ACF_EXPORT char *
an_allocator_strndup(const struct an_allocator *a, const char *s, size_t n)
{
	if (s == NULL) {
		return NULL;
	}

	size_t len = strnlen(s, n);
	char *r = AN_MALLOC(a, len + 1);
	memcpy(r, s, len);
	r[len] = '\0';

	return r;
}
