#ifndef AN_BUF_HTTP_H_
#define AN_BUF_HTTP_H_

#include "common/an_buf.h"

struct an_buffer;

struct an_wbuf *an_buf_http_wrap(struct an_buffer *);

#endif /* AN_BUF_HTTP_H_ */
