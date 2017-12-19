#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common/an_server.h"
#include "common/an_buf_if.h"

struct an_buf_http_state {
	struct an_buffer *hbuf;
};

#define an_buf_http_get(buf)	\
	(((struct an_buf_http_state *)an_buf_private(buf))->hbuf)

static size_t
an_buf_http_length(an_buf_const_ptr_t buf)
{
	struct an_buffer *hbuf;

	hbuf = an_buf_http_get(buf.wbuf);
	return hbuf->size;
}

static void
an_buf_http_add(struct an_wbuf *buf, const void *data, size_t len)
{
	struct an_buffer *hbuf;

	hbuf = an_buf_http_get(buf);

	assert(hbuf->size - hbuf->in >= len);
	memcpy(hbuf->data + hbuf->in, data, len);
	hbuf->in += len;
}

static void __attribute__((format(printf, 2, 0)))
an_buf_http_add_printf(struct an_wbuf *buf, const char *fmt, va_list ap)
{
	struct an_buffer *hbuf;
	size_t left;
	int len;

	hbuf = an_buf_http_get(buf);
	left = hbuf->size - hbuf->in;
	len = vsnprintf(hbuf->data + hbuf->in, left, fmt, ap);
	if ((unsigned)len >= left) {
		/* Output was truncated. */
		hbuf->in += left;
	} else {
		hbuf->in += len;
	}
}

static const struct an_buf_if an_buf_http_if = {
	.length		= an_buf_http_length,
	.add		= an_buf_http_add,
	.add_printf	= an_buf_http_add_printf
};

struct an_wbuf *
an_buf_http_wrap(struct an_buffer *hbuf)
{
	struct an_buf_http_state *s;
	struct an_wbuf *buf;

	buf = an_wbuf_create(&an_buf_http_if, sizeof(struct an_buf_http_state));
	s = an_buf_private(buf);
	s->hbuf = hbuf;
	return buf;
}
