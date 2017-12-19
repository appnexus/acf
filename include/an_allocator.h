#ifndef AN_ALLOCATOR_H
#define AN_ALLOCATOR_H

#include "an_cc.h"

AN_EXTERN_C_BEGIN

#include "acf_export.h"

#include <stddef.h>
#include <string.h>

struct an_allocator {
	void *(*malloc)(const void *ctx, size_t size, void *return_addr);
	void *(*calloc)(const void *ctx, size_t nmemb, size_t size, void *return_addr);
	void *(*realloc)(const void *ctx, void *address, size_t size_from, size_t size_to, void *return_addr);
	void (*free)(const void *ctx, void *ptr, void *return_addr);
};

static inline void *
an_allocator_malloc(const struct an_allocator *a, size_t size, void *return_addr)
{
	return a->malloc(a, size, return_addr);
}

static inline void *
an_allocator_calloc(const struct an_allocator *a, size_t nmemb, size_t size, void *return_addr)
{
	return a->calloc(a, nmemb, size, return_addr);
}

static inline void *
an_allocator_realloc(const struct an_allocator *a, void *address, size_t size_from, size_t size_to, void *return_addr)
{
	return a->realloc(a, address, size_from, size_to, return_addr);
}

static inline void
an_allocator_free(const struct an_allocator *a, void *ptr, void *return_addr)
{
	a->free(a, ptr, return_addr);
}

char *an_allocator_strdup(const struct an_allocator *a, const char *s);

char *an_allocator_strndup(const struct an_allocator *a, const char *s, size_t n);

#define AN_MALLOC(IF, N) \
	an_allocator_malloc((IF), (N), __builtin_return_address(0))

#define AN_CALLOC(IF, N, SIZE) \
	an_allocator_calloc((IF), (N), (SIZE), __builtin_return_address(0))

#define AN_REALLOC(IF, ADDR, SIZE_FROM, SIZE_TO) \
	an_allocator_realloc((IF), (ADDR), (SIZE_FROM), (SIZE_TO), __builtin_return_address(0))

#define AN_FREE(IF, ADDR) \
	an_allocator_free((IF), (ADDR), __builtin_return_address(0))

#define AN_STRDUP(IF, S) \
	an_allocator_strdup((IF), (S))

#define AN_STRNDUP(IF, S, N) \
	an_allocator_strndup((IF), (S), (N))


/*
 * Default allocator uses plain malloc, calloc, realloc, and free
 */
ACF_EXPORT const struct an_allocator *an_default_allocator(void);
extern const struct an_allocator default_allocator;

AN_EXTERN_C_END

#endif
