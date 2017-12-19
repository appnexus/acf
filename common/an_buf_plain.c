#include <unistd.h>

#include "common/an_buf_if.h"

struct an_buf_plain_state {
	const void *data;
	size_t len;
};

static size_t
an_buf_plain_length(an_buf_const_ptr_t buf)
{
	struct an_buf_plain_state *s;

	s = an_buf_private(buf.rbuf);
	return s->len;
}

static const void *
an_buf_plain_linearize(struct an_rbuf *buf)
{
	struct an_buf_plain_state *s;

	s = an_buf_private(buf);
	return s->data;
}

static const struct an_buf_if an_buf_plain_if = {
	.length		= an_buf_plain_length,
	.linearize	= an_buf_plain_linearize
};

struct an_rbuf *
an_buf_plain_wrap(const void *data, size_t len)
{
	struct an_buf_plain_state *s;
	struct an_rbuf *buf;

	buf = an_rbuf_create(&an_buf_plain_if,
	    sizeof(struct an_buf_plain_state));
	s = an_buf_private(buf);
	s->data = data;
	s->len = len;
	return buf;
}
