#ifndef AN_STRING_H
#define AN_STRING_H

#include <evhttp.h>
#include <stdlib.h>

#if defined(__GNUC__) || defined(__clang__)
#define MALLOC_ATTRIBUTE __attribute__((malloc, warn_unused_result))
#else
#define MALLOC_ATTRIBUTE
#endif

MALLOC_ATTRIBUTE char *an_string_malloc(size_t);
void an_string_free(char *);
void an_string_defer(char *);
void an_string_sstm_call(char *);
MALLOC_ATTRIBUTE char *an_string_dup(const char *);
MALLOC_ATTRIBUTE char *an_string_strndup(const char *, size_t);
MALLOC_ATTRIBUTE char *an_string_realloc(char *src, size_t old_size, size_t new_size);
int an_string_vasprintf(char **, const char *, va_list) __attribute__((format(printf, 2, 0)));
int an_string_asprintf(char **, const char *, ...) __attribute__((format(printf, 2, 3)));

struct an_json_parser;
MALLOC_ATTRIBUTE char *an_string_jsonp_dup(struct an_json_parser *);
MALLOC_ATTRIBUTE char *an_string_jsonp_escaped_dup(struct an_json_parser *);
MALLOC_ATTRIBUTE char *an_string_dup_urldecode(const char *s, size_t len);
MALLOC_ATTRIBUTE char *an_string_dup_tolower(const char *s, size_t len);

#undef MALLOC_ATTRIBUTE

#endif /* _AN_STRING_H */
