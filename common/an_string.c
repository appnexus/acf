#include <modp_ascii.h>
#include <modp_burl.h>
#include <string.h>

#include "common/an_malloc.h"
#include "common/an_sstm.h"
#include "common/an_string.h"
#include "common/an_thread.h"
#include "common/json_parser.h"

/* string type support */
static AN_MALLOC_DEFINE(an_string_token,
    .string = "an_string",
    .mode = AN_MEMORY_MODE_VARIABLE,
    .use_pool_allocation = true);

char *
an_string_malloc(size_t size)
{

	return an_malloc_region(an_string_token, size);
}

void
an_string_free(char *string)
{

	an_free(an_string_token, string);
	return;
}

void
an_string_defer(char *string)
{

	if (string == NULL) {
		return;
	}

	an_thread_defer(string,
	    AN_CC_CAST_CB(void, an_string_free, string));
	return;
}

void
an_string_sstm_call(char *string)
{

	if (string == NULL) {
		return;
	}

	an_sstm_call(an_string_free, string);
	return;
}

char *
an_string_dup(const char *string)
{
	char *copy;
	size_t len;

	if (string == NULL)
		return NULL;

	len = strlen(string) + 1;
	copy = an_string_malloc(len);
	memcpy(copy, string, len);

	return copy;
}

char *
an_string_strndup(const char *string, size_t n)
{
	size_t len;
	char *copy;

	if (string == NULL)
		return NULL;

	len = strnlen(string, n);
	copy = an_string_malloc(len + 1);
	memcpy(copy, string, len);
	copy[len] = '\0';
	return copy;
}

char *
an_string_realloc(char *src, size_t old_size, size_t new_size)
{

	return an_realloc_region(an_string_token, src, old_size, new_size);
}

int __attribute__((format(printf, 2, 0)))
an_string_vasprintf(char **strp, const char *fmt, va_list ap)
{
	va_list aq;
	va_copy(aq, ap);
	int size = vsnprintf(NULL, 0, fmt, ap);
	if (size < 0) {
		goto failure;
	}

	*strp = an_string_malloc(size + 1);
	size = vsprintf(*strp, fmt, aq);
	va_end(aq);

	return size;

failure:
	va_end(aq);
	return -1;
}

int
an_string_asprintf(char **strp, const char *fmt, ...)
{
	int ret;
	va_list ap;

	va_start(ap, fmt);
	ret = an_string_vasprintf(strp, fmt, ap);
	va_end(ap);

	return ret;
}

static inline char *
_an_string_jsonp_dup(an_json_parser_t *parser, size_t (*fn)(an_json_parser_t *, char *, int))
{
	char *ret;
	size_t len;

	if (parser->type != AN_JSON_STRING || parser->val_len == 0) {
		return NULL;
	}

	len = parser->val_len + 1;
	ret = an_string_malloc(len);
	len = fn(parser, ret, len);
	if (len == 0) {
		an_string_free(ret);
		return NULL;
	}

	return ret;
}

char *
an_string_jsonp_dup(an_json_parser_t *parser)
{

	return _an_string_jsonp_dup(parser, an_json_get_string);
}

char *
an_string_jsonp_escaped_dup(an_json_parser_t *parser)
{

	return _an_string_jsonp_dup(parser, an_json_get_escaped_string);
}

char *
an_string_dup_urldecode(const char *s, size_t len)
{
	char *decode;

	if (s == NULL) {
		return NULL;
	}

	if (len == 0) {
		len = strlen(s);
	}

	decode = an_string_malloc(modp_burl_decode_len(len));
	modp_burl_decode(decode, s, len);

	return decode;
}

char *
an_string_dup_tolower(const char *s, size_t len)
{
	char *lower;

	if (s == NULL) {
		return NULL;
	}

	if (len == 0) {
		len = strlen(s);
	}

	lower = an_string_malloc(len + 1);
	modp_tolower_copy(lower, s, len);
	lower[len] = '\0';

	return lower;
}
