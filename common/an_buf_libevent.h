#ifndef AN_BUF_LIBEVENT_H_
#define AN_BUF_LIBEVENT_H_

#include "common/an_buf.h"

struct evbuffer;

struct an_rbuf *an_buf_libevent_wrap(struct evbuffer *);
struct an_wbuf *an_buf_libevent_create(void);

#endif /* AN_BUF_LIBEVENT_H_ */
