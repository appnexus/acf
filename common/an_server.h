#ifndef _COMMON_AN_SERVER_H
#define _COMMON_AN_SERVER_H

#include <netinet/in.h>
#include <stdint.h>

#include "common/net/protocol/http-parser/http_parser.h"
#include "common/an_array.h"

#define AN_IO_CONNECTIONS_MAX	(1U << 28)
#define AN_IO_THREADS_MAX	(1U << 8)

/* Server configuration */
struct an_server_config_listener {
	char *host;
	in_port_t port;
};
AN_ARRAY(an_server_config_listener, an_server_config_listener);

struct an_server_config {
	uint32_t max_active_connections;
	uint32_t max_total_connections;
	size_t max_response_size;
	unsigned int num_threads;
	int request_timeout_ms;
	AN_ARRAY_INSTANCE(an_server_config_listener) listeners;
};

/*
 * Internal HTTP buffers. Do not use directly; wrap it
 * in an an_wbuf using an_buf_http_wrap() instead.
 */
struct an_buffer {
	char *data;
	size_t size;
	size_t in;	/* Offset for reads (from socket to buffer) */
	size_t out;	/* Offset for writes (from buffer to socket) */
	bool external_allocation;	/* Allocated using plain malloc() */
};

struct tcp_info;

typedef struct {
	uint64_t id;
} an_request_id_t;

struct an_http_request {
	an_request_id_t id;
	const char *buffer;	/* The full HTTP request buffer */
	struct http_parser_url url;
	uint32_t total_len;
	uint32_t uri_offset;
	uint32_t uri_len;
	uint32_t body_offset;
	uint32_t body_len;
};

struct an_io_server;

/**
 * Create a server supporting at most @a max_total_connections concurrent
 * connections, with at most @a max_active_connections active ones, and
 * allocating at most @a max_bytes for input and output buffers, and using
 * @a num_io_threads I/O threads. An active connection is defined as a
 * connection we are currently reading bytes from, or a connection we
 * already read a request from and that is still waiting for our response.
 *
 * All limits are per-I/O thread rather than global limits.
 */
struct an_io_server *an_io_server_create(struct an_server_config *);

/*
 * Instruct the server to listen on the specified address and port.
 * If the host parameter is NULL, listen on all available addresses.
 *
 * This needs to be called before an_io_server_start().
 */
int an_io_server_listen(struct an_io_server *, const char *, in_port_t);

/* Spawn the I/O threads and start processing requests. */
void an_io_server_start(struct an_io_server *);

/*
 * Check whether the server is ready. It is always ready after
 * an_io_server_start() returns, this function is only useful for
 * code that does not know whether that has happened yet.
 */
bool an_io_server_ready(const struct an_io_server *);

/*
 * Stop accepting new requests for graceful shutdown. This function
 * needs to be called repeatedly until it returns true, signaling us
 * that every in-flight request has been processed.
 */
bool an_io_server_quiesce(struct an_io_server *);

/*
 * Destroy the server. This can only be called once the server has
 * been successfully quiesced, that is after an_io_server_quiesce()
 * has returned true. If that pre-condition hasn't been respected,
 * this function will fail, returning -1 and setting errno to EAGAIN.
 * Otherwise, it will return 0 and release all resources associated
 * with this an_server instance.
 */
int an_io_server_destroy(struct an_io_server *);

/* API for worker threads. */
int an_io_server_notify_fd(struct an_io_server *);
void an_io_server_read(struct an_io_server *, struct an_http_request *);
int an_io_server_tryread(struct an_io_server *, struct an_http_request *);

/* Low-level API for handlers, use an_http instead. */
struct an_buffer *an_io_get_outbuf(struct an_io_server *,
    an_request_id_t, size_t);
void an_io_server_write(struct an_io_server *, an_request_id_t,
    struct an_buffer *);

/* API for handlers. */
int an_io_set_deadline(an_request_id_t, unsigned int);
int an_io_get_tcp_info(an_request_id_t, struct tcp_info *);

#endif /* _COMMON_AN_SERVER_H */
