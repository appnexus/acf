#include <stdarg.h>
#include <string.h>

#include <ck_cc.h>

#include "common/an_array.h"
#include "common/an_buf_if.h"
#include "common/an_malloc.h"

/* Buffer flags */
#define AN_BUF_OWNED	1
#define AN_BUF_FROZEN	2

#define BUF_OP(BUF, OP, ...)	(BUF->buf.bif->OP(buf, ##__VA_ARGS__))

struct an_buf_cleanup {
	an_buf_cleanup_t *cb;
	void *arg;
};
AN_ARRAY(an_buf_cleanup, cleanup_list);

struct an_buf {
	const struct an_buf_if *bif;
	AN_ARRAY_INSTANCE(cleanup_list) cleanups;
	int flags;
	char data[] CK_CC_ALIGN(16);
};

struct an_rbuf {
	struct an_buf buf;
};

struct an_wbuf {
	struct an_buf buf;
};

static AN_MALLOC_DEFINE(an_buf_token,
    .string = "an_buf",
    .mode   = AN_MEMORY_MODE_VARIABLE);

/* Get access to the backend-specific data. */
void *
an_buf_private(an_buf_ptr_t buf)
{

	return buf.rbuf->buf.data;
}

static void
an_buf_init(struct an_buf *buf, const struct an_buf_if *bif)
{

	buf->flags = 0;
	buf->bif = bif;
	AN_ARRAY_INIT(cleanup_list, &buf->cleanups, 0);
}

struct an_rbuf *
an_rbuf_create(const struct an_buf_if *bif, size_t size)
{
	struct an_rbuf *buf;

	buf = an_calloc_region(an_buf_token, 1, sizeof(struct an_rbuf) + size);
	an_buf_init(&buf->buf, bif);
	return buf;
}

struct an_wbuf *
an_wbuf_create(const struct an_buf_if *bif, size_t size)
{
	struct an_wbuf *buf;

	buf = an_calloc_region(an_buf_token, 1, sizeof(struct an_wbuf) + size);
	an_buf_init(&buf->buf, bif);
	return buf;
}

/*
 * Consumer API.
 */

void
an_buf_own(an_buf_ptr_t buf)
{

	buf.rbuf->buf.flags |= AN_BUF_OWNED;
}

void
an_buf_disown(an_buf_ptr_t buf)
{

	buf.rbuf->buf.flags &= ~AN_BUF_OWNED;
}

bool
an_buf_owned(an_buf_const_ptr_t buf)
{

	return buf.rbuf->buf.flags & AN_BUF_OWNED;
}

void
an_buf_add_cleanup(an_buf_ptr_t buf, an_buf_cleanup_t *cb, void *arg)
{
	struct an_buf_cleanup c = { .cb = cb, .arg = arg };

	AN_ARRAY_PUSH(cleanup_list, &buf.rbuf->buf.cleanups, &c);
}

void
an_buf_destroy(an_buf_ptr_t buf)
{
	AN_ARRAY_INSTANCE(cleanup_list) *cleanups;
	struct an_buf_cleanup *c;

	cleanups = &buf.rbuf->buf.cleanups;

	while ((c = AN_ARRAY_POP(cleanup_list, cleanups, NULL)) != NULL) {
		c->cb(buf, c->arg);
	}
	if (buf.rbuf->buf.bif->destroy != NULL) {
		BUF_OP(buf.rbuf, destroy);
	}
	AN_ARRAY_DEINIT(cleanup_list, cleanups);
	an_free(an_buf_token, buf.rbuf);
}

size_t
an_buf_length(an_buf_const_ptr_t buf)
{

	return BUF_OP(buf.const_rbuf, length);
}

/* API for write buffers */
void
an_buf_add(struct an_wbuf *buf, const void *data, size_t len)
{

	assert((buf->buf.flags & AN_BUF_FROZEN) == 0);
	BUF_OP(buf, add, data, len);
}

void
an_buf_add_printf(struct an_wbuf *buf, const char *fmt, ...)
{
	va_list ap;

	assert((buf->buf.flags & AN_BUF_FROZEN) == 0);
	va_start(ap, fmt);
	BUF_OP(buf, add_printf, fmt, ap);
	va_end(ap);
}

void
an_buf_reset(struct an_wbuf *buf)
{

	assert((buf->buf.flags & AN_BUF_FROZEN) == 0);
	BUF_OP(buf, reset);
}

void
an_buf_freeze(struct an_wbuf *buf)
{

	buf->buf.flags |= AN_BUF_FROZEN;
}

void
an_buf_thaw(struct an_wbuf *buf)
{

	buf->buf.flags &= ~AN_BUF_FROZEN;
}

/* API for read buffers */
const void *
an_buf_linearize(struct an_rbuf *buf)
{

	return BUF_OP(buf, linearize);
}
