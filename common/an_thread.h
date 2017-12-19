#ifndef _AN_THREAD_H
#define _AN_THREAD_H

#include <ck_cc.h>
#include <ck_fifo.h>
#include <stdlib.h>
#include <event2/event.h>
#include <event2/http.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <limits.h>

#include "an_monitor.h"
#include "an_smr.h"
#include "autoconf_globals.h"
#include "common/an_cc.h"
#include "common/an_sched.h"

#ifndef AN_THREAD_LIMIT
#define AN_THREAD_LIMIT 32U
#endif

#define AN_THREAD_BACKTRACE_LIMIT 200

#define AN_THREAD_STAGE_CHAR_LIMIT 64

#define AN_THREAD_STACK_SIZE PTHREAD_STACK_MIN

#define AN_THREAD_STATE_READY  1
#define AN_THREAD_STATE_ACTIVE 2
#define AN_THREAD_STATE_EXITED 4
#define AN_THREAD_STATE_JOINED 8

struct an_thread_backtrace {
	const char *function;
	const char *file;
	unsigned int line;
	struct an_thread_backtrace *next;
};

enum an_thread_signal {
	AN_THREAD_FLUSH = 1,
	AN_THREAD_SHUTDOWN = 2,
};

struct an_thread_affinity {
	unsigned int delta;
	unsigned int request;
	unsigned int cores;
};
typedef struct an_thread_affinity an_thread_affinity_t;

#ifdef HAVE_LIBGIMLI
/**
 * @brief Initializes gimli, should be called at most once upon start
 * @param enable True to enable gimli, false to disable
 */
void an_thread_gimli_init(bool enable);

/**
 * @brief Check if gimli is enabled
 * @return True if enabled, false otherwise
 */
bool an_thread_gimli_is_enabled();
#endif

/**
 * @brief opaque key for thread local storage
 */
typedef pthread_key_t an_thread_key_t;

/**
 * @brief retrieves thread's smr record
 * @param id id of a thread for which to retrieve smr record
 *
 * @return smr record of a thread
 */
const an_smr_record_t *an_thread_get_smr_record(unsigned int id);

/**
 * @brief create an `an_thread_key_t` and optionally register a destructor
 * @return zero on success, non-zero on failure
 *
 * Unlike pthread_key_create, this function maintains the registered keys in a stack so that
 * destructors are called in the opposite order they are registered.
 */
int an_thread_key_create(an_thread_key_t *key, void (*destructor)(void*));

/**
 * @brief delete a previously registered an_thread_key_t
 * @return 0 on success, non-zero on failure
 *
 * This follows the semantics of pthread_key_delete in that any registered destructor functions are not called.
 */
int an_thread_key_delete(an_thread_key_t key);

/**
 * @brief Store thread local data at `an_thread_key_t`
 * @return 0 on success, non-zero on failure
 */
int an_thread_setspecific(an_thread_key_t key, const void *value);

/*
 * @brief return thread local data previously stored using `an_thread_setspecific`
 *
 * This will return NULL if no thread local data is stored at that key or the key does not exist.
 */
void *an_thread_getspecific(an_thread_key_t key);

/**
 * Affines the currently executing thread with the given core.
 *
 * @return -1 on failure, 0 on success.
 */
int an_thread_affinity_set(unsigned int cpu);

/**
 * Affines the currently executing thread to a low priority code.
 *
 * @return -1 on failure, 0 on success.
 */
int an_thread_affine_low_priority(void);

/**
 * Affines the currently executing thread with the next available
 * core available in the affinity object.
 *
 * @return -1 on failure, 0 on success.
 */
int an_thread_affinity_iterate(an_thread_affinity_t *);

/**
 * Initializes the provided an_thread_affinity_t object to allow
 * for threads to iterate over the number of cores specified in
 * the third argument at the interval specified in the second
 * argument.
 */
void an_thread_affinity_init(an_thread_affinity_t *object, unsigned int delta, unsigned int cores);

struct an_sched_context;
struct an_malloc_thread;
struct an_thread_cleanup {
	void (*cb)(void *);
	void *data;
};

struct an_thread {
	unsigned int id;
	/* True for (one of) the thread we expect to get most of the work units. */
	bool is_preferred_worker;
	struct an_malloc_thread *malloc;

	struct an_malloc_epoch *epoch;
	struct an_malloc_state malloc_state;
	/* Thread-local state for an_rand64. */
	uint64_t xorshift_state[2];

	/*
	 * Flexible array of cleanups.  Not an an_array because
	 * an_array depends on an_swlock which depends on an_thread.
	 */
	struct {
		struct an_thread_cleanup *cleanups;
		unsigned int n_cleanups;
		unsigned int max_cleanups;
		/* First irrevocable_cleanups elements are not covered by unwind. */
		unsigned int irrevocable_cleanups;
	};

	an_smr_record_t smr;
	/* Command queue for low frequency events. */
	struct {
		ck_fifo_spsc_t queue;
	} command;

	sigjmp_buf unwind_target;
	bool unwind_target_set;
	int backtrace_size;
	void *backtrace[AN_THREAD_BACKTRACE_LIMIT];
	char backtrace_info[AN_THREAD_STAGE_CHAR_LIMIT];

	an_sched_t *scheduler;
	struct an_sched_context *scheduler_context;

	struct an_thread_backtrace *bt_rd;
	struct an_thread_backtrace *bt_wr;
	struct an_thread_backtrace *bt_free;

	unsigned int signal;
	int httpd_socket;

	struct event_base *event_base;
	struct evdns_base *dns_base;
	struct evhttp *httpd;
	struct evhttp *httpsd;
	pthread_t thread;

	size_t num_open_epochs;
	size_t num_reclaimed_epochs;
	STAILQ_HEAD(epochs_head, an_malloc_epoch) open_epochs;
	STAILQ_HEAD(reclaimed_epochs_head, an_malloc_epoch) reclaimed_epochs;

	int state;
	pid_t tid;

	LIST_ENTRY(an_thread) thread_entry;
	struct an_monitor *monitor;

	/* Monitor on event queue latency */
	uint64_t queue_latency_assignment_time;
	struct event queue_latency_timer;
	uint64_t queue_latency;
} CK_CC_CACHELINE;

/*
 * Per-thread context can be accessed through current.
 */
extern __thread struct an_thread *current;
extern __thread unsigned int an_thread_current_id;
extern __thread bool an_thread_current_is_preferred_worker;

#define AN_THREAD_STAGE(...) do {					\
		snprintf(current->backtrace_info,			\
		    ARRAY_SIZE(current->backtrace_info), __VA_ARGS__);	\
		current->backtrace_info[ARRAY_SIZE(current->backtrace_info) - 1] = '\0'; \
	} while (0)

void an_thread_push_slow(struct an_thread_cleanup *);
void an_thread_pop_slow(struct an_thread_cleanup *);

#define an_thread_push(CB, ARG)						\
	do {								\
		struct an_thread_cleanup cleanup;			\
									\
		cleanup.cb = AN_CC_CAST_CB(void, (CB), (ARG));		\
		cleanup.data = (ARG);					\
		if (current->n_cleanups < current->max_cleanups) {	\
			current->cleanups[current->n_cleanups++] = cleanup; \
		} else {						\
			an_thread_push_slow(&cleanup);			\
		}							\
	} while (0)

#define an_thread_push_poison(ARG)					\
	do {								\
		struct an_thread_cleanup cleanup;			\
									\
		cleanup.cb = NULL;					\
		cleanup.data = (ARG);					\
		if (current->n_cleanups < current->max_cleanups) {	\
			current->cleanups[current->n_cleanups++] = cleanup; \
		} else {						\
			an_thread_push_slow(&cleanup);			\
		}							\
	} while (0)

#ifndef DISABLE_DEBUG
#define an_thread_pop(CB, ARG)						\
		  do {							\
			  struct an_thread_cleanup cleanup;		\
									\
			  cleanup.cb = AN_CC_CAST_CB(void, (CB), (ARG)); \
			  cleanup.data = (ARG);				\
			  an_thread_pop_slow(&cleanup);			\
		  } while (0)

#define an_thread_pop_poison(ARG)					\
	do {								\
		struct an_thread_cleanup cleanup;			\
									\
		cleanup.cb = NULL;					\
		cleanup.data = (ARG);					\
		an_thread_pop_slow(&cleanup);				\
	} while (0)
#else
#define an_thread_pop(CB, ARG)						\
		  do {							\
			  struct an_thread_cleanup cleanup;		\
									\
			  cleanup.cb = AN_CC_CAST_CB(void, (CB), (ARG)); \
			  cleanup.data = (ARG);				\
			  (void)cleanup;				\
			  current->n_cleanups--;			\
		  } while (0)

#define an_thread_pop_poison(ARG)		\
	do {					\
						\
		(void)(ARG);			\
		current->n_cleanups--;		\
	} while (0)
#endif

typedef bool an_thread_broadcast_execute_fn_t(void *);
typedef void *an_thread_broadcast_create_fn_t(unsigned int, void *);

/**
 * Broadcast a callback for execution by all threads. The third argument
 * is passed to the function specified in the second argument in order
 * to generate the function argument associated with the broadcast
 * execution function.
 */
bool an_thread_broadcast(an_thread_broadcast_execute_fn_t *,
			 an_thread_broadcast_create_fn_t *,
			 void *);

/*
 * Block until all thread command queues are empty.
 */
void an_thread_broadcast_wait(void);

/*
 * Return true if any threads still have unprocessed commands.
 */
bool an_thread_broadcast_pending(void);

/**
 * This function will wait for all processing threads to halt
 * execution. This function may only be called from the first
 * thread (the control thread).
 */
void an_thread_join(void);

/**
 * This function will wait for all processing threads to halt
 * execution. This function may only be called from the first
 * thread (the control thread). Unlike standard join this function will not
 * block.  It will return true only after all threads have successfully
 * shutdown.
 */
bool an_thread_tryjoin(void);

/**
 * Defers the destruction of the object pointed to by the first
 * argument. The function destructor is applied at some point
 * in the future when no active references exist to the given
 * object. The object must first be made unreachable.
 *
 * @param pointer    Pointer to object that requires deferred destruction.
 * @param destructor Function that is called to destroy the object pointed to by
 *                   pointer.
 */
void an_thread_defer(void *pointer, void (*destructor)(void *));

/**
 * Disables any listeners associated with the httpd and/or httpsd
 * objects associated with the provided thread. This prevents
 * additional incoming connections from being processed.
 */
void an_thread_httpd_disable(struct an_thread *);

/**
 * Enables any listeners associated with the httpd and/or httpsd
 * objects associated with the provided thread. This allows
 * additional incoming connections from being processed.
 */
void an_thread_httpd_enable(struct an_thread *);

/**
 * Creates an an_thread object. The thread is provided
 * a unique ID and becomes globally manageable through
 * various an_thread facilities. Epoch reclamation
 * and BRL state is initialized at this stage. In order
 * for this thread to be used, an_thread_put() should be
 * executed in the context of the POSIX thread that is to
 * be associated with the an_thread object.
 *
 * @return NULL on failure, otherwise returns a pointer to a valid
 *         an_thread object.
 */
struct an_thread *an_thread_create(void);

/**
 * Returns a pointer to the an_thread context associated with
 * the currently executing POSIX thread.
 *
 * @return NULL if no an_thread context is associated with the
 *         currently executing POSIX thread, otherwise returns
 *         a pointer to a valid an_thread object.
 */
struct an_thread *an_thread_get(void);

/**
 * Associates the provided thread object with the currently
 * executing POSIX thread. In addition to this, various
 * monitoring facilities are enabled for the thread including
 * a heartbeat monitor.
 *
 * @param thread The an_thread object to associate with the currently
 *               executing POSIX thread.
 * @return -1 on failure, 0 on success.
 */
int an_thread_put(struct an_thread *thread);

/**
 * Causes a heartbeat tick, you should use on operations that will
 * take longer than a minute.
 */
void an_thread_tick(void);

/**
 * Returns the number of an_thread objects that have been allocated.
 */
unsigned int an_thread_count(void);

/**
 * Sends the specified signal to the specified thread. Signals are
 * not queued, and are processed in bulk through a heartbeat monitor
 * that by default monitors signal state every ten seconds.
 e
 * @param thread Thread to send signal to.
 * @param signal Signal to send to thread, currently only the
 *               AN_THREAD_SHUTDOWN and AN_THREAD_FLUSH are
 *               supported.
 */
void an_thread_signal(struct an_thread *thread, unsigned int signal);

/**
 * Catches any pending signals or commands waiting to be processed by
 * the thread.
 */
void an_thread_catch(void);

/**
 * Initializes an_latency contexts associated with the
 * an_thread sub-system.
 */
void an_thread_perf_init(void);

/**
 * VEs want to call this function to prioritise batch processing speed
 * over workers.
 */
void an_thread_disable_numa(void);

/**
 * Set up NUMA nodemasks. Should probably only be called once (in load_server_config or the like).
 */
void an_thread_set_nodemask(void);

int an_thread_numa_set(bool local);

/**
 * @brief Stash up a longjmp-like buffer in the current thread object.
 * @param RET is 0 on the first call, and 1 on abnormal exits.
 */
#define an_thread_setup_unwind(RET) do {			\
		RET = sigsetjmp(current->unwind_target, 0);	\
		an_thread_setup_unwind_slow(RET);		\
	} while (0)

void an_thread_setup_unwind_slow(int status);

/**
 * @brief Clear the longjmp buffer (no-error case).
 */
void an_thread_clear_unwind();

/**
 * @brief Setup a handler to try and unwind on error.
 */
void an_thread_setup_soft_error_handler();

/**
 * @brief Set the global limit for consecutive soft errors (number of
 * [hopefully] succesfully handled errors without any intervening
 * error-free call to an_thread_clear_unwind).  When that limit is
 * reached, we revert to normal error handling (i.e., coring).
 */
void an_thread_max_consecutive_soft_errors(unsigned int);

/**
 * @brief Set the global limit for lifetime soft errors.  We also dump core
 * whenever that limit is reached.
 */
void an_thread_max_soft_errors(unsigned int);

/**
 * @return true if consecutive and total soft errors limits are non-zero.
 */
bool an_thread_soft_errors_enabled();

/**
 * @brief Max count of consecutive soft errors.
 * @param clear atomically reset the max to 0 if clear is true.
 */
unsigned int an_thread_consecutive_soft_errors(bool);

/**
 * @brief Current total count of soft errors.
 * @param clear atomically reset the count to 0 if clear is true.
 */
unsigned int an_thread_soft_errors(bool clear);

/**
 * Enable heartbeats for the specified thread.
 */
void an_thread_heartbeat(struct an_thread *);

/**
 * Enables an_thread debug handlers in the supplied httpd.
 * This currently includes:
 *   - /control/threads/list
 */
void an_thread_handler_http_enable(struct evhttp *);

/**
 * @brief set ability to use epoch allocator on a thread.
 */
AN_CC_UNUSED static void
an_thread_set_epoch_malloc(struct an_thread *thread, bool use_epoch_malloc)
{

	if (thread != NULL) {
		thread->malloc_state.use_epoch_malloc = use_epoch_malloc;
		thread->malloc_state.allow_epoch_malloc = use_epoch_malloc;
	}

	return;
}

int an_thread_set_bonding_interface(const char *);

/**
 * Initializes the an_thread sub-system.
 */
void an_thread_init(void);

extern an_smr_t global_smr;

#if defined(USE_THREADS)
#define AN_THREAD_SLOT (an_thread_current_id)
#else
#define AN_THREAD_SLOT 0
#endif

/**
 * @brief Return currently memory used percentage.
 * It returns 0 if an_thread hasn't had a chance to do its calculation during startup.
 * Currently it recalculates the ratio using an memory monitor,
 * which queries memory info every 30 seconds
 */
double an_thread_get_rss_anon_ratio(void);

#endif /* _AN_THREAD_H */
