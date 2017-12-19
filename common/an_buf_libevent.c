#include <event2/buffer.h>

#include <ck_md.h>

#include <unistd.h>

#include "common/an_buf_if.h"

/* A reasonably large initial buffer size */
#define AN_BUF_LIBEVENT_INITIAL_SIZE	(2 * getpagesize() - CK_MD_CACHELINE)

struct an_buf_libevent_state {
	struct evbuffer *evbuf;
};

#define an_buf_libevent_get(buf)	\
	(((struct an_buf_libevent_state *)an_buf_private(buf))->evbuf)

static void
an_buf_libevent_destroy(an_buf_ptr_t buf)
{
	struct evbuffer *evbuf;

	if (!an_buf_owned(buf.rbuf)) {
		return;
	}
	evbuf = an_buf_libevent_get(buf);
	evbuffer_free(evbuf);
}

static size_t
an_buf_libevent_length(an_buf_const_ptr_t buf)
{
	struct evbuffer *evbuf;

	evbuf = an_buf_libevent_get(buf.rbuf);
	return evbuffer_get_length(evbuf);
}

static void
an_buf_libevent_add(struct an_wbuf *buf, const void *data, size_t len)
{
	struct evbuffer *evbuf;

	evbuf = an_buf_libevent_get(buf);
	evbuffer_add(evbuf, data, len);
}

static void __attribute__((format(printf, 2, 0)))
an_buf_libevent_add_printf(struct an_wbuf *buf, const char *fmt, va_list ap)
{
	struct evbuffer *evbuf;

	evbuf = an_buf_libevent_get(buf);
	evbuffer_add_vprintf(evbuf, fmt, ap);
}

static void
an_buf_libevent_reset(struct an_wbuf *buf)
{
	struct evbuffer *evbuf;

	evbuf = an_buf_libevent_get(buf);
	evbuffer_drain(evbuf, evbuffer_get_length(evbuf));
}

static const void *
an_buf_libevent_linearize(struct an_rbuf *buf)
{
	struct evbuffer *evbuf;

	evbuf = an_buf_libevent_get(buf);
	return evbuffer_pullup(evbuf, -1);
}

static const struct an_buf_if an_buf_libevent_if = {
	.destroy	= an_buf_libevent_destroy,
	.length		= an_buf_libevent_length,
	.linearize	= an_buf_libevent_linearize,
	.add		= an_buf_libevent_add,
	.add_printf	= an_buf_libevent_add_printf,
	.reset		= an_buf_libevent_reset
};

struct an_rbuf *
an_buf_libevent_wrap(struct evbuffer *evbuf)
{
	struct an_buf_libevent_state *s;
	struct an_rbuf *buf;

	buf = an_rbuf_create(&an_buf_libevent_if,
	    sizeof(struct an_buf_libevent_state));
	s = an_buf_private(buf);
	s->evbuf = evbuf;
	return buf;
}

struct an_wbuf *
an_buf_libevent_create(void)
{
	struct an_buf_libevent_state *s;
	struct an_wbuf *buf;
	struct evbuffer *evbuf;

	evbuf = evbuffer_new();
	evbuffer_set_strategy(evbuf, EVBUFFER_STRATEGY_CHAIN,
	    AN_BUF_LIBEVENT_INITIAL_SIZE);

	buf = an_wbuf_create(&an_buf_libevent_if,
	    sizeof(struct an_buf_libevent_state));
	s = an_buf_private(buf);
	s->evbuf = evbuf;
	an_buf_own(buf);
	return buf;
}
