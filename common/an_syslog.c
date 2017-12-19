#include <ck_pr.h>
#include <ck_ring.h>
#include <execinfo.h>
#include <pthread.h>

#include "common/an_array.h"
#include "common/an_cc.h"
#include "common/an_malloc.h"
#include "common/an_ring.h"
#include "common/an_stat.h"
#include "common/an_syslog.h"
#include "common/an_thread.h"
#include "common/an_time.h"
#include "common/server_config.h"
#include "common/util.h"

/* The length of the pointer array `buffer' in our syslog work ring */
#define SYSLOG_WORK_RING_BUFFER_LENGTH (2 * 1024)

/*
 * The number of work items in our local object cache
 *
 * This needs to be at least as large as the length of the ring buffer,
 * plus one to account for a single `waiting' enqueue behind a completely
 * full queue of N already-allocated work items.
 */
#define SYSLOG_WORK_CACHE_SIZE (SYSLOG_WORK_RING_BUFFER_LENGTH + 1)

/*
 * The number of buckets in the time hash. Bigger means less hash
 * collisions and so fewer false-positives when throttling.
 */
#define SYSLOG_TIME_HASH_SIZE (8 * 1024)

/*
 * Each syslog write can be at most this many bytes. This number is chosen
 * because it's the max size syslog will guarantee it can write at once.
 *
 * We subtract the size of an int here so that the syslog work struct is
 * a power of two.
 */
#define MSG_MAX_SIZE (1024 - sizeof(int))

/* The maximum amount of time the consumer thread should wait for new work. */
#define SYSLOG_CONSUMER_SLEEP_MS 10UL

/* Allocates a generous number of characters for each line of the backtrace */
#define AN_SYSLOG_BACKTRACE_BUFFER_LEN (AN_THREAD_BACKTRACE_LIMIT * 200)

enum syslog_stat_type {
	/* One type per priority (aka type, aka severity) */
	SYSLOG_STAT_DEBUG = 0,
	SYSLOG_STAT_INFO,
	SYSLOG_STAT_NOTICE,
	SYSLOG_STAT_WARNING,
	SYSLOG_STAT_ERR,
	SYSLOG_STAT_CRIT,
	SYSLOG_STAT_ALERT,
	SYSLOG_STAT_EMERG,

	/* Represents a throttled syslog message (too recently written) */
	SYSLOG_STAT_THROTTLED,

	/* Represents a dropped syslog message (producer pressure) */
	SYSLOG_STAT_DROPPED,

	SYSLOG_STAT_MAX
};

static const char *syslog_stat_names[] = {
	[SYSLOG_STAT_DEBUG]	= "syslog.debug_sum",
	[SYSLOG_STAT_INFO]	= "syslog.info_sum",
	[SYSLOG_STAT_NOTICE]	= "syslog.notice_sum",
	[SYSLOG_STAT_WARNING]	= "syslog.warning_sum",
	[SYSLOG_STAT_ERR]	= "syslog.error_sum",
	[SYSLOG_STAT_CRIT]	= "syslog.critical_sum",
	[SYSLOG_STAT_ALERT]	= "syslog.alert_sum",
	[SYSLOG_STAT_EMERG]	= "syslog.emerg_sum",
	[SYSLOG_STAT_THROTTLED]	= "syslog.throttled_sum",
	[SYSLOG_STAT_DROPPED]	= "syslog.dropped_sum"
};

static an_stat_t *syslog_stats = NULL;

struct syslog_work {
	int priority;
	char msg[MSG_MAX_SIZE];
};

struct syslog_work_buffer {
	struct syslog_work buffer[SYSLOG_WORK_RING_BUFFER_LENGTH];
};

/*
 * One hash per thread, each mapping syslog messages to the last
 * time they were written. Used to throttle single-message spam.
 */
static time_t time_hashes[AN_THREAD_LIMIT][SYSLOG_TIME_HASH_SIZE];
static __thread unsigned long time_hash_seed;

/*
 * Per-thread work ring of pending syslog writes.
 *
 * This is the shared work ring between producer (this thread) and consumer.
 */
struct syslog_work_ring {
	struct ck_ring ring;
	struct syslog_work_buffer buffer;
};

/* ck_ring depends on the work ring buffer length to be a power of two */
#define IS_POWER_OF_TWO(x) (((x) & ((x) - 1)) == 0)
_Static_assert(IS_POWER_OF_TWO(SYSLOG_WORK_RING_BUFFER_LENGTH), "syslog ring buffer length must be a power of two");
_Static_assert(IS_POWER_OF_TWO(sizeof(struct syslog_work)), "syslog work size must be a power of two");
_Static_assert(IS_POWER_OF_TWO(sizeof(struct syslog_work_buffer)), "syslog work buffer size must be a power of two");

/* Global array of registered thread-local work rings. */
static struct syslog_work_ring all_work_rings[AN_THREAD_LIMIT];

/* Non-zero if a consumer thread is active. */
static unsigned int consumer_thread_active;

/* Non-zero if a consumer thread should stop work and return .*/
static unsigned int consumer_thread_shutdown;

/* The single consumer thread. */
static pthread_t consumer_thread;

/* Global array of thread-local backtrace message buffers. */
static char bt_message[AN_THREAD_LIMIT][AN_SYSLOG_BACKTRACE_BUFFER_LEN];

static void
syslog_stat_inc(enum syslog_stat_type type)
{

	if (AN_CC_LIKELY(current != NULL)) {
		an_stat_inc_uint64(syslog_stats, type);
	}

	return;
}

static void
syslog_stat_inc_priority(int priority)
{

	switch (LOG_PRI(priority)) {
	case LOG_DEBUG:
		syslog_stat_inc(SYSLOG_STAT_DEBUG);
		break;

	case LOG_INFO:
		syslog_stat_inc(SYSLOG_STAT_INFO);
		break;

	case LOG_NOTICE:
		syslog_stat_inc(SYSLOG_STAT_NOTICE);
		break;

	case LOG_WARNING:
		syslog_stat_inc(SYSLOG_STAT_WARNING);
		break;

	case LOG_ERR:
		syslog_stat_inc(SYSLOG_STAT_ERR);
		break;

	case LOG_CRIT:
		syslog_stat_inc(SYSLOG_STAT_CRIT);
		break;

	case LOG_ALERT:
		syslog_stat_inc(SYSLOG_STAT_ALERT);
		break;

	case LOG_EMERG:
		syslog_stat_inc(SYSLOG_STAT_EMERG);
		break;
	}

	return;
}

static void
syslog_stat_clear(void)
{

	if (syslog_stats == NULL) {
		return;
	}

	for (size_t i = 0 ; i < SYSLOG_STAT_MAX; i++) {
		/* When the last parameter is true, each value gets reset. Magic. */
		an_stat_sum_uint64(syslog_stats, i, true);
	}

	return;
}

static void
syslog_stat_print(struct evbuffer *buf, double elapsed)
{

	if (syslog_stats == NULL) {
		return;
	}

	for (size_t i = 0 ; i < SYSLOG_STAT_MAX; i++) {
		if (i >= (size_t)SYSLOG_STAT_THROTTLED) {
			evbuffer_add_printf(buf, "%s: %lu\n", syslog_stat_names[i],
			    an_stat_sum_uint64(syslog_stats, i, false));
		} else {
			evbuffer_add_printf(buf, "%s: %.2f\n", syslog_stat_names[i],
			    (float)an_stat_sum_uint64(syslog_stats, i, false) / elapsed);
		}
	}

	return;
}

static bool
consume_syslog_work_from_ring(struct syslog_work_ring *ring)
{
	struct syslog_work *work;

	work = an_ring_dequeue_spsc_prepare(&ring->ring, &ring->buffer, sizeof(struct syslog_work));
	if (work == NULL) {
		return false;
	}

	syslog(work->priority, "%s", work->msg);
	an_ring_dequeue_spsc_commit(&ring->ring);

	return true;
}

static void
consume_syslog_work(void)
{
	bool did_work;

	do {
		did_work = false;
		for (size_t i = 0; i < ARRAY_SIZE(all_work_rings); i++) {
			if (consume_syslog_work_from_ring(&all_work_rings[i]) == true) {
				did_work = true;
			}
		}
		/* Keep working while at least one syslog ring had work to do. */
	} while (did_work == true);

	return;
}

static void *
consumer_thread_work(void *args)
{
	int ret;
	unsigned int shutdown;

	ret = an_thread_affine_low_priority();
	if (ret != 0) {
		an_syslog(LOG_CRIT,
		    "Could not set consumer thread affinity. Continuing anyway.");
	}

	ck_pr_store_uint(&consumer_thread_active, 1);

	do {
		usleep(SYSLOG_CONSUMER_SLEEP_MS * 1000UL);

		/*
		 * Read the shutdown bit before consuming work, since we still
		 * want to finish up pending work even if shutdown != 0.
		 */
		shutdown = ck_pr_load_uint(&consumer_thread_shutdown);

		/* Consume as much work from the syslog work rings as possible. */
		consume_syslog_work();
	} while (shutdown == 0);

	ck_pr_store_uint(&consumer_thread_active, 0);

	return NULL;
}

static void
syslog_consumer_join(void)
{

	if (ck_pr_load_uint(&consumer_thread_active) == 1) {
		ck_pr_store_uint(&consumer_thread_shutdown, 1);
		pthread_join(consumer_thread, NULL);
	}

	return;
}

void
an_syslog_init(void)
{
	config_cb_t conf;

	AN_BLOCK_EXECUTE_ONCE_GUARD;

	memset(&conf, 0, sizeof(conf));
	conf.print_stats_cb = syslog_stat_print;
	conf.clear_stats_cb = syslog_stat_clear;
	add_server_config_handler("an_syslog", &conf);

	/*
	 * This allows early exit() calls to join with the syslog thread
	 * first, so that pending syslog writes are flushed before exit.
	 */
	atexit(syslog_consumer_join);

	return;
}

void
an_syslog_deinit(void)
{

	AN_BLOCK_EXECUTE_ONCE_GUARD;

	syslog_consumer_join();
	an_stat_destroy(syslog_stats);

	return;
}

int
an_syslog_init_consumer(unsigned int num_producers)
{
	int ret;

	if (server_config->use_an_syslog == false) {
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(all_work_rings); i++) {
		ck_ring_init(&all_work_rings[i].ring, SYSLOG_WORK_RING_BUFFER_LENGTH);
	}

	/* allow for as many an_stat slots as potential an_syslog slots */
	syslog_stats = an_stat_create_explicit(AN_THREAD_LIMIT, SYSLOG_STAT_MAX, 0, 0);

	ck_pr_store_uint(&consumer_thread_shutdown, 0);
	ret = pthread_create(&consumer_thread, NULL, &consumer_thread_work, 0);

	return ret;
}

void
an_syslog_register_producer(void)
{

	time_hash_seed = an_rand64();

	return;
}

void
an_syslog_openlog(const char *ident, int option, int facility)
{

	openlog(ident, option, facility);

	return;
}

void
an_syslog_closelog(void)
{

	closelog();

	return;
}

void
an_syslog(int priority, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	an_vsyslog(priority, format, args);
	va_end(args);

	return;
}

/*
 * Return true if the time hash says the given message was written too
 * recently and should be throttled.
 */
static bool
check_and_update_time_hash(const char *msg)
{
	time_t now, last;
	unsigned long idx;
	time_t *time_hash;

	now = an_time(true);
	idx = MurmurHash64A(msg, strlen(msg), time_hash_seed) % SYSLOG_TIME_HASH_SIZE;
	time_hash = time_hashes[current->id];
	last = time_hash[idx];

	if (now < last + server_config->syslog_interval) {
		return true;
	}

	time_hash[idx] = now;

	return false;

}

void
an_vsyslog(int priority, const char *format, va_list args)
{
	struct syslog_work *work;
	struct syslog_work_ring *ring;

	/*
	 * Skip writing to the work queue if we are in startup, fancy
	 * syslog is explicitly disabled, or there is not yet a consumer.
	 */
	if (current == NULL ||
	    (server_config != NULL && server_status(NULL) != 1) ||
	    ck_pr_load_uint(&consumer_thread_active) == 0 ||
	    server_config->use_an_syslog == false) {
		vsyslog(priority, format, args);
		return;
	}

	ring = &all_work_rings[current->id];
	work = an_ring_enqueue_spsc_prepare(&ring->ring, &ring->buffer,
	    sizeof(struct syslog_work));
	if (work == NULL) {
		/* Drop syslog writes when the consumer is under pressure. */
		syslog_stat_inc(SYSLOG_STAT_DROPPED);
		return;
	}


	work->priority = priority;
	vsnprintf(work->msg, MSG_MAX_SIZE, format, args);

	syslog_stat_inc_priority(work->priority);

	if (check_and_update_time_hash(work->msg) == true) {
		syslog_stat_inc(SYSLOG_STAT_THROTTLED);
		return;
	}

	an_ring_enqueue_spsc_commit(&ring->ring);

	return;
}

void
an_syslog_backtrace_internal(int priority, const char *format, ...)
{
	va_list args;
	int bt_message_len;
	char **symbols;
	void *buffer[AN_THREAD_BACKTRACE_LIMIT];
	int thread = current->id;

	va_start(args, format);
	bt_message_len = vsnprintf(bt_message[thread], AN_SYSLOG_BACKTRACE_BUFFER_LEN, format, args);
	va_end(args);

	if (bt_message_len < 0) {
		an_syslog(LOG_WARNING, "%s: format message failed to write", __func__);
		return;
	}

	size_t buffer_size = (size_t) backtrace(buffer, ARRAY_SIZE(buffer));
	symbols = backtrace_symbols(buffer, buffer_size);

	for (size_t i = 0; i < buffer_size; i++) {
		int bt_line_len = snprintf(&bt_message[thread][bt_message_len],
		    AN_SYSLOG_BACKTRACE_BUFFER_LEN - bt_message_len,
		    "Thread %4d: %zd\t%s\n", (current == NULL ? -1 : thread), i, symbols[i]);
		if (bt_line_len < 0) {
			an_syslog(LOG_WARNING, "%s: function calls failed to write", __func__);
			return;
		}
		bt_message_len += bt_line_len;
	}

	an_syslog(priority, bt_message[thread], NULL);

	free(symbols);

	return;
}
