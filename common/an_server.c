#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/tcp.h>

#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netdb.h>
#include <numa.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <ck_pr.h>
#include <ck_ring.h>

#include "common/memory/pool.h"
#include "common/rtbr/rtbr.h"
#include "common/an_array.h"
#include "common/an_buf_http.h"
#include "common/an_cc.h"
#include "common/an_malloc.h"
#include "common/an_string.h"
#include "common/an_md.h"
#include "common/an_rand.h"
#include "common/an_server.h"
#include "common/an_syslog.h"
#include "common/server_config.h"
#include "common/util.h"
#include "third_party/http-parser/http_parser.h"

#define INITIAL_BUFFER_SIZE	4096ULL
#define INITIAL_NUM_EVENTS	64ULL
#define MAX_REQUEST_TIME	1000000ULL /* Maximum request time, in us. */
#define POOL_SIZE		(4 * 1024 * 1024 * 1024ULL)	/* 4GB */
#define BUMP_SIZE		(16 * 1024 * 1024ULL)		/* 16MB */
#define TOTAL_LARGE_ALLOCATION_LIMIT POOL_SIZE

/*
 * A large 64 bits prime number and its multiplicative inverse
 * modulo 2^64. When encoding, we multiply the request ID by the
 * first number, and by the second number when decoding, in order
 * to maximize the chances of ending up with completely incorrect
 * IDs in the face of even small (one bit) corruption.
 */
#define RID_FACTOR	0x10000FFFFFFE1ULL
#define RID_FACTOR_INV	0x37F114C742108421ULL
#define RID_GEN_MASK		((1UL << 28) - 1)
#define RID_CONNIDX_MASK	((1UL << 28) - 1)
#define RID_CONNIDX_SHIFT	28
#define RID_IOTDIDX_SHIFT	56

_Static_assert((uint64_t)(RID_FACTOR * RID_FACTOR_INV) == 1,
    "RID_FACTOR_INV * RID_FACTOR modulo 2^64 must be 1");

#define AN_IO_PARENT(PTR, TYPE)		container_of(PTR, TYPE, io)

#define an_pool_get(POOL, SIZE)						\
	(__builtin_choose_expr(						\
	    __builtin_types_compatible_p(__typeof__(POOL), struct an_pool_private *), \
		an_pool_input_get, an_pool_output_get)(SIZE))

/* Uncomment this to trace connection state changes. */
//#define AN_IO_DEBUG	1

#ifdef AN_IO_DEBUG
#define AN_IO_TRACE(IOTD, FMT, ...)					\
	an_io_trace(IOTD, __LINE__, FMT, ##__VA_ARGS__)
#else
#define AN_IO_TRACE(IOTD, FMT, ...)	do {} while (0)
#endif

#define AN_IO_CONNECTION_STATE(CONN, S)					\
	do {								\
		AN_IO_TRACE(CONN->iotd, "i:%x g:%"PRIx32" %s -> %s\n",	\
		    (uint64_t)(CONN - CONN->iotd->connections),		\
		    CONN->generation,					\
		    an_io_connection_state_str(CONN->state),		\
		    an_io_connection_state_str(S));			\
		CONN->state = S;					\
	} while (0);

/*
 * TODO:
 *
 * - Support for SSL/TLS.
 */

enum an_io_connection_state {
	HTTP_CONNECTION_FREE,		/* Unused connection ready for use */
	HTTP_CONNECTION_IDLE,		/* Established but idle connection */
	HTTP_CONNECTION_READING,	/* Reading request bytes */
	HTTP_CONNECTION_PROCESSING,	/* Request is in-flight */
	HTTP_CONNECTION_WRITING,	/* Writing response bytes */
	HTTP_CONNECTION_CLOSING		/* In asynchronous tear-down */
};

/* Input and output buffer pools */
AN_POOL_PRIVATE(static, input, BUMP_SIZE, POOL_SIZE);
AN_POOL_SHARED(static, output, BUMP_SIZE, POOL_SIZE);

struct an_http_response {
	an_request_id_t id;
	struct an_buffer *buf;
	struct an_http_response *next;
};

enum an_io_kind {
	AN_IO_LISTENER,
	AN_IO_CONNECTION,
	AN_IO_THREAD
};

/* An I/O object we can monitor with epoll */
struct an_io {
	enum an_io_kind kind;	/* The kind of I/O object */
	uint32_t events;	/* The events we're subscribed for */
	int fd;			/* The socket */
};

/* The decoded information in an an_request_id_t */
struct an_request_location {
	unsigned int iotd_idx;
	uint32_t conn_idx;
	uint32_t generation;
};

/* An HTTP connection */
struct an_io_connection {
	struct an_io_thread *iotd;
	struct an_io_listener *listener;
	struct an_io io;
	enum an_io_connection_state state;
	struct an_rtbr_section rtbr_section;
	uint64_t request_start;
	uint32_t generation;
	uint64_t timeout;
	http_parser parser;
	bool keepalive;
	bool remote_closed;
	struct an_buffer *inbuf;
	struct an_buffer *outbuf;
	/*
	 * Since we do not support HTTP pipelining, we process requests
	 * in a strictly sequential fashion, and we can embed the public
	 * request object here to avoid allocations.
	 */
	struct an_http_request request;
	/* Linkage for the free connections list */
	SLIST_ENTRY(an_io_connection) free_next;
	/* Linkage for the idle connections list */
	LIST_ENTRY(an_io_connection) idle_next;
	/* Linkage for the active connections list */
	LIST_ENTRY(an_io_connection) active_next;
};

struct an_io_listener {
	struct an_io_thread *iotd;
	char *service;
	struct an_io io;
};

AN_ARRAY_PRIMITIVE(struct an_io_listener *, listener_list);
AN_ARRAY_PRIMITIVE(struct epoll_event *, events_list);

CK_RING_PROTOTYPE(requests, an_http_request);
CK_RING_PROTOTYPE(responses, an_http_response);

enum an_io_stat {
	/* Current number of established connections. */
	AN_IO_NUM_CONNS,
	/* Current number of established and active connections. */
	AN_IO_ACTIVE_CONNS,
	/* Read errors */
	AN_IO_READ_ERRORS,
	/* Write errors */
	AN_IO_WRITE_ERRORS,
	/* Request timeout exceeded */
	AN_IO_REQUEST_TIMEOUT,
	/* Number of times the connection was closed early by our peer */
	AN_IO_RESET_BY_PEER,
	/*
	 * Number of times we refused a connection request because
	 * we reached the maximum connections limit.
	 */
	AN_IO_REFUSED_CONNS,
	/* Same but for the active connections limit. */
	AN_IO_REFUSED_ACTIVE_CONNS,
	/* Number of malformed requests */
	AN_IO_MALFORMED_REQS,
	/* Number of times we failed to allocate memory */
	AN_IO_OOM_FAILURES,
	/* Number of valid requests processed */
	AN_IO_NUM_REQUESTS,
	AN_IO_STAT_MAX
};

struct an_io_stats {
	uint64_t values[AN_IO_STAT_MAX];
};

struct an_io_thread {
	struct an_io_server *server;
	pthread_t thread;
	bool quiesce;

	/* Request FIFO and its backing array (SPMC) */
	ck_ring_t requests_fifo;
	struct an_http_request *requests_buffer;

	/* Concurrent stack of responses (MPSC) */
	struct an_http_response *responses_head;

	/* Connections storage and management */
	uint32_t max_total_connections;
	uint32_t max_active_connections;
	struct an_io_connection *connections;
	/* Free list of connection objects */
	SLIST_HEAD(, an_io_connection) free_conns;
	/* List of established but idle connections */
	LIST_HEAD(, an_io_connection) idle_conns;
	/* List of established and active connections */
	LIST_HEAD(, an_io_connection) active_conns;

	/* I/O event notification bits */
	struct epoll_event *events;
	unsigned int nevents;
	int epollfd;
	uint64_t request_timeout;

	/* The eventfd notifying us we have responses to process */
	struct an_io io;

	/* Statistics */
	struct an_io_stats stats;

	/* The listeners attached to this I/O thread. */
	AN_ARRAY_INSTANCE(listener_list) listeners;
};

struct an_io_server {
	/* Startup and shutdown management. */
	sem_t startup;
	sem_t shutdown;
	unsigned int threads_finished;
	bool ready;
	/*
	 * When the flag below is set to a non-zero value, it means we
	 * are currently shutting down this application, and I/O threads
	 * should stop accepting new connections or requests, terminate
	 * all idle connections, and close currently active ones after
	 * the request has completed.
	 */
	uint8_t quiesce;	/* This would be a bool if ck_pr supported it */

	/* Worker threads eventfd */
	int workers_eventfd;

	struct an_io_thread *threads;
	unsigned int num_threads;

	/* Shared across all I/O threads and connections */
	http_parser_settings parser_settings;

	/* Only used if AN_IO_DEBUG is defined. */
	int tracefd;

	/* Hard limit on response sizes. */
	size_t max_response_size;
};

static AN_MALLOC_DEFINE(an_io_server_token,
    .string = "an_io_server",
    .mode   = AN_MEMORY_MODE_FIXED,
    .size   = sizeof(struct an_io_server));

static AN_MALLOC_DEFINE(an_io_thread_token,
    .string = "an_io_thread",
    .mode   = AN_MEMORY_MODE_VARIABLE);

static AN_MALLOC_DEFINE(an_io_listener_token,
    .string = "an_io_listener",
    .mode   = AN_MEMORY_MODE_FIXED,
    .size   = sizeof(struct an_io_listener));

static AN_MALLOC_DEFINE(an_http_response_token,
    .string = "an_http_response",
    .mode   = AN_MEMORY_MODE_FIXED,
    .size   = sizeof(struct an_http_response));

static AN_MALLOC_DEFINE(an_io_ring_buffer_token,
    .string = "ck_ring_buffer_t",
    .mode   = AN_MEMORY_MODE_VARIABLE);

static AN_MALLOC_DEFINE(an_io_connection_token,
    .string = "an_io_connection array",
    .mode   = AN_MEMORY_MODE_VARIABLE);

static AN_MALLOC_DEFINE(an_io_event_token,
    .string = "struct epoll_event",
    .mode   = AN_MEMORY_MODE_VARIABLE);

static void an_io_subscribe(const struct an_io_thread *, struct an_io *, uint32_t);

static uint64_t an_server_large_allocations = 0;

/* Used by the stats callback. */
static struct an_io_server *server;

/* Request ID encoding and decoding functions. */
static inline void
an_request_id_encode(an_request_id_t *rid, const struct an_io_thread *iotd,
    const struct an_io_connection *conn)
{

	/*
	 * The request ID is a 64 bits number with the less significant
	 * 28 bits holding the random generation number, while the next
	 * 28 bits hold the connection index and the 8 most significant
	 * bits hold the I/O thread index, as shown below:
	 *
	 *   8 bits           28 bits                      28 bits
	 * [--------|----------------------------|----------------------------]
	 *     |                  |                            |
	 *     v                  |                            v
	 * I/O thread index       |                    generation number
	 *                        v
	 *                  connection index
	 */
	rid->id =
	    ((uint64_t)(iotd - iotd->server->threads) << RID_IOTDIDX_SHIFT) |
	    ((uint64_t)(conn - iotd->connections) << RID_CONNIDX_SHIFT) |
	    conn->generation;
	rid->id *= RID_FACTOR;
}

static inline struct an_request_location
an_request_id_decode(an_request_id_t erid)
{
	uint64_t rid;

	rid = erid.id * RID_FACTOR_INV;
	return (struct an_request_location) {
		.iotd_idx = rid >> RID_IOTDIDX_SHIFT,
		.conn_idx = (rid >> RID_CONNIDX_SHIFT) & RID_CONNIDX_MASK,
		.generation = rid & RID_GEN_MASK
	};
}

AN_CC_UNUSED static void
an_io_trace(struct an_io_thread *iotd, int line, const char *fmt, ...)
{
	struct an_io_server *server;
	char linebuf[256];
	va_list ap;
	ssize_t nbytes;
	size_t total;
	int ret;

	ret = snprintf(linebuf, sizeof(linebuf), "L%d ", line);
	assert(ret > 0 && (unsigned)ret < sizeof(linebuf));
	total = ret;

	va_start(ap, fmt);
	ret = vsnprintf(linebuf + total, sizeof(linebuf) - total, fmt, ap);
	assert(ret > 0 && (unsigned)ret < (sizeof(linebuf) - total));
	total += ret;
	va_end(ap);

	server = iotd->server;
	do {
		nbytes = write(server->tracefd, linebuf, total);
		assert(nbytes > 0 && (unsigned)nbytes <= total);
		total -= nbytes;
	} while (total > 0);
}

AN_CC_UNUSED static const char *
an_io_connection_state_str(enum an_io_connection_state state)
{

	switch (state) {
	case HTTP_CONNECTION_FREE:
		return "FREE";
	case HTTP_CONNECTION_IDLE:
		return "IDLE";
	case HTTP_CONNECTION_READING:
		return "READING";
	case HTTP_CONNECTION_PROCESSING:
		return "PROCESSING";
	case HTTP_CONNECTION_WRITING:
		return "WRITING";
	case HTTP_CONNECTION_CLOSING:
		return "CLOSING";
	}

	return NULL;
}

/* HTTP parser callbacks. */
static int
on_url(http_parser *parser, const char *data, size_t len)
{
	struct an_io_connection *conn;
	struct an_http_request *req;
	struct an_buffer *buffer;

	conn = parser->data;
	req = &conn->request;
	buffer = conn->inbuf;

	if (AN_CC_LIKELY(req->uri_offset == 0)) {
		req->uri_offset = data - buffer->data;
	}
	req->uri_len += len;
	return 0;
}

static int
on_body(http_parser *parser, const char *data, size_t len)
{
	struct an_io_connection *conn;
	struct an_http_request *req;
	struct an_buffer *buffer;

	conn = parser->data;
	req = &conn->request;
	buffer = conn->inbuf;

	if (AN_CC_LIKELY(req->body_offset == 0)) {
		req->body_offset = data - buffer->data;
	}
	req->body_len += len;
	return 0;
}

static int
on_message_complete(http_parser *parser)
{
	struct an_io_connection *conn;

	conn = parser->data;

	if (!http_should_keep_alive(parser)) {
		conn->keepalive = false;
	}

	AN_IO_CONNECTION_STATE(conn, HTTP_CONNECTION_PROCESSING);
	return 0;
}

/* Stats */
static inline void
an_io_stat_inc(struct an_io_thread *iotd, enum an_io_stat stat)
{

	ck_pr_inc_64(&iotd->stats.values[stat]);
}

static inline void
an_io_stat_dec(struct an_io_thread *iotd, enum an_io_stat stat)
{

	ck_pr_dec_64(&iotd->stats.values[stat]);
}

static inline uint64_t
an_io_stat_get(struct an_io_thread *iotd, enum an_io_stat stat, bool clear)
{

	if (clear == true) {
		return ck_pr_fas_64(&iotd->stats.values[stat], 0);
	}
	return ck_pr_load_64(&iotd->stats.values[stat]);
}

static void
an_io_thread_stats(struct an_io_thread *iotd, unsigned int id,
    struct evbuffer *buf, double elapsed, bool clear)
{

	evbuffer_add_printf(buf,
	    "iothread.%u.num_conns_sum: %"PRIu64"\n", id,
	    an_io_stat_get(iotd, AN_IO_NUM_CONNS, false));
	evbuffer_add_printf(buf,
	    "iothread.%u.active_conns_sum: %"PRIu64"\n", id,
	    an_io_stat_get(iotd, AN_IO_ACTIVE_CONNS, false));
	evbuffer_add_printf(buf,
	    "iothread.%u.read_errors_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_READ_ERRORS, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.request_timeouts_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_REQUEST_TIMEOUT, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.write_errors_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_WRITE_ERRORS, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.client_resets_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_RESET_BY_PEER, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.refused_conns_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_REFUSED_CONNS, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.refused_active_conns_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_REFUSED_ACTIVE_CONNS, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.malformed_reqs_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_MALFORMED_REQS, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.oom_failures_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_OOM_FAILURES, clear) / elapsed);
	evbuffer_add_printf(buf,
	    "iothread.%u.num_requests_sum: %.3f\n", id,
	    (double)an_io_stat_get(iotd, AN_IO_NUM_REQUESTS, clear) / elapsed);
}

static void
an_io_server_stats(struct evbuffer *buf, double elapsed, bool clear)
{
	unsigned int i;

	for (i = 0; i < server->num_threads; i++) {
		an_io_thread_stats(&server->threads[i], i, buf, elapsed, clear);
	}
}

/* Internal request buffer API */
static bool
an_pool_grow_to(struct an_buffer *buf, size_t want)
{
	uint64_t new_size;
	void *data;

	new_size = next_power_of_2(want);

	if (buf->external_allocation) {
		data = realloc(buf->data, new_size);
		if (AN_CC_UNLIKELY(data == NULL)) {
			return false;
		}

		buf->data = data;
		buf->size = new_size;
		return true;
	}

	data = an_pool_alloc(&input, new_size, false, 8);
	if (AN_CC_UNLIKELY(data == NULL)) {
		return false;
	}

	memcpy(data, buf->data, buf->in);
	buf->data = data;
	buf->size = new_size;
	return true;
}

static bool
an_pool_grow(struct an_buffer *buf)
{

	return an_pool_grow_to(buf, buf->size + 1);
}

static struct an_buffer *
an_pool_input_get(size_t want)
{
	struct an_buffer *buf;

	buf = an_pool_alloc(&input, sizeof(struct an_buffer), false, 8);
	if (AN_CC_UNLIKELY(buf == NULL)) {
		return NULL;
	}

	buf->data = an_pool_alloc(&input, want, true, 8);
	if (AN_CC_UNLIKELY(buf->data == NULL)) {
		return NULL;
	}

	buf->in = 0;
	buf->out = 0;
	buf->size = want;
	buf->external_allocation = false;

	return buf;
}

static struct an_buffer *
an_pool_output_get(size_t want)
{
	struct an_buffer *buf;

	buf = an_pool_alloc(&output, sizeof(struct an_buffer), false, 8);
	if (AN_CC_UNLIKELY(buf == NULL)) {
		return NULL;
	}

	buf->data = an_pool_alloc(&output, want, true, 8);
	if (AN_CC_UNLIKELY(buf->data == NULL)) {
		return NULL;
	}

	buf->in = 0;
	buf->out = 0;
	buf->size = want;
	buf->external_allocation = false;

	return buf;
}

/* I/O object API */
static void
an_io_init(struct an_io *io, enum an_io_kind kind, int fd)
{

	io->kind = kind;
	io->fd = fd;
	io->events = 0;
}

static void
an_io_deinit(struct an_io_thread *iotd, struct an_io *io)
{

	an_io_subscribe(iotd, io, 0);
	if (io->fd != -1) {
		close(io->fd);
		io->fd = -1;
	}
}

static int
an_io_setopt(const struct an_io *io, int level, int opt)
{
	int ret, on;

	on = 1;
	ret = setsockopt(io->fd, level, opt, &on, sizeof(on));
	if (ret != 0) {
		an_syslog(LOG_WARNING, "Failed to enable socket option %d "
		    "(level %d): %d (%s)", opt, level, errno,
		    an_strerror(errno));
	}
	return ret;
}

static int
an_io_cloexec(const struct an_io *io)
{
	int ret, flags;

	flags = fcntl(io->fd, F_GETFD);
	if (flags == -1) {
		an_syslog(LOG_CRIT, "Failed to get fcntl flags for socket: "
		    "%d (%s)", errno, an_strerror(errno));
		return -1;
	}
	ret = fcntl(io->fd, F_SETFD, flags | FD_CLOEXEC);
	if (ret == -1) {
		an_syslog(LOG_CRIT, "Failed to set fcntl flags for socket: "
		    "%d (%s)", errno, an_strerror(errno));
		return -1;
	}

	return 0;
}

int
an_io_thread_listen(struct an_io_thread *iotd, const char *host,
    in_port_t port)
{
	struct an_io_listener *listener;
	struct an_io io;
	struct addrinfo hints;
	struct addrinfo *result, *ai;
	char service[8];
	int ret, fd;
	bool found;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	hints.ai_protocol = IPPROTO_TCP;

	(void)snprintf(service, sizeof(service), "%d", port);

	ret = getaddrinfo(host, service, &hints, &result);
	if (ret != 0) {
		an_syslog(LOG_CRIT, "Failed to lookup own address: %s",
		    gai_strerror(ret));
		return -1;
	}

	found = false;
	for (ai = result; ai != NULL; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd == -1) {
			continue;
		}

		an_io_init(&io, AN_IO_LISTENER, fd);

		an_io_cloexec(&io);
		an_io_setopt(&io, SOL_SOCKET, SO_REUSEADDR);
		an_io_setopt(&io, SOL_SOCKET, SO_REUSEPORT);
		an_io_setopt(&io, SOL_SOCKET, SO_KEEPALIVE);
		an_io_setopt(&io, IPPROTO_TCP, TCP_NODELAY);
		an_io_setopt(&io, IPPROTO_TCP, TCP_QUICKACK);

		ret = bind(fd, ai->ai_addr, ai->ai_addrlen);
		if (ret == 0) {
			found = true;
			break;
		}

		close(fd);
	}
	freeaddrinfo(result);

	if (found == false) {
		an_syslog(LOG_CRIT,
		    "Failed to find a suitable address to listen on");
		return -1;
	}

	ret = listen(fd, iotd->max_active_connections);
	if (ret != 0) {
		close(fd);
		an_syslog(LOG_CRIT, "Failed to listen on socket: %d (%s)",
		    errno, an_strerror(errno));
		return -1;
	}

	an_syslog(LOG_NOTICE, "I/O thread #%d listening on %s:%u",
	    (int)(iotd - iotd->server->threads),
	    host == NULL ? "*" : host, port);
	listener = an_calloc_object(an_io_listener_token);
	listener->io = io;
	listener->iotd = iotd;
	listener->service = an_string_dup(service);

	AN_ARRAY_PUSH(listener_list, &iotd->listeners, &listener);

	an_io_subscribe(iotd, &listener->io, EPOLLIN);
	return 0;
}

static void
an_io_listener_destroy(struct an_io_listener *listener)
{

	an_string_free(listener->service);
	an_io_deinit(listener->iotd, &listener->io);
	an_free(an_io_listener_token, listener);
}

/* Configure the server to listen on the specified address and port. */
int
an_io_server_listen(struct an_io_server *server, const char *host,
    in_port_t port)
{
	unsigned int i;
	int ret;

	for (i = 0; i < server->num_threads; i++) {
		ret = an_io_thread_listen(&server->threads[i], host, port);
		if (ret != 0) {
			return -1;
		}
	}
	return 0;
}

static void
an_io_thread_wakeup(const struct an_io_thread *iotd)
{
	int ret;

	ret = eventfd_write(iotd->io.fd, 1);
	assert(ret == 0);
}

bool
an_io_server_quiesce(struct an_io_server *server)
{
	unsigned int i;
	int error;

	if (!server->quiesce) {
		server->threads_finished = 0;
		ck_pr_store_8(&server->quiesce, true);
		for (i = 0; i < server->num_threads; i++) {
			an_io_thread_wakeup(&server->threads[i]);
		}
	}

	while (server->threads_finished < server->num_threads) {
		error = sem_trywait(&server->shutdown);
		if (error) {
			assert(errno == EAGAIN);
			return false;
		}

		server->threads_finished++;
	}

	return true;
}

/* Get a connection object from the free list. */
static struct an_io_connection *
an_io_connection_get(struct an_io_thread *iotd)
{
	struct an_io_connection *conn;

	conn = SLIST_FIRST(&iotd->free_conns);
	if (conn == NULL) {
		return NULL;
	}

	SLIST_REMOVE_HEAD(&iotd->free_conns, free_next);
	LIST_INSERT_HEAD(&iotd->idle_conns, conn, idle_next);
	return conn;
}

/* Reset a connection object so it can be reused. */
static void
an_io_connection_recycle(struct an_io_connection *conn)
{

	if (conn->state > HTTP_CONNECTION_IDLE) {
		an_rtbr_end(&conn->rtbr_section);
	}
	memset(&conn->request, 0, sizeof(conn->request));
	conn->request_start = 0;
	conn->timeout = 0;
	conn->inbuf = NULL;
	if (AN_CC_UNLIKELY(conn->outbuf != NULL &&
	    conn->outbuf->external_allocation)) {
		ck_pr_sub_64(&an_server_large_allocations,
		    malloc_usable_size(conn->outbuf->data));
		free(conn->outbuf->data);
		free(conn->outbuf);
	}
	conn->outbuf = NULL;
	http_parser_init(&conn->parser, HTTP_REQUEST);
}

/* Dispose of a request object by putting it back on the free list. */
static void
an_io_connection_put(struct an_io_connection *conn)
{
	struct an_io_thread *iotd;

	iotd = conn->iotd;

	assert(conn->state != HTTP_CONNECTION_PROCESSING);
	an_io_connection_recycle(conn);

	if (conn->state != HTTP_CONNECTION_FREE) {
		if (conn->state == HTTP_CONNECTION_IDLE) {
			LIST_REMOVE(conn, idle_next);
		} else {
			an_io_stat_dec(iotd, AN_IO_ACTIVE_CONNS);
			LIST_REMOVE(conn, active_next);
		}

		an_io_stat_dec(iotd, AN_IO_NUM_CONNS);
		AN_IO_CONNECTION_STATE(conn, HTTP_CONNECTION_FREE);
		SLIST_INSERT_HEAD(&iotd->free_conns, conn, free_next);
	}
}

/* Locate the I/O thread corresponding to a request ID. */
static inline struct an_io_thread *
an_io_thread_select(struct an_io_server *server, an_request_id_t rid)
{
	struct an_request_location loc;

	loc = an_request_id_decode(rid);
	if (AN_CC_UNLIKELY(loc.iotd_idx >= server->num_threads)) {
		an_syslog(LOG_CRIT, "Invalid request ID (%#"PRIx64"): "
		    "I/O thread index out of bounds (%u)", rid.id, loc.iotd_idx);
		return NULL;
	}

	return &server->threads[loc.iotd_idx];
}

static struct an_io_connection *
an_io_thread_connection_select(struct an_io_thread *iotd, an_request_id_t rid)
{
	struct an_request_location loc;
	struct an_io_connection *conn;

	loc = an_request_id_decode(rid);
	if (AN_CC_UNLIKELY(loc.conn_idx >= iotd->max_total_connections)) {
		an_syslog(LOG_CRIT, "Invalid request ID (%#"PRIx64"): "
		    "index out of bounds (%u)", rid.id, loc.conn_idx);
		return NULL;
	}

	conn = &iotd->connections[loc.conn_idx];
	if (AN_CC_UNLIKELY(conn->generation != loc.generation)) {
		an_syslog(LOG_CRIT, "Invalid request ID (%#"PRIx64"): "
		    "generation mismatch: %#"PRIx32" != %#"PRIx32, rid.id,
		    loc.generation, conn->generation);
		return NULL;
	}

	return conn;
}

/* Variant for when the owning I/O thread is not known. */
static inline struct an_io_connection *
an_io_connection_select(struct an_io_server *server, an_request_id_t id)
{
	struct an_io_thread *iotd;

	iotd = an_io_thread_select(server, id);
	if (AN_CC_UNLIKELY(iotd == NULL)) {
		return NULL;
	}

	return an_io_thread_connection_select(iotd, id);
}

/* Forcibly close a connection that may be in-flight. */
static void
an_io_connection_close(struct an_io_connection *conn)
{

	if (conn->state == HTTP_CONNECTION_CLOSING) {
		return;
	}

	an_io_deinit(conn->iotd, &conn->io);
	if (conn->state == HTTP_CONNECTION_PROCESSING) {
		/*
		 * The connection is currently being processed by a
		 * worker thread. We mark it as closing, and will
		 * recycle it once we have gotten a response.
		 */
		AN_IO_CONNECTION_STATE(conn, HTTP_CONNECTION_CLOSING);
		return;
	}
	an_io_connection_put(conn);
}

/*
 * Try to transition a connection to the active state.
 *
 * This function checks that we do not exceed the maximum number of
 * active connections, allocates a buffer for input, and transitions the
 * state from HTTP_CONNECTION_IDLE to HTTP_CONNECTION_READING.
 */
static bool
an_io_connection_preread(struct an_io_connection *conn)
{
	struct an_rtbr_timestamp ts;
	struct an_io_thread *iotd;
	struct an_io *io;
	uint64_t num_active;
	size_t need;
	int ret, ready_bytes;

	iotd = conn->iotd;
	io = &conn->io;
	if (conn->state == HTTP_CONNECTION_READING) {
		return true;
	}

	num_active = an_io_stat_get(iotd, AN_IO_ACTIVE_CONNS, false);
	if (num_active >= iotd->max_active_connections) {
		an_io_connection_close(conn);
		an_io_stat_inc(iotd, AN_IO_REFUSED_ACTIVE_CONNS);
		return false;
	}

	assert(conn->state == HTTP_CONNECTION_IDLE);
	assert(conn->inbuf == NULL);
	/*
	 * If we haven't allocated a buffer yet, make sure that there
	 * is data to be read before doing so. This is important as we
	 * call into this function optimistically whenever we might
	 * have bytes to read (right after accepting a connection but
	 * also after we are done replying to the previous request,
	 * in the case of persistent connections). If we always
	 * allocated buffers blindly, we would have to cycle through a
	 * lot of unused buffer space upon establishing connections,
	 * as we maintain a large number of connections but only a
	 * small subset of those is actually sending bytes to us.
	 */
	ret = ioctl(io->fd, FIONREAD, &ready_bytes);
	assert(ret == 0);
	if (ready_bytes == 0) {
		if (conn->remote_closed) {
			an_io_connection_close(conn);
			return false;
		}
		an_io_subscribe(iotd, io, EPOLLIN);
		return false;
	}
	need = next_power_of_2(ready_bytes);

	iotd = conn->iotd;

	ts = an_rtbr_prepare();
	conn->inbuf = an_pool_get(&input, need);
	if (AN_CC_UNLIKELY(conn->inbuf == NULL)) {
		an_syslog(LOG_CRIT, "Inqueue allocation failure, "
		    "failed to allocate %zu bytes.", need);
		an_io_connection_close(conn);
		an_io_stat_inc(iotd, AN_IO_OOM_FAILURES);
		return false;
	}

	an_rtbr_begin(&conn->rtbr_section, ts, "an_io_server");
	an_io_stat_inc(iotd, AN_IO_ACTIVE_CONNS);
	LIST_REMOVE(conn, idle_next);
	AN_IO_CONNECTION_STATE(conn, HTTP_CONNECTION_READING);
	conn->request_start = an_md_rdtsc();
	LIST_INSERT_HEAD(&iotd->active_conns, conn, active_next);
	return true;
}

/*
 * Try to read more data from this connection.
 *
 * This is called whenever epoll notifies us that there is data to be
 * read, but also right after we accept a new connection: in that case,
 * it is quite possible that there already is data to be read, so we
 * want to give it a try. If there actually was no data, we will just
 * register this socket for reads in epoll.
 */
static void
an_io_connection_read(struct an_io_connection *conn)
{
	struct an_io_server *server;
	struct an_io_thread *iotd;
	struct an_http_request *req;
	struct an_io *io;
	struct an_buffer *buf;
	ssize_t nbytes, nparsed, left;
	http_parser *parser;
	char *data;
	int ret;
	bool success;

	iotd = conn->iotd;
	server = iotd->server;
	io = &conn->io;

	if (an_io_connection_preread(conn) == false) {
		return;
	}

	assert(conn->state == HTTP_CONNECTION_READING);
	buf = conn->inbuf;
	assert(buf != NULL);

again:
	if (buf->in == buf->size) {
		success = an_pool_grow(buf);
		if (success == false) {
			an_syslog(LOG_CRIT, "Cannot grow buffer per policy. "
			    "Dropping request.");
			an_io_connection_close(conn);
			an_io_stat_inc(iotd, AN_IO_OOM_FAILURES);
			return;
		}
	}
	data = buf->data + buf->in;
	left = buf->size - buf->in;
	assert(left > 0);

	do {
		nbytes = read(io->fd, data, left);
	} while (nbytes == -1 && errno == EINTR);

	if (nbytes == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			an_io_subscribe(iotd, &conn->io, EPOLLIN);
			return;
		}

		if (errno != ECONNRESET) {
			an_syslog(LOG_CRIT, "Unexpected read error: %d (%s)",
			    errno, an_strerror(errno));
			an_io_stat_inc(iotd, AN_IO_READ_ERRORS);
		} else {
			an_io_stat_inc(iotd, AN_IO_RESET_BY_PEER);
		}

		an_io_connection_close(conn);
		return;
	}

	if (nbytes == 0) {
		/*
		 * An HTTP parser always knows whether it has fully read an
		 * HTTP request or not: if the request doesn't have a body,
		 * we know the message will be complete once we see the
		 * "\r\n" marker after headers; if it does have a body, we
		 * either got a Content-Length header and know the length
		 * of that body beforehand, or the body is sent using chunked
		 * transfer encoding, in which case we will know the message
		 * is complete when we get the terminating 0-length chunk.
		 *
		 * Long story short, if we get an EOF here, it means we have
		 * received a malformed request because otherwise, the HTTP
		 * parser would have already seen the end of the request and
		 * properly transitioned into the next state.
		 */
		an_io_connection_close(conn);
		an_io_stat_inc(iotd, AN_IO_MALFORMED_REQS);
		return;
	}

	buf->in += nbytes;

	parser = &conn->parser;
	nparsed = http_parser_execute(parser, &server->parser_settings,
	    data, nbytes);
	if (nparsed != nbytes || parser->upgrade) {
		if (parser->upgrade) {
			an_syslog(LOG_CRIT, "Invalid HTTP request: "
			    "unexpected upgrade request");
		} else {
			an_syslog(LOG_CRIT, "Invalid HTTP request: %s",
			    http_errno_description(parser->http_errno));
		}
		an_io_connection_close(conn);
		an_io_stat_inc(iotd, AN_IO_MALFORMED_REQS);
		return;
	}

	/*
	 * If the request has been read entirely, state transition will happen
	 * within the on_message_complete callback of the HTTP parser.
	 */
	if (conn->state == HTTP_CONNECTION_READING) {
		/* Not done; try to read some more bytes. */
		goto again;
	}

	assert(conn->state == HTTP_CONNECTION_PROCESSING);
	req = &conn->request;
	an_request_id_encode(&req->id, iotd, conn);
	req->buffer = buf->data;
	req->total_len = buf->in;

	ret = http_parser_parse_url(req->buffer + req->uri_offset,
	    req->uri_len, 0, &req->url);
	if (ret != 0 || (req->url.field_set & (1U << UF_PATH)) == 0) {
		an_syslog(LOG_CRIT, "Malformed request: Invalid URL: %.*s",
		    (int)req->uri_len, req->buffer + req->uri_offset);
		an_io_connection_close(conn);
		an_io_stat_inc(iotd, AN_IO_MALFORMED_REQS);
		return;
	}

	an_io_subscribe(iotd, &conn->io, 0);
	success = CK_RING_ENQUEUE_SPMC(requests, &iotd->requests_fifo,
	    iotd->requests_buffer, req);
	assert(success == true);
	an_io_stat_inc(iotd, AN_IO_NUM_REQUESTS);
}

/* The counterpart of an_io_connection_read(). */
static void
an_io_connection_write(struct an_io_connection *conn)
{
	struct an_io_thread *iotd;
	struct an_io *io;
	struct an_buffer *buf;
	ssize_t nbytes;

	iotd = conn->iotd;
	io = &conn->io;
	buf = conn->outbuf;

	assert(conn->state == HTTP_CONNECTION_WRITING);

again:
	do {
		nbytes = write(io->fd, buf->data + buf->out,
		    buf->in - buf->out);
	} while (nbytes == -1 && errno == EINTR);

	if (nbytes == -1) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			an_io_subscribe(conn->iotd,
			    &conn->io, EPOLLOUT);
			return;
		}

		if (errno != EPIPE) {
			an_syslog(LOG_CRIT, "Unexpected write error: %d (%s)",
			    errno, an_strerror(errno));
			an_io_stat_inc(iotd, AN_IO_WRITE_ERRORS);
		} else {
			an_io_stat_inc(iotd, AN_IO_RESET_BY_PEER);
		}

		an_io_connection_close(conn);
		return;
	}

	buf->out += nbytes;
	if (buf->out < buf->in) {
		/* Not done; try to write some more bytes. */
		goto again;
	}

	/* We're done writing the response. */
	if (conn->keepalive && !conn->remote_closed && !iotd->quiesce) {
		LIST_REMOVE(conn, active_next);
		an_io_stat_dec(iotd, AN_IO_ACTIVE_CONNS);
		an_io_connection_recycle(conn);
		AN_IO_CONNECTION_STATE(conn, HTTP_CONNECTION_IDLE);
		LIST_INSERT_HEAD(&iotd->idle_conns, conn, idle_next);
		an_io_connection_read(conn);
	} else {
		an_io_connection_close(conn);
	}
}

/* Accept a new connection from a listening file descriptor. */
static void
an_io_connection_accept(struct an_io_listener *listener)
{
	struct an_io_connection *conn;
	struct an_io_thread *iotd;
	struct an_io *io;
	int fd;

	iotd = listener->iotd;
	io = &listener->io;
	fd = accept4(io->fd, NULL, 0, SOCK_NONBLOCK | SOCK_CLOEXEC);
	if (fd == -1) {
		an_syslog(LOG_CRIT, "Failed to accept new connection: %d (%s)",
		    errno, an_strerror(errno));
		return;
	}

	conn = an_io_connection_get(iotd);
	if (conn == NULL) {
		/* Connection array is full. */
		an_syslog(LOG_CRIT,
		    "Connection array is full, dropping connection.");
		close(fd);
		an_io_stat_inc(iotd, AN_IO_REFUSED_CONNS);
		return;
	}

	assert(conn->state == HTTP_CONNECTION_FREE);
	conn->iotd = iotd;
	conn->listener = listener;
	conn->keepalive = true;
	conn->remote_closed = false;
	conn->generation = an_rand32() & RID_GEN_MASK;
	assert(conn->generation <= RID_GEN_MASK);
	AN_IO_CONNECTION_STATE(conn, HTTP_CONNECTION_IDLE);

	an_io_init(&conn->io, AN_IO_CONNECTION, fd);

	an_io_setopt(&conn->io, IPPROTO_TCP, TCP_NODELAY);
	an_io_setopt(&conn->io, IPPROTO_TCP, TCP_QUICKACK);

	http_parser_init(&conn->parser, HTTP_REQUEST);
	conn->parser.data = conn;

	an_io_stat_inc(iotd, AN_IO_NUM_CONNS);
	an_io_connection_read(conn);
}

/*
 * Returns the next request timeout in milliseconds, or -1.
 *
 * We just iterate over the active connections as we never have
 * a large enough number of those for it to matter.
 */
static int
an_io_thread_next_timeout(struct an_io_thread *iotd)
{
	struct an_io_connection *conn;
	uint64_t next, deadline, timeout, now, when;

	if (iotd->request_timeout == 0 || LIST_EMPTY(&iotd->active_conns)) {
		return -1;
	}

	now = an_md_rdtsc();

	next = 0;
	LIST_FOREACH(conn, &iotd->active_conns, active_next) {
		assert(conn->state > HTTP_CONNECTION_IDLE);

		if (conn->state == HTTP_CONNECTION_CLOSING) {
			continue;
		}

		timeout = ck_pr_load_64(&conn->timeout);
		if (AN_CC_LIKELY(timeout == 0)) {
			timeout = iotd->request_timeout;
		}
		deadline = conn->request_start + timeout;
		if (deadline <= now) {
			an_io_connection_close(conn);
			an_io_stat_inc(iotd, AN_IO_REQUEST_TIMEOUT);
			continue;
		}

		when = deadline - now;
		if (next == 0 || when < next) {
			next = when;
		}
	}

	if (next == 0) {
		return -1;
	}

	return an_md_rdtsc_scale(next) / 1000.0;
}

static void
an_io_thread_process_event(struct an_io_thread *iotd, struct epoll_event *event)
{
	struct an_io *io;
	struct an_io_listener *listener;
	struct an_io_connection *conn;
	eventfd_t val;
	int ret;

	io = event->data.ptr;

	assert((event->events & ~(io->events | EPOLLERR | EPOLLHUP)) == 0);

	if (event->events & EPOLLRDHUP) {
		/*
		 * Remote closed his writing side of the connection.
		 */
		assert(io->kind == AN_IO_CONNECTION);
		conn = AN_IO_PARENT(io, struct an_io_connection);

		/* We only listen for EPOLLRDHUP in those states. */
		assert(conn->state == HTTP_CONNECTION_IDLE ||
		    conn->state == HTTP_CONNECTION_READING);

		conn->remote_closed = true;
		an_io_connection_read(conn);
		return;
	}

	if (event->events & (EPOLLERR | EPOLLHUP)) {
		if (io->kind == AN_IO_CONNECTION) {
			conn = AN_IO_PARENT(io, struct an_io_connection);

			an_io_connection_close(conn);
			an_io_stat_inc(iotd, AN_IO_RESET_BY_PEER);
		} else {
			assert(io->kind == AN_IO_LISTENER);
			listener = AN_IO_PARENT(io, struct an_io_listener);

			an_io_connection_accept(listener);
		}
		return;
	}

	if (event->events & EPOLLIN) {
		/* We have data to read */
		if (io->kind == AN_IO_THREAD) {
			assert(iotd == AN_IO_PARENT(io, struct an_io_thread));
			assert(io == &iotd->io);
			/*
			 * Notification from worker threads letting us know
			 * that there are responses to grab from the FIFO.
			 * All we need to do is reading the eventfd value
			 * to clear the state as we have already processed
			 * the responses.
			 */
			ret = eventfd_read(io->fd, &val);
			assert(ret == 0);
		} else if (io->kind == AN_IO_LISTENER) {
			/* File descriptor belongs to a listener object */
			listener = AN_IO_PARENT(io, struct an_io_listener);

			an_io_connection_accept(listener);
		} else {
			/* File descriptor belongs to a connection object */
			assert(io->kind == AN_IO_CONNECTION);
			conn = AN_IO_PARENT(io, struct an_io_connection);
			assert(conn->state == HTTP_CONNECTION_IDLE ||
			    conn->state == HTTP_CONNECTION_READING);

			an_io_connection_read(conn);
		}
	} else if (event->events & EPOLLOUT) {
		/* We can write some data */
		assert(io->kind == AN_IO_CONNECTION);
		conn = AN_IO_PARENT(io, struct an_io_connection);
		assert(conn->state == HTTP_CONNECTION_WRITING);

		an_io_connection_write(conn);
	} else {
		an_syslog(LOG_CRIT, "Unexpected epoll event: %#x",
		    event->events);
	}
}

static void
an_io_thread_process_response(struct an_io_thread *iotd,
    struct an_http_response *resp)
{
	struct an_io_connection *conn;

	conn = an_io_thread_connection_select(iotd, resp->id);
	if (AN_CC_UNLIKELY(conn == NULL)) {
		return;
	}

	if (conn->state == HTTP_CONNECTION_CLOSING) {
		/*
		 * The socket has been forcibly closed asynchronously.
		 */
		an_io_connection_put(conn);
		return;
	}

	if (AN_CC_UNLIKELY(conn->state != HTTP_CONNECTION_PROCESSING)) {
		an_syslog(LOG_CRIT, "Invalid connection state %d, conn = %p, "
		    "response ID = %#"PRIx64, conn->state, conn, resp->id.id);
		return;
	}

	AN_IO_CONNECTION_STATE(conn, HTTP_CONNECTION_WRITING);

	if (resp->buf == NULL) {
		/*
		 * This signals us that the worker thread failed to allocate
		 * a buffer in the outqueue. We just drop the request.
		 */
		an_io_connection_close(conn);
		an_io_stat_inc(iotd, AN_IO_OOM_FAILURES);
		return;
	}

	conn->outbuf = resp->buf;
	an_io_connection_write(conn);
}

static void
an_io_thread_process_responses(struct an_io_thread *iotd)
{
	struct an_http_response *resp, *next, *head;

	resp = ck_pr_fas_ptr(&iotd->responses_head, NULL);

	/* Reverse the list to preserve FIFO ordering. */
	head = NULL;
	while (resp != NULL) {
		next = resp->next;
		resp->next = head;
		head = resp;
		resp = next;
	}

	resp = head;
	while (resp != NULL) {
		an_io_thread_process_response(iotd, resp);
		next = resp->next;
		an_free(an_http_response_token, resp);
		resp = next;
	}
}

static void
an_io_thread_quiesce(struct an_io_thread *iotd)
{
	struct an_io_listener *listener;
	struct an_io_connection *conn;
	size_t num_active_conns;

	if (iotd->quiesce) {
		return;
	}

	num_active_conns = an_io_stat_get(iotd, AN_IO_ACTIVE_CONNS, false);
	an_syslog(LOG_NOTICE, "Quiescing I/O thread #%d, %zu remaining "
	    "connection%s", (int)(iotd - iotd->server->threads),
	    num_active_conns, num_active_conns != 1 ? "s" : "");

	/*
	 * Set the quiesce flag so that active keepalive
	 * connections are closed upon completion.
	 */
	iotd->quiesce = true;

	/*
	 * Close all our listening sockets so that
	 * no new connections are accepted.
	 */
	AN_ARRAY_FOREACH_VAL(&iotd->listeners, listener) {
		an_io_deinit(iotd, &listener->io);
	}

	/* Now go over all idle connections and close them. */
	LIST_FOREACH(conn, &iotd->idle_conns, idle_next) {
		an_io_connection_close(conn);
	}
}

/* Main loop of the I/O threads. */
static void
an_io_thread_loop(struct an_io_thread *iotd)
{
	AN_ARRAY_INSTANCE(events_list) events[3];
	struct an_io_server *server;
	struct an_io_connection *conn;
	struct an_io *io;
	struct epoll_event *event;
	unsigned int i, nevents, n_jobs;
	int ret, timeout;

	server = iotd->server;

	for (;;) {
		an_io_thread_process_responses(iotd);

		assert(iotd->nevents > 0);
		do {
			timeout = an_io_thread_next_timeout(iotd);
			ret = epoll_wait(iotd->epollfd, iotd->events,
			    iotd->nevents, timeout);
		} while (ret == 0 || (ret == -1 && errno == EINTR));

		if (ret < 0) {
			an_syslog(LOG_CRIT, "Failed to wait on epoll fd: "
			    "%d (%s)", errno, an_strerror(errno));
			return;
		}

		nevents = (unsigned)ret;

		/*
		 * Sort our events by priority. We always want to prioritize
		 * doing progress on in-flight work versus accepting new work.
		 */
		for (i = 0; i < ARRAY_SIZE(events); i++) {
			AN_ARRAY_INIT(events_list, &events[i], 8);
		}
		for (i = 0; i < nevents; i++) {
			event = &iotd->events[i];
			io = event->data.ptr;

			if (io->kind == AN_IO_THREAD) {
				AN_ARRAY_PUSH(events_list, &events[0], &event);
				continue;
			}

			if (io->kind == AN_IO_LISTENER) {
				AN_ARRAY_PUSH(events_list, &events[2], &event);
				continue;
			}

			assert(io->kind == AN_IO_CONNECTION);
			conn = AN_IO_PARENT(io, struct an_io_connection);

			if (conn->state > HTTP_CONNECTION_IDLE) {
				AN_ARRAY_PUSH(events_list, &events[0], &event);
			} else {
				AN_ARRAY_PUSH(events_list, &events[1], &event);
			}
		}

		an_io_thread_process_responses(iotd);

		for (i = 0; i < ARRAY_SIZE(events); i++) {
			AN_ARRAY_FOREACH_VAL(&events[i], event) {
				an_io_thread_process_event(iotd, event);
			}
			AN_ARRAY_DEINIT(events_list, &events[i]);
		}

		/* Wake worker threads. */
		n_jobs = ck_ring_size(&iotd->requests_fifo);
		if (n_jobs > 0) {
			/*
			 * This is inherently racy as some requests might
			 * have already been popped off the queue, but we
			 * don't care if we wake up too many threads; it
			 * only matters that we wake up enough.
			 */
			ret = eventfd_write(server->workers_eventfd, n_jobs);
			assert(ret == 0);
		}

		if ((unsigned)nevents == iotd->nevents) {
			iotd->nevents *= 2;
			iotd->events = an_realloc_region(an_io_event_token,
			    iotd->events, nevents * sizeof(struct epoll_event),
			    iotd->nevents * sizeof(struct epoll_event));
		}

		if (ck_pr_load_8(&server->quiesce)) {
			an_io_thread_quiesce(iotd);
			if (LIST_EMPTY(&iotd->active_conns)) {
				an_syslog(LOG_NOTICE, "Finished draining all "
				    "active requests of I/O thread #%d",
				    (int)(iotd - iotd->server->threads));
				sem_post(&iotd->server->shutdown);
				/* We're done. */
				return;
			}
		}

		an_rtbr_poll(false);
	}
}

static void *
an_io_thread(void *arg)
{
	struct an_io_thread *iotd;
	cpu_set_t set;
	unsigned int n_cpu, i;
	int ret;

	iotd = arg;

	CPU_ZERO(&set);
	n_cpu = numa_num_configured_cpus();
	for (i = 0; i < n_cpu; i++) {
		CPU_SET(i, &set);
	}
	ret = sched_setaffinity(0, sizeof(set), &set);
	if (ret != 0) {
		an_syslog(LOG_CRIT, "Failed to set affinity for I/O thread: "
		    "%d (%s)", errno, an_strerror(errno));
	}

	sem_post(&iotd->server->startup);

	an_io_thread_loop(iotd);
	return NULL;
}

static void
an_io_thread_init(struct an_io_thread *iotd, struct an_io_server *server,
    struct an_server_config *config)
{
	unsigned int i, ring_buffer_size;
	int epollfd, evfd;

	epollfd = epoll_create1(EPOLL_CLOEXEC);
	if (epollfd == -1) {
		an_syslog(LOG_CRIT, "Failed to create epoll instance: %d (%s)",
		    errno, an_strerror(errno));
		abort();
	}

	/* For notifications from worker threads. */
	evfd = eventfd(0, EFD_CLOEXEC);
	if (evfd == -1) {
		an_syslog(LOG_CRIT, "Failed to create eventfd: %d (%s)",
		    errno, an_strerror(errno));
		close(epollfd);
		abort();
	}

	iotd->server = server;
	iotd->quiesce = false;
	iotd->max_total_connections = config->max_total_connections;
	iotd->max_active_connections = config->max_active_connections;
	if (config->request_timeout_ms <= 0) {
		iotd->request_timeout = 0;
	} else {
		iotd->request_timeout = an_md_us_to_rdtsc(config->request_timeout_ms * 1000);
	}

	/*
	 * We need max_active_connections + 1 entries in the ck_ring as you
	 * can only enqueue capacity - 1 elements at most. Also, the size must
	 * be a power of 2.
	 */
	ring_buffer_size = next_power_of_2(config->max_active_connections + 1);

	ck_ring_init(&iotd->requests_fifo, ring_buffer_size);
	iotd->requests_buffer = an_calloc_region(an_io_ring_buffer_token,
	    ring_buffer_size, sizeof(struct an_http_request));

	iotd->responses_head = NULL;

	iotd->connections = an_calloc_region(an_io_connection_token,
	    config->max_total_connections, sizeof(struct an_io_connection));

	/* Initialize the connections free list */
	SLIST_INIT(&iotd->free_conns);
	LIST_INIT(&iotd->idle_conns);
	LIST_INIT(&iotd->active_conns);
	for (i = config->max_total_connections; i > 0; i--) {
		SLIST_INSERT_HEAD(&iotd->free_conns,
		    &iotd->connections[i - 1], free_next);
	}

	iotd->epollfd = epollfd;
	iotd->nevents = INITIAL_NUM_EVENTS;
	iotd->events = an_calloc_region(an_io_event_token,
	    iotd->nevents, sizeof(struct epoll_event));
	AN_ARRAY_INIT(listener_list, &iotd->listeners, 8);

	/* Setup eventfd for notifications from worker threads */
	an_io_init(&iotd->io, AN_IO_THREAD, evfd);
	an_io_subscribe(iotd, &iotd->io, EPOLLIN);
}

static void
an_io_thread_deinit(struct an_io_thread *iotd)
{
	struct an_io_listener *listener;

	an_io_deinit(iotd, &iotd->io);
	close(iotd->epollfd);

	AN_ARRAY_FOREACH_VAL(&iotd->listeners, listener) {
		an_io_listener_destroy(listener);
	}
	AN_ARRAY_DEINIT(listener_list, &iotd->listeners);

	assert(LIST_EMPTY(&iotd->active_conns));
	assert(LIST_EMPTY(&iotd->idle_conns));

	an_free(an_io_event_token, iotd->events);
	an_free(an_io_connection_token, iotd->connections);
	an_free(an_io_ring_buffer_token, iotd->requests_buffer);
	assert(iotd->responses_head == NULL);
}

static void
an_io_thread_spawn(struct an_io_thread *iotd)
{
	int ret;

	ret = pthread_create(&iotd->thread, NULL, an_io_thread, iotd);
	assert(ret == 0);
}

/* Change the events we're subscribed to for this I/O object */
static void
an_io_subscribe(const struct an_io_thread *iotd, struct an_io *io, uint32_t events)
{
	struct epoll_event event;
	struct epoll_event *ev;
	int op, ret;

	if ((io->events & ~EPOLLRDHUP) == events) {
		/* Nothing to do. */
		return;
	}

	if (events == 0) {
		op = EPOLL_CTL_DEL;
		ev = NULL;
	} else {
		/*
		 * Always listen for EPOLLRDHUP events (half-close events)
		 * when we're waiting on reads.
		 *
		 * Not doing so leads to more complex code, as if the peer
		 * shuts down his write side of the connection and we don't
		 * listen for EPOLLRDHUP, then the only way for epoll to
		 * report that is through an EPOLLIN event, which will look
		 * strange as there actually aren't any bytes to read and
		 * ioctl(FIONREAD) would give us 0. Ultimately, we would
		 * fail to clear the event and it would keep firing over
		 * over again.
		 */
		if (events & EPOLLIN) {
			events |= EPOLLRDHUP;
		}
		ev = &event;
		ev->events = events;
		ev->data.ptr = io;
		if (io->events == 0) {
			op = EPOLL_CTL_ADD;
		} else {
			op = EPOLL_CTL_MOD;
		}
	}

	ret = epoll_ctl(iotd->epollfd, op, io->fd, ev);
	assert(ret == 0);

	io->events = events;
}

struct an_io_server *
an_io_server_create(struct an_server_config *config)
{
	struct an_server_config_listener *listener;
	config_cb_t conf_cb;
	int evfd;
	unsigned int i;

	assert(server == NULL);
	assert(config->max_active_connections <= config->max_total_connections);

	if (config->max_total_connections > AN_IO_CONNECTIONS_MAX) {
		return NULL;
	}

	if (config->num_threads > AN_IO_THREADS_MAX) {
		return NULL;
	}

	/* For notifications to the worker threads. */
	evfd = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE | EFD_NONBLOCK);
	if (evfd == -1) {
		an_syslog(LOG_CRIT, "Failed to create eventfd: %d (%s)",
		    errno, an_strerror(errno));
		return NULL;
	}

	server = an_calloc_object(an_io_server_token);
	server->ready = false;
	server->quiesce = false;
	server->workers_eventfd = evfd;
	server->max_response_size = config->max_response_size;

	/* Initialize parser settings with our callbacks */
	http_parser_settings_init(&server->parser_settings);
	server->parser_settings.on_url = on_url;
	server->parser_settings.on_body = on_body;
	server->parser_settings.on_message_complete = on_message_complete;

#ifdef AN_IO_DEBUG
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "/tmp/bidder-%u.trc", getpid());
	server->tracefd = open(path,
	    O_CREAT | O_TRUNC | O_APPEND | O_WRONLY | O_CLOEXEC, 0644);
	if (server->tracefd < 0) {
		an_syslog(LOG_CRIT, "Failed to open trace file: %s",
		    an_strerror(errno));
		abort();
	}
#endif

	server->num_threads = config->num_threads;
	server->threads = an_calloc_region(an_io_thread_token,
	    config->num_threads, sizeof(struct an_io_thread));
	for (i = 0; i < config->num_threads; i++) {
		an_io_thread_init(&server->threads[i], server, config);
	}

	AN_ARRAY_FOREACH(&config->listeners, listener) {
		an_io_server_listen(server, listener->host, listener->port);
	}

	memset(&conf_cb, 0, sizeof(config_cb_t));
	conf_cb.stat_print_cb = an_io_server_stats;
	add_server_config_handler("an_io_server", &conf_cb);
	return server;
}

int
an_io_server_destroy(struct an_io_server *server)
{
	struct an_io_thread *iotd;
	unsigned int i;

	if (!server->quiesce || !an_io_server_quiesce(server)) {
		an_syslog(LOG_CRIT, "Cannot destroy an_server instance %p: "
		    "server is still busy", server);
		errno = EAGAIN;
		return -1;
	}

	sem_destroy(&server->shutdown);

	close(server->workers_eventfd);

	for (i = 0; i < server->num_threads; i++) {
		iotd = &server->threads[i];
		pthread_join(iotd->thread, NULL);
		an_io_thread_deinit(iotd);
	}
	an_free(an_io_thread_token, server->threads);

#ifdef AN_IO_DEBUG
	close(server->tracefd);
#endif

	an_free(an_io_server_token, server);
	return 0;
}

void
an_io_server_start(struct an_io_server *server)
{
	unsigned int i;
	int error;

	error = sem_init(&server->startup, 0, 0);
	assert(!error);
	error = sem_init(&server->shutdown, 0, 0);
	assert(!error);

	for (i = 0; i < server->num_threads; i++) {
		an_io_thread_spawn(&server->threads[i]);
	}

	for (i = 0; i < server->num_threads; i++) {
		do {
			error = sem_wait(&server->startup);
		} while (error == -1 && errno == EINTR);
		assert(error == 0);
	}

	server->ready = true;

	sem_destroy(&server->startup);
}

bool
an_io_server_ready(const struct an_io_server *server)
{

	return server->ready;
}

/* Worker threads API */

/*
 * Return the notification eventfd for worker threads.
 *
 * Useful when you cannot use an_io_server_read() because
 * you need to wait on other events as well.
 */
int
an_io_server_notify_fd(struct an_io_server *server)
{

	return server->workers_eventfd;
}

/* Attempt to steal a request off the FIFO. */
int
an_io_server_tryread(struct an_io_server *server, struct an_http_request *req)
{
	struct an_io_thread *iotd;
	bool success;
	unsigned int i, start;

	start = an_random_below(server->num_threads);
	i = start;
	do {
		iotd = &server->threads[i];
		success = CK_RING_DEQUEUE_SPMC(requests, &iotd->requests_fifo,
		    iotd->requests_buffer, req);
		if (success == true) {
			return 0;
		}
		i = (i + 1) % server->num_threads;
	} while (i != start);

	return -1;
}

void
an_io_server_read(struct an_io_server *server, struct an_http_request *req)
{
	eventfd_t value;
	int ret;

	for (;;) {
		/* We always try to grab items before blocking on eventfd. */
		ret = an_io_server_tryread(server, req);
		if (ret == 0) {
			return;
		}

		ret = eventfd_read(server->workers_eventfd, &value);
		assert(ret == 0);
	}
}

static void
an_io_thread_push_response(struct an_io_thread *iotd,
    struct an_http_response *resp)
{
	struct an_http_response *orig;

	/*
	 * First, we try an optimistic "blind" CAS assuming that the stack
	 * is empty, in order to avoid acquiring the cache line for reads,
	 * only to acquire it for writes shortly after.
	 */
	resp->next = NULL;
	ck_pr_fence_store();
	if (ck_pr_cas_ptr_value(&iotd->responses_head, NULL, resp, &orig)) {
		return;
	}

	do {
		resp->next = orig;
		ck_pr_fence_store();
	} while (!ck_pr_cas_ptr_value(&iotd->responses_head,
	    resp->next, resp, &orig));
}

static void
an_io_thread_send(struct an_io_thread *iotd, an_request_id_t id,
    struct an_buffer *buf)
{
	struct an_http_response *resp;

	resp = an_calloc_object(an_http_response_token);
	resp->id = id;
	resp->buf = buf;

	an_io_thread_push_response(iotd, resp);

	/* Notify the I/O thread. */
	an_io_thread_wakeup(iotd);
}

struct an_buffer *
an_io_get_outbuf(struct an_io_server *server, an_request_id_t id, size_t want)
{
	struct an_buffer *buf;

	if (AN_CC_UNLIKELY(want > server->max_response_size)) {
		goto fail;
	}

	if (AN_CC_UNLIKELY(want > BUMP_SIZE)) {
		if (AN_CC_UNLIKELY(ck_pr_load_64(&an_server_large_allocations) >
		    TOTAL_LARGE_ALLOCATION_LIMIT)) {
			goto fail;
		}

		buf = calloc(1, sizeof(struct an_buffer));
		if (AN_CC_UNLIKELY(buf == NULL)) {
			goto fail;
		}
		buf->data = malloc(want);
		if (AN_CC_UNLIKELY(buf->data == NULL)) {
			free(buf);
			goto fail;
		}
		buf->in = 0;
		buf->out = 0;
		buf->size = want;
		buf->external_allocation = true;
		ck_pr_add_64(&an_server_large_allocations,
		    malloc_usable_size(buf->data));
		return buf;
	}

	buf = an_pool_get(&output, want);
	if (AN_CC_UNLIKELY(buf == NULL)) {
		goto fail;
	}

	return buf;

fail:
	an_syslog(LOG_CRIT, "Outqueue allocation failure, "
	    "failed to allocate %zu bytes.", want);
	return NULL;
}

void
an_io_server_write(struct an_io_server *server, an_request_id_t id,
    struct an_buffer *buf)
{
	struct an_io_thread *iotd;

	iotd = an_io_thread_select(server, id);
	if (AN_CC_LIKELY(iotd != NULL)) {
		an_io_thread_send(iotd, id, buf);
	}

	an_rtbr_poll(false);
}

int
an_io_set_deadline(an_request_id_t id, unsigned int deadline_us)
{
	struct an_io_server *server;
	struct an_io_connection *conn;
	uint64_t timeout;

	server = server_config->server;
	conn = an_io_connection_select(server, id);
	if (conn == NULL) {
		return -1;
	}

	if (conn->state != HTTP_CONNECTION_PROCESSING) {
		return -1;
	}

	timeout = an_md_us_to_rdtsc(deadline_us);
	ck_pr_store_64(&conn->timeout, timeout);
	return 0;
}

int
an_io_get_tcp_info(an_request_id_t id, struct tcp_info *tcpi)
{
	struct an_io_server *server;
	struct an_io_connection *conn;
	socklen_t len;
	int ret;

	server = server_config->server;
	conn = an_io_connection_select(server, id);
	if (conn == NULL) {
		return -1;
	}

	if (conn->state != HTTP_CONNECTION_PROCESSING) {
		return -1;
	}

	len = sizeof(*tcpi);
	ret = getsockopt(conn->io.fd, IPPROTO_TCP, TCP_INFO, tcpi, &len);
	return ret;
}
