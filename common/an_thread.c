#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "autoconf_globals.h"
#include "common/an_cc.h"
#include "common/an_handler.h"
#include "common/an_latency.h"
#include "common/an_malloc.h"
#include "common/an_smr.h"
#include "common/an_sstm.h"
#include "common/an_syslog.h"
#include "common/an_thread.h"
#include "common/libevent_extras.h"
#include "common/server_config.h"

#include <assert.h>
#include <ck_pr.h>
#include <ck_spinlock.h>
#include <ck_stack.h>
#include <errno.h>
#include <event.h>
#include <fcntl.h>
#include <inttypes.h>
#include <numa.h>
#include <signal.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/queue.h>

#ifdef HAVE_LIBGIMLI
#include <libgimli.h>
#endif

#include <event2/listener.h>
#include <event2/http.h>

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

/* glibc extension, only used for backtrace on abnormal exits. */
#include <execinfo.h>

#define NUMA_MODE_DEFAULT 0
/* Local: shared allocation go on last socket, rest is local. */
#define NUMA_MODE_LOCAL 1
/* Interleave: shared allocations are interleaved, rest is local. */
#define NUMA_MODE_INTERLEAVE 2

#if defined(IS_BIDDER)
#define NUMA_MODE NUMA_MODE_LOCAL
#endif

#if defined(IS_IMPBUS) && !defined(IS_COOKIEMONSTER)
#define NUMA_MODE NUMA_MODE_INTERLEAVE
#endif

#if defined(IS_COOKIEMONSTER)
#define NUMA_MODE NUMA_MODE_INTERLEAVE
#endif

#ifndef NUMA_MODE
#define NUMA_MODE NUMA_MODE_DEFAULT
#endif

static bool disable_numa = false;

static int an_thread_numa_available(void);

static double anon_ratio = 0;

#ifndef AN_THREAD_MEMORY_THRESHOLD_CRITICAL
#define AN_THREAD_MEMORY_THRESHOLD_CRITICAL (0.92)
#endif

static AN_MALLOC_DEFINE(fifo_token,
    .string = "an_thread_fifo",
    .mode   = AN_MEMORY_MODE_FIXED,
    .size   = sizeof(ck_fifo_spsc_entry_t));

struct an_thread_block {
	an_thread_broadcast_execute_fn_t *e;
	void *closure;
};

static AN_MALLOC_DEFINE(block_token,
    .string = "an_thread_block",
    .mode   = AN_MEMORY_MODE_FIXED,
    .size   = sizeof(struct an_thread_block));

static AN_MALLOC_DEFINE(token,
    .string = "an_thread",
    .mode = AN_MEMORY_MODE_FIXED,
    .size = sizeof(struct an_thread));

static AN_MALLOC_DEFINE(cleanup_token,
    .string = "an_thread_cleanup",
    .mode = AN_MEMORY_MODE_VARIABLE);

#ifdef HAVE_LIBGIMLI
static volatile struct gimli_heartbeat *hb;
static bool gimli_is_enabled = true;

void
an_thread_gimli_init(bool enable)
{
	static bool inited = false;

	assert(!inited);
	inited = true;
	gimli_is_enabled = enable;
	an_syslog(LOG_INFO, "gimli component %s...", enable ? "enabled" : "disabled");

	return;
}

bool
an_thread_gimli_is_enabled()
{

	return gimli_is_enabled;
}
#endif

LIST_HEAD(an_thread_list, an_thread) threads;
static ck_spinlock_fas_t threads_mtx = CK_SPINLOCK_FAS_INITIALIZER;
static unsigned int an_thread_id;
static an_thread_key_t an_thread_key;
an_smr_t global_smr;
__thread struct an_thread *current;
__thread unsigned int an_thread_current_id = -1U;
__thread bool an_thread_current_is_preferred_worker = false;

#ifdef __linux__
#ifndef gettid
static pid_t
gettid(void)
{

	return syscall(__NR_gettid);
}
#endif /* gettid */
#endif /* __linux__ */

static bool an_thread_tls_destructor_key_created;
static pthread_key_t an_thread_tls_destructor_key;

struct an_thread_tls_destructor_stack_entry {
	void (*destructor)(void*);
	an_thread_key_t key;
	ck_stack_entry_t next_dtor;
};
CK_STACK_CONTAINER(struct an_thread_tls_destructor_stack_entry, next_dtor, an_thread_dtor_container);

/**
 * Global counters (atomically incremented) of consecutive and total
 * errors we tried to recover from.
 */
static unsigned int consecutive_soft_errors_watermark = 0;
static unsigned int consecutive_soft_errors = 0;
static unsigned int total_soft_errors = 0;

/**
 * Global limit for soft errors without any intervening successful
 * transaction and for the total lifetime number of soft errors.
 *
 * When either limit is reached, the handler passes the buck back to
 * gimli.
 *
 * Soft error handling is disabled by default.
 */
static unsigned int max_consecutive_soft_errors = 0;
static unsigned int max_total_soft_errors = 0;

static uint64_t io_numa_nodes = 0;

static void
an_thread_tls_destructor(void *data)
{
	ck_stack_t *stack = data;
	ck_stack_entry_t *entry = NULL;

	while(CK_STACK_ISEMPTY(stack) == false) {
		entry = ck_stack_pop_npsc(stack);
		struct an_thread_tls_destructor_stack_entry *se = an_thread_dtor_container(entry);
		if (se != NULL) {
			void *d = pthread_getspecific(se->key);
			se->destructor(d);
			free(se);
		}
	}
	free(stack);
}

static int
an_thread_tls_init(void)
{
	int rval = 0;

	ck_spinlock_fas_lock(&threads_mtx);
	if (an_thread_tls_destructor_key_created == false) {
		rval = pthread_key_create(&an_thread_tls_destructor_key, an_thread_tls_destructor);
		an_thread_tls_destructor_key_created = rval == 0;
	}
	ck_spinlock_fas_unlock(&threads_mtx);

	return rval;
}

const an_smr_record_t *
an_thread_get_smr_record(unsigned int id)
{
	struct an_thread *cursor;

	LIST_FOREACH(cursor, &threads, thread_entry) {
		if (cursor->id == id) {
			return &cursor->smr;
		}
	}

	return NULL;
}

int
an_thread_key_create(an_thread_key_t *key, void (*destructor)(void *))
{
	int rval;
	struct an_thread_tls_destructor_stack_entry *se = NULL;

	if (an_thread_tls_init() != 0) {
		abort();
	}

	rval = pthread_key_create(key, NULL);

	/*  push the key/dtor combination onto the stack if there is a dtor */
	if (destructor != NULL) {
		ck_stack_t *stack = pthread_getspecific(an_thread_tls_destructor_key);
		if (stack == NULL) {
			stack = calloc(sizeof(ck_stack_t), 1);
			if (pthread_setspecific(an_thread_tls_destructor_key, stack) != 0) {
				abort();
			}
		}
		se = calloc(sizeof(struct an_thread_tls_destructor_stack_entry), 1);
		se->destructor = destructor;
		se->key = *key;
		ck_stack_push_spnc(stack, &se->next_dtor);
	}
	return rval;
}

int
an_thread_key_delete(an_thread_key_t key)
{
	return pthread_key_delete(key);
}

int
an_thread_setspecific(an_thread_key_t key, const void *data)
{
	return pthread_setspecific(key, data);
}

void *
an_thread_getspecific(an_thread_key_t key)
{
	return pthread_getspecific(key);
}

int
an_thread_affinity_set(unsigned int cpu)
{
	cpu_set_t s;

	CPU_ZERO(&s);
	CPU_SET(cpu, &s);

	return sched_setaffinity(gettid(), sizeof(s), &s);
}

AN_CC_UNUSED static int
an_thread_numa_io_node(void)
{

	if (io_numa_nodes == 0) {
		return 0;
	}

	return ffs(io_numa_nodes) - 1;
}

/**
 * NICs may be on NUMA node 1.  Affinity targets assume that
 * 0 is low priority/IO, and higher core # are free for CPU
 * heavy stuff.
 *
 * Remap NUMA nodes circularly when assigning physical affinity.
 */
static int
an_thread_affinity_set_modulo_io(unsigned int target)
{
#if LIBNUMA_API_VERSION >= 2
	int cpu_per_node;
	int n_cpu;
	int numa_nodes;
	unsigned int target_node, target_cpu;

	if (an_thread_numa_available() == false) {
		goto out;
	}
	numa_nodes = numa_num_configured_nodes();
	n_cpu = numa_num_configured_cpus();

	/* Please don't make this run on asymmetric setups. */
	if (numa_nodes <= 0 || n_cpu <= 0 || (n_cpu % numa_nodes) != 0) {
		goto out;
	}

	cpu_per_node = n_cpu / numa_nodes;

	target_node = target / cpu_per_node;
	target_cpu = target % cpu_per_node;

	target_node = (target_node + an_thread_numa_io_node()) % numa_nodes;
	target = target_node * cpu_per_node + target_cpu;

out:
#endif

	return an_thread_affinity_set(target);
}

int
an_thread_affine_low_priority(void)
{

	return an_thread_affinity_set_modulo_io(0);
}

int
an_thread_affinity_iterate(struct an_thread_affinity *acb)
{
	unsigned int c;

	c = ck_pr_faa_uint(&acb->request, acb->delta) % acb->cores;
	return an_thread_affinity_set_modulo_io(c);
}

void
an_thread_affinity_init(struct an_thread_affinity *acb, unsigned int d, unsigned int cores)
{

	acb->cores = cores;
	acb->delta = d;
	acb->request = 0;
	return;
}

void
an_thread_defer(void *pointer, void (*f)(void *))
{

	if (pointer == NULL) {
		return;
	}

	an_smr_call(pointer, f);
	return;
}

/*
 * Disables listener associated with bound socket.
 */
static void
an_thread_disable_httpd_fn(struct evhttp_bound_socket *socket, void *unused)
{
	struct evconnlistener *listener = evhttp_bound_socket_get_listener(socket);

	(void)unused;
	evconnlistener_disable(listener);
	return;
}

/*
 * Enables listener associated with bound socket.
 */
static void
an_thread_enable_httpd_fn(struct evhttp_bound_socket *socket, void *unused)
{
	struct evconnlistener *listener = evhttp_bound_socket_get_listener(socket);

	(void)unused;
	evconnlistener_enable(listener);
	return;
}

void
an_thread_httpd_enable(struct an_thread *thread)
{

	if (thread->httpd != NULL) {
		evhttp_foreach_bound_socket(thread->httpd,
		    an_thread_enable_httpd_fn, NULL);
	}

	if (thread->httpsd != NULL) {
		evhttp_foreach_bound_socket(thread->httpsd,
		    an_thread_enable_httpd_fn, NULL);
	}

	return;
}

void
an_thread_httpd_disable(struct an_thread *thread)
{

	if (thread->httpd != NULL) {
		evhttp_foreach_bound_socket(thread->httpd,
		    an_thread_disable_httpd_fn, NULL);
	}

	if (thread->httpsd != NULL) {
		evhttp_foreach_bound_socket(thread->httpsd,
		    an_thread_disable_httpd_fn, NULL);
	}

	return;
}

/*
 * Returns current number of threads.
 */
unsigned int
an_thread_count(void)
{

	return ck_pr_load_uint(&an_thread_id);
}

/*
 * Grab thread-local state.
 */
struct an_thread *
an_thread_get(void)
{
	struct an_thread *s;

	if (current != NULL)
		return current;

	s = an_thread_getspecific(an_thread_key);
	current = s;
	an_smr_register(&current->smr);
	an_thread_current_is_preferred_worker = current->is_preferred_worker;
	an_thread_current_id = current->id;
	return s;
}

void
an_thread_join(void)
{
	struct an_thread *cursor;
	int state;

	if (current != server_config->process)
		return;

	/*
	 * This is currently only safe to call from the server process.
	 */
	LIST_FOREACH(cursor, &threads, thread_entry) {
		if (cursor == server_config->process)
			continue;

		state = ck_pr_load_int(&cursor->state);
		if (state == AN_THREAD_STATE_JOINED || state == AN_THREAD_STATE_READY)
			continue;

		pthread_join(cursor->thread, NULL);
		ck_pr_store_int(&cursor->state, AN_THREAD_STATE_JOINED);
	}

	return;
}

bool
an_thread_tryjoin(void)
{
	struct an_thread *cursor;
	int state;
	bool all_joined = true;

	if (current != server_config->process) {
		return false;
	}


	/*
	 * This is currently only safe to call from the server process.
	 */
	LIST_FOREACH(cursor, &threads, thread_entry) {
		if (cursor == server_config->process) {
			continue;
		}

		state = ck_pr_load_int(&cursor->state);
		if (state == AN_THREAD_STATE_JOINED || state == AN_THREAD_STATE_READY) {
			continue;
		}

		int ret = pthread_tryjoin_np(cursor->thread, NULL);
		if (ret == EBUSY) {
			all_joined = false;
			continue;
		}

		ck_pr_store_int(&cursor->state, AN_THREAD_STATE_JOINED);
	}

	return all_joined;
}

static void
an_thread_monitor_memory(struct an_monitor_memory *memory)
{
	anon_ratio = (double)memory->vm_rss_anon / (double)memory->phys_memtotal;

	/*
	 * We only want to consider anonymous pages for our critical memory threshold,
	 * because file-backed pages are evictable and expected to accumulate with
	 * an_db and LMDB.
	 */
	if (anon_ratio >= AN_THREAD_MEMORY_THRESHOLD_CRITICAL) {
		/* Abort and allow the parent to restart the process. */
		an_syslog(LOG_CRIT, "Critical low memory condition, aborting.");
		abort();
	}

#if !defined(DISABLE_SMR)
#if defined(USR_RTBR)
	/*
	 * Arbitrary 5 second delay. Impbus used to mostly run
	 * fine with 5 second TBR ;)
	 */
	if (anon_ratio >= 0.95 * AN_THREAD_MEMORY_THRESHOLD_CRITICAL) {
		an_rtbr_force_progress(5000);
	}
#endif
	an_smr_poll();
#endif

	return;
}

#ifdef HAVE_LIBGIMLI
static void
an_thread_gimli(void)
{

	assert(an_thread_gimli_is_enabled());

	if (hb != NULL) {
		gimli_heartbeat_set(hb, GIMLI_HB_RUNNING);
	}

	return;
}
#endif

void
an_thread_signal(struct an_thread *thread, unsigned int s)
{

	if (thread == NULL) {
		struct an_thread *cursor;

		ck_spinlock_fas_lock(&threads_mtx);
		LIST_FOREACH(cursor, &threads, thread_entry)
			ck_pr_or_uint(&cursor->signal, s);
		ck_spinlock_fas_unlock(&threads_mtx);
	} else {
		ck_pr_or_uint(&thread->signal, s);
	}

	return;
}

static void
an_thread_heartbeat_internal(void)
{
	struct an_thread *thread = current;
	unsigned int signal;
	struct an_thread_block *block;

	while (ck_fifo_spsc_dequeue(&thread->command.queue, &block) == true) {
		if (block->e(block->closure) == false) {
			an_syslog(LOG_CRIT, "[%u] Failed to execute command: %p/%p\n",
			    current->id, block->e, block->closure);
		}

		an_free(block_token, block);
	}

#if !defined(DISABLE_SMR)
	if (an_smr_n_pending(&current->smr) != 0) {
		an_smr_poll();
	}
#endif

	signal = ck_pr_load_uint(&thread->signal);
	if (signal == 0) {
		return;
	}

	signal = ck_pr_fas_uint(&thread->signal, 0);
	if (signal & AN_THREAD_FLUSH) {
		server_config_flush();
	}

	if (signal & AN_THREAD_SHUTDOWN) {
		struct timeval tv;

		tv.tv_sec = 10;
		tv.tv_usec = 0;
		event_base_loopexit(thread->event_base, &tv);
	}

	return;
}

static void
an_thread_queue_latency_heartbeat(int fd, short event, void *arg)
{
	struct an_thread *thread = arg;
	struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
	uint64_t now = micros_now();

	if (thread->queue_latency_assignment_time != 0) {
		uint64_t delta = now - thread->queue_latency_assignment_time;
		if (delta > 100000) {
			thread->queue_latency = now - thread->queue_latency_assignment_time - 100000;
		}
	}

	thread->queue_latency_assignment_time = now;
	evtimer_assign(&thread->queue_latency_timer, thread->event_base, an_thread_queue_latency_heartbeat, thread);
	evtimer_add(&thread->queue_latency_timer, &tv);

	return;
}

void
an_thread_push_slow(struct an_thread_cleanup *cleanup)
{
	size_t goal = max(1UL, 2UL * current->max_cleanups);

	if (goal > UINT_MAX) {
		goal = UINT_MAX;
	}

	current->cleanups = an_realloc_region(cleanup_token,
	    current->cleanups,
	    current->max_cleanups * sizeof(*current->cleanups),
	    goal * sizeof(*current->cleanups));
	current->max_cleanups = goal;
	current->cleanups[current->n_cleanups++] = *cleanup;
	return;
}

void
an_thread_pop_slow(struct an_thread_cleanup *cleanup)
{
	size_t n;

	assert(current->n_cleanups > 0);
	n = --current->n_cleanups;
	assert(current->cleanups[n].cb == cleanup->cb);
	assert(current->cleanups[n].data == cleanup->data);
	return;
}

/*
 * Catches any pending signals.
 */
void
an_thread_catch(void)
{

	an_thread_heartbeat_internal();
	return;
}

bool
an_thread_broadcast(an_thread_broadcast_execute_fn_t *e,
		    an_thread_broadcast_create_fn_t *c,
		    void *m)
{
	struct an_thread_block *entry = NULL;
	ck_fifo_spsc_entry_t *fifo_entry = NULL;
	struct an_thread *cursor;
	unsigned int i;

	i = 0;
	LIST_FOREACH(cursor, &threads, thread_entry) {
		if (cursor == server_config->process) {
			continue;
		}

		entry = an_calloc_object(block_token);
		if (entry == NULL) {
			goto fail;
		}

		fifo_entry = ck_fifo_spsc_recycle(&cursor->command.queue);
		if (fifo_entry == NULL) {
			fifo_entry = an_calloc_object(fifo_token);
			if (fifo_entry == NULL) {
				goto fail;
			}
		}

		entry->e = e;

		if (c != NULL) {
			entry->closure = c(cursor->id, m);
			if (entry->closure == NULL) {
				goto fail;
			}
		} else {
			entry->closure = NULL;
		}

		ck_fifo_spsc_enqueue(&cursor->command.queue, fifo_entry, entry);
		i++;
	}

	return true;

fail:
	an_free(block_token, entry);
	an_free(fifo_token, fifo_entry);
	return false;
}

void
an_thread_broadcast_wait(void)
{
	struct an_thread *cursor;

	LIST_FOREACH(cursor, &threads, thread_entry) {
		while (ck_fifo_spsc_isempty(&cursor->command.queue) == false)
			ck_pr_stall();
	}

	return;
}

bool
an_thread_broadcast_pending(void)
{
	struct an_thread *cursor;

	LIST_FOREACH(cursor, &threads, thread_entry) {
		if (ck_fifo_spsc_isempty(&cursor->command.queue) == false)
			return true;
	}

	return false;
}

/*
 * Associate the provided an_thread with the underlying POSIX thread.
 */
int
an_thread_put(struct an_thread *thread)
{
	sigset_t sigset;
	struct an_monitor *monitor;
	char buffer[20];

	if (current != NULL) {
		errno = EINVAL;
		return -1;
	}

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGHUP);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGPIPE);
	sigaddset(&sigset, SIGALRM);
	pthread_sigmask(SIG_BLOCK, &sigset, (sigset_t *)NULL);

	if (an_thread_setspecific(an_thread_key, thread) != 0) {
		return -1;
	}

	thread->tid = gettid();
	an_thread_get();

	if (server_config != NULL && server_config->process == thread) {
		sprintf(buffer, "memory%u", thread->id);
		monitor = an_monitor_create(buffer, 30);
		if (monitor != NULL) {
			an_monitor_enable(monitor, AN_MONITOR_MEMORY, an_thread_monitor_memory);
		}

#ifdef HAVE_LIBGIMLI
		if (an_thread_gimli_is_enabled()) {
			hb = gimli_heartbeat_attach();
			if (hb != NULL) {
				gimli_heartbeat_set(hb, GIMLI_HB_STARTING);
			}
		}
#endif
	} else {
		sprintf(buffer, "heartbeat%u", thread->id);
		monitor = an_monitor_create(buffer, server_config->heartbeat_interval);
		if (monitor != NULL) {
			an_monitor_enable(monitor, AN_MONITOR_HEARTBEAT, an_thread_heartbeat_internal);
		}

		an_syslog_register_producer();
	}

	/* Register a timer callback which will record queue depth latency. */
	an_thread_queue_latency_heartbeat(0, 0, thread);
	an_sstm_register_thread();
	current->state = AN_THREAD_STATE_ACTIVE;
	current->monitor = monitor;
	return 0;
}

void
an_thread_heartbeat(struct an_thread *thread)
{
#ifdef HAVE_LIBGIMLI
	struct an_monitor *monitor;
	char buffer[20];

	if (an_thread_gimli_is_enabled()) {

		if (hb == NULL)
			return;

		sprintf(buffer, "heartbeat%u", thread->id);
		monitor = an_monitor_create(buffer, 10);
		if (monitor != NULL)
			an_monitor_enable(monitor, AN_MONITOR_HEARTBEAT, an_thread_gimli);

		gimli_establish_signal_handlers();
	}
#else
	(void)thread;
#endif

	an_thread_setup_soft_error_handler();
	return;
}

void
an_thread_tick(void)
{

#ifdef HAVE_LIBGIMLI
	if (an_thread_gimli_is_enabled()) {
		an_thread_gimli();
	}
#endif
	return;
}

struct an_thread *
an_thread_create(void)
{
	ck_fifo_spsc_entry_t *spsc_entry;
	struct an_thread *current = an_calloc_object(token);
	struct timeval tv;

	gettimeofday(&tv, NULL);
	if (current == NULL) {
		return NULL;
	}

	current->bt_rd = current->bt_wr = current->bt_free = NULL;
	current->scheduler = NULL;
	current->state = AN_THREAD_STATE_READY;
	current->signal = 0;
	current->httpd = NULL;
	current->httpsd = NULL;
	current->event_base = NULL;
	current->dns_base = NULL;
	current->id = ck_pr_faa_uint(&an_thread_id, 1);
	current->httpsd = NULL;
	current->malloc = NULL;
	an_xorshift_plus_seed(current->xorshift_state,
	    (gettid() << 16) ^ tv.tv_sec ^ tv.tv_usec);

	current->cleanups = an_calloc_region(cleanup_token,
	    32, sizeof(struct an_thread_cleanup));
	current->n_cleanups = 0;
	current->max_cleanups = 32;

	spsc_entry = an_calloc_object(fifo_token);
	if (spsc_entry == NULL) {
		an_free(token, current);
		current = NULL;
		return NULL;
	}

	ck_fifo_spsc_init(&current->command.queue, spsc_entry);

	current->unwind_target_set = false;

	current->num_open_epochs = 0;
	current->num_reclaimed_epochs = 0;
	STAILQ_INIT(&current->open_epochs);
	STAILQ_INIT(&current->reclaimed_epochs);

	ck_spinlock_fas_lock(&threads_mtx);
	LIST_INSERT_HEAD(&threads, current, thread_entry);
	ck_spinlock_fas_unlock(&threads_mtx);
	return current;
}

static void
an_thread_handler_http(struct evhttp_request *request, void *c)
{
	struct an_thread *cursor;

	EVBUFFER_ADD_STRING(request->output_buffer, "\"threads\" : [\n");
	ck_spinlock_fas_lock(&threads_mtx);
	LIST_FOREACH(cursor, &threads, thread_entry) {
		EVBUFFER_ADD_STRING(request->output_buffer, "\t{\n");
		evbuffer_add_printf(request->output_buffer, "\t\t\"id\"              : %u,\n", cursor->id);
		evbuffer_add_printf(request->output_buffer, "\t\t\"tid\"             : %ju,\n", (uintmax_t)cursor->tid);
		evbuffer_add_printf(request->output_buffer, "\t\t\"queue_latency\"             : %" PRIu64 ",\n", cursor->queue_latency);

		if (cursor->scheduler != NULL) {
			struct an_thread *master = an_sched_master(cursor->scheduler);
			if (master != NULL && master != cursor) {
				evbuffer_add_printf(request->output_buffer, "\t\t\"master\"          : %u,\n", master->id);
			} else {
				evbuffer_add_printf(request->output_buffer, "\t\t\"httpd_port\"      : %d,\n",
					server_config->port - 1 + cursor->id);
			}
		} else {
			evbuffer_add_printf(request->output_buffer, "\t\t\"httpd_port\"      : %d,\n",
				server_config->port - 1 + cursor->id);
		}

		/* This is not guaranteed to be correct. */
		evbuffer_add_printf(request->output_buffer, "\t\t\"event_base\"      : [ %d, %d, %d ],\n",
			event_base_get_max_events(cursor->event_base, EVENT_BASE_COUNT_ACTIVE, true),
			event_base_get_max_events(cursor->event_base, EVENT_BASE_COUNT_ADDED, true),
			event_base_get_max_events(cursor->event_base, EVENT_BASE_COUNT_VIRTUAL, true));

		an_smr_handler_http(request, cursor);
		if (LIST_NEXT(cursor, thread_entry) != NULL) {
			EVBUFFER_ADD_STRING(request->output_buffer, "\t},\n");
		} else {
			EVBUFFER_ADD_STRING(request->output_buffer, "\t}\n");
		}
	}
	ck_spinlock_fas_unlock(&threads_mtx);
	EVBUFFER_ADD_STRING(request->output_buffer, "]\n");

	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

static void
total_soft_errors_handler(struct evhttp_request *request, void *c)
{
	struct evkeyvalq kv;
	const char *value_str, *uri;
	unsigned int value;

	(void)c;
	uri = evhttp_request_uri(request);
	if (uri == NULL) {
		goto out;
	}

	if (evhttp_parse_query(uri, &kv) != 0) {
		goto out;
	}

	value_str = evhttp_find_header(&kv, "value");
	if (value_str == NULL) {
		goto out;
	}

	str2uint32(value_str, &value, 0);
	an_thread_max_soft_errors(value);

out:
	evbuffer_add_printf(request->output_buffer, "max total soft errors: %u (current: %u)\n",
	    ck_pr_load_uint(&max_total_soft_errors), ck_pr_load_uint(&total_soft_errors));

	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

static void
consecutive_soft_errors_handler(struct evhttp_request *request, void *c)
{
	struct evkeyvalq kv;
	const char *value_str, *uri;
	unsigned int value;

	(void)c;
	uri = evhttp_request_uri(request);
	if (uri == NULL) {
		goto out;
	}

	if (evhttp_parse_query(uri, &kv) != 0) {
		goto out;
	}

	value_str = evhttp_find_header(&kv, "value");
	if (value_str == NULL) {
		goto out;
	}

	str2uint32(value_str, &value, 0);
	an_thread_max_consecutive_soft_errors(value);

out:
	evbuffer_add_printf(request->output_buffer, "max consecutive soft errors: %u (current: %u)\n",
	    ck_pr_load_uint(&max_consecutive_soft_errors),
	    ck_pr_load_uint(&consecutive_soft_errors_watermark));

	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

void
an_thread_handler_http_enable(struct evhttp *httpd)
{

	an_handler_control_register("threads/list", an_thread_handler_http, NULL, NULL);

	an_handler_control_register("debug/total_soft_errors", total_soft_errors_handler, NULL, NULL);
	an_handler_control_register("debug/consecutive_soft_errors", consecutive_soft_errors_handler, NULL, NULL);

	return;
}

static void
an_thread_key_destroy(void *p)
{

	return;
}

void
an_thread_perf_init(void)
{

	return;
}

static int
an_thread_numa_available(void)
{

	return disable_numa == false && numa_available() != -1;
}

int
an_thread_numa_set(bool local)
{
	int ret = -1;
	int io_numa_node;

	(void)io_numa_node;
#if NUMA_MODE == NUMA_MODE_DEFAULT
	return ret;
#endif

	/* coverity[unreachable : FALSE] */
	if (an_thread_numa_available() == false) {
		return ret;
	}

	if (local == true) {
		numa_set_localalloc();
	} else {
#if NUMA_MODE == NUMA_MODE_LOCAL
		io_numa_node = an_thread_numa_io_node();
		ret = numa_max_node();
		if (io_numa_node > 0) {
			ret = io_numa_node - 1;
		}
		numa_set_preferred(ret);
#elif NUMA_MODE == NUMA_MODE_INTERLEAVE
#if LIBNUMA_API_VERSION >= 2
		numa_set_interleave_mask(numa_all_nodes_ptr);
#else
		numa_set_interleave_mask(&numa_all_nodes);
#endif
#endif
	}

	return ret;
}

void
an_thread_disable_numa(void)
{

	disable_numa = true;
}

void
an_thread_set_nodemask(void)
{

#if NUMA_MODE != NUMA_MODE_DEFAULT
	if (an_thread_numa_available() == true) {
		int index;

		index = an_thread_numa_set(false);
		if (index >= 0) {
#if LIBNUMA_API_VERSION >= 2
			struct bitmask *last_node;

			last_node = numa_bitmask_alloc(numa_max_node() + 1);
			numa_bitmask_clearall(last_node);
			numa_bitmask_setbit(last_node, index);
			numa_migrate_pages(0, numa_all_nodes_ptr, last_node);
			numa_bitmask_free(last_node);
#else
			nodemask_t last_node;

			nodemask_zero(&last_node);
			nodemask_set(&last_node, index);
			/* Migrate self. */
			numa_migrate_pages(0, &numa_all_nodes, &last_node);
#endif
		}
	}
#endif

	return;
}

void
an_thread_setup_unwind_slow(int status)
{

	if (status == 0) {
		assert(current->unwind_target_set == false);
		assert(current->irrevocable_cleanups == 0);
		current->unwind_target_set = true;
		current->irrevocable_cleanups = current->n_cleanups;

		if (current->n_cleanups > 128) {
			an_syslog(LOG_NOTICE,
			    "thread %d n_cleanup = %u. Likely missing an unlock/cleanup.\n",
			    current->id, current->n_cleanups);
		}

		return;
	}

	for (size_t i = current->n_cleanups; i > current->irrevocable_cleanups; ) {
		i--;
		current->cleanups[i].cb(current->cleanups[i].data);
	}

	an_syslog_backtrace(LOG_CRIT, "Attempting to recover from signal %d. Thread %d backtrace:",
	    status, current->id);

	assert(current->n_cleanups >= current->irrevocable_cleanups);
	current->n_cleanups = current->irrevocable_cleanups;
	return;
}

void
an_thread_clear_unwind()
{
	unsigned int consecutive;
	unsigned int max_consecutive = ck_pr_load_uint(&consecutive_soft_errors_watermark);

	assert(current->unwind_target_set == true);
	current->unwind_target_set = false;

	consecutive = ck_pr_fas_uint(&consecutive_soft_errors, 0);
	if (consecutive > max_consecutive) {
		/* Only best effort. */
		ck_pr_cas_uint(&consecutive_soft_errors_watermark, max_consecutive, consecutive);
	}

	assert(current->n_cleanups == current->irrevocable_cleanups);
	current->irrevocable_cleanups = 0;
	return;
}

static void
an_thread_soft_error_handler(int sig, siginfo_t *info, void *data)
{

	(void)data;
	switch (info->si_code) {
	case SI_USER:
	case SI_KERNEL:
	case SI_QUEUE:
	case SI_TIMER:
	case SI_MESGQ:
	case SI_ASYNCIO:
	case SI_SIGIO:
	case SI_TKILL:
		goto pass;
	default:
		break;
	}

	if (current != NULL && current->unwind_target_set == true) {
		unsigned int consecutive = ck_pr_faa_uint(&consecutive_soft_errors, 1);
		unsigned int max_consecutive = ck_pr_load_uint(&consecutive_soft_errors_watermark);
		unsigned int total = ck_pr_faa_uint(&total_soft_errors, 1);

		/* We have poison on the stack. */
		for (size_t i = 0; i < current->n_cleanups; i++) {
			if (current->cleanups[i].cb == NULL) {
				goto pass;
			}
		}

		if (consecutive + 1 > max_consecutive) {
			/* Best effort only... */
			ck_pr_cas_uint(&consecutive_soft_errors_watermark, max_consecutive, consecutive + 1);
		}

		if (total < ck_pr_load_uint(&max_total_soft_errors) &&
		    consecutive < ck_pr_load_uint(&max_consecutive_soft_errors)) {
			current->unwind_target_set = false;
			current->backtrace_size = backtrace(current->backtrace,
			    ARRAY_SIZE(current->backtrace));
			siglongjmp(current->unwind_target, sig);
			return;
		}
	}

pass:
#ifdef HAVE_LIBGIMLI
	if (an_thread_gimli_is_enabled()) {
		gimli_establish_signal_handlers();
	} else {
		signal(sig, SIG_DFL);
	}
#else
	signal(sig, SIG_DFL);
#endif

	raise(sig);
	return;
}

void
an_thread_setup_soft_error_handler()
{
	int sigs[] = { SIGFPE, SIGSEGV, SIGBUS };
	struct sigaction action = {
		.sa_sigaction = an_thread_soft_error_handler,
		.sa_flags = SA_SIGINFO | SA_NODEFER
	};

	/* backtrace needs signal unsafe lazy initialisation. */
	{
		void *buf[4];

		backtrace(buf, ARRAY_SIZE(buf));
	}

	for (size_t i = 0; i < ARRAY_SIZE(sigs); i++) {
		sigaction(sigs[i], &action, NULL);
	}

	return;
}

void
an_thread_max_consecutive_soft_errors(unsigned int max)
{

	ck_pr_store_uint(&max_consecutive_soft_errors, max);
	return;
}

void
an_thread_max_soft_errors(unsigned int max)
{

	ck_pr_store_uint(&max_total_soft_errors, max);
	return;
}

bool
an_thread_soft_errors_enabled()
{

	return (ck_pr_load_uint(&max_consecutive_soft_errors) > 0 &&
	    ck_pr_load_uint(&max_total_soft_errors) > 0);
}

unsigned int
an_thread_consecutive_soft_errors(bool clear)
{

	if (clear == true) {
		return ck_pr_fas_uint(&consecutive_soft_errors_watermark, 0);
	}

	return ck_pr_load_uint(&consecutive_soft_errors_watermark);
}

unsigned int
an_thread_soft_errors(bool clear)
{

	if (clear == true) {
		return ck_pr_fas_uint(&total_soft_errors, 0);
	}

	return ck_pr_load_uint(&total_soft_errors);
}

/*
 * Discover the NUMA nodes our network interface lives on.
 */
int
an_thread_set_bonding_interface(const char *bonding_if)
{
	char path[PATH_MAX], buf[128];
	char numabuf[16];
	char *ifname, *cp;
	ssize_t n;
	int fd, node;

	snprintf(path, sizeof(path), "/sys/class/net/%s/bonding/slaves",
	    bonding_if);
	fd = open(path, O_RDONLY);
	if (fd == -1) {
		an_syslog(LOG_WARNING, "Failed to open \"%s\": %s",
		    path, an_strerror(errno));
		return -1;
	}

	n = an_readall(fd, buf, sizeof(buf));
	close(fd);

	if (n == -1) {
		an_syslog(LOG_WARNING, "Failed to read \"%s\": %s",
		    path, an_strerror(errno));
		return -1;
	}

	if (buf[n - 1] == '\n') {
		buf[n - 1] = '\0';
	}

	cp = buf;
	for (;;) {
		ifname = strsep(&cp, " ");
		if (ifname == NULL) {
			break;
		}

		snprintf(path, sizeof(path),
		    "/sys/class/net/%s/device/numa_node", ifname);
		fd = open(path, O_RDONLY);
		if (fd == -1) {
			an_syslog(LOG_WARNING, "Failed to open \"%s\": %s",
			    path, an_strerror(errno));
			continue;
		}
		n = an_readall(fd, numabuf, sizeof(numabuf));
		close(fd);
		if (n == -1) {
			an_syslog(LOG_WARNING,
			    "Failed to read NUMA node for interface %s: %s",
			    ifname, an_strerror(errno));
			continue;
		}

		str2int(numabuf, &node, -1);
		if (node == -1) {
			an_syslog(LOG_WARNING,
			    "Failed to parse contents of \"%s\"", path);
			continue;
		}

		io_numa_nodes |= (1U << node);
	}
	return 0;
}

void
an_thread_init(void)
{

	if (an_thread_key_create(&an_thread_key, an_thread_key_destroy) != 0)
		abort();

	an_smr_init();

	an_thread_setup_soft_error_handler();

	return;
}

double
an_thread_get_rss_anon_ratio(void)
{

	return anon_ratio;
}
