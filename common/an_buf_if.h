#ifndef AN_BUF_IF_H_
#define AN_BUF_IF_H_

#include <unistd.h>
#include <stdarg.h>
#include <stdbool.h>

#include "common/an_buf.h"

struct an_buf_if {
	void (*destroy)(an_buf_ptr_t);
	size_t (*length)(an_buf_const_ptr_t);
	const void *(*linearize)(struct an_rbuf *);
	void (*add)(struct an_wbuf *, const void *, size_t);
	void (*add_printf)(struct an_wbuf *, const char *, va_list);
	void (*reset)(struct an_wbuf *);
};

struct an_rbuf	*an_rbuf_create(const struct an_buf_if *, size_t);
struct an_wbuf	*an_wbuf_create(const struct an_buf_if *, size_t);
void		*an_buf_private(an_buf_ptr_t);

#endif /* AN_BUF_IF_H_ */
