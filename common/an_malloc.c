#include <assert.h>
#include <ck_cc.h>
#include <ck_pr.h>
#include <ck_spinlock.h>
#include <dlfcn.h>
#include <errno.h>
#include <evhttp.h>
#include <inttypes.h>
#include <link.h>
#include <malloc.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <modp_ascii.h>
#include <modp_burl.h>

#include "common/an_cc.h"
#include "common/an_handler.h"
#include "common/an_malloc.h"
#include "common/an_string.h"
#include "common/an_syslog.h"
#include "common/an_thread.h"
#include "common/an_time.h"
#include "common/assert_dev.h"
#include "common/json_parser.h"
#include "common/libevent_extras.h"
#include "common/module/generic.h"
#include "common/server_config.h"
#include "common/util.h"

static int an_malloc_epoch_module_load(enum an_generic_action action, struct json_object *json);
static void an_malloc_epoch_metrics_cb(struct evbuffer *buf, double elapsed, bool clear);
static void an_malloc_epoch_set_cache_handler(struct evhttp_request *req, void *arg);

AN_GENERIC_MODULE(
    this, "an_malloc_epoch", an_malloc_epoch_module_load,
    "AN transactional memory allocator module",
    .metrics_cb = an_malloc_epoch_metrics_cb,
    .uris = {
	    { .path =  "/set_cache", .cb = an_malloc_epoch_set_cache_handler, .broadcast = false },
    });

struct an_malloc_stat {
	/* bytes. */
	uint64_t total;
	uint64_t active;
	uint64_t peak;

	/* allocations. */
	uint64_t count_total;
	uint64_t count_active;
	uint64_t count_peak;
};

struct an_malloc_thread {
	struct an_malloc_stat *stat;
	unsigned int length;
	int thread_id;
	LIST_ENTRY(an_malloc_thread) list_entry;
};
LIST_HEAD(an_malloc_thread_list, an_malloc_thread);

struct an_malloc_owner {
	struct an_malloc_stat *stat;
	unsigned int length;
};

struct an_malloc_table {
	struct an_malloc_type *type;
	struct an_malloc_stat *stat;
	struct an_malloc_thread_list threads;
	struct an_malloc_owner *owners;
	unsigned int owner_length;
	unsigned int stat_length;
};

enum an_malloc_http_type {
	AN_MALLOC_HTTP_ACTIVE = 0,
	AN_MALLOC_HTTP_PEAK = 1,
	AN_MALLOC_HTTP_TOTAL = 2,
	AN_MALLOC_HTTP_COUNT_ACTIVE = 3,
	AN_MALLOC_HTTP_COUNT_PEAK = 4,
	AN_MALLOC_HTTP_COUNT_TOTAL = 5,
};

static ck_bitmap_t *epoch_map;

static struct an_malloc_epoch_stats {
	uint64_t epochs_open;
	uint64_t epochs_created;
	uint64_t epochs_destroyed;
	uint64_t epoch_allocations;
	uint64_t non_epoch_allocations;
	uint64_t max_ref_count;
	uint64_t total_ref_count;
	uint64_t epoch_allocated_size;
} epoch_stats;

/* Global type table. */
static struct an_malloc_table global_table;
static pthread_rwlock_t global_table_mutex;
static an_thread_key_t an_malloc_key;

/* string type support */
static AN_MALLOC_DEFINE(an_epoch_alloc_token,
    .string = "an_malloc_epoch",
    .mode = AN_MEMORY_MODE_VARIABLE,
    .use_pool_allocation = false);

static AN_MALLOC_DEFINE(an_epoch_large_alloc_token,
    .string = "an_malloc_epoch_large",
    .mode = AN_MEMORY_MODE_VARIABLE,
    .use_pool_allocation = true);

struct an_malloc_allocator {
	const char *name;
	int (*init)(struct evhttp *);
};
static struct an_malloc_allocator *allocator = NULL;

static int an_malloc_jemalloc(struct evhttp *);
static int an_malloc_glibc(struct evhttp *);

enum an_malloc_allocators {
	AN_MALLOC_ALLOCATOR_DEFAULT = 0,
	AN_MALLOC_ALLOCATOR_JEMALLOC = 1
};

/*
 * These are the list of supported allocators according to
 * a corresponding DSO unique substring. The allocator with
 * the NULL name is the default (glibc) selection.
 */
static struct an_malloc_allocator allocators[] = {
	[AN_MALLOC_ALLOCATOR_DEFAULT] = {
		.name = NULL,
		.init = an_malloc_glibc
	},

	[AN_MALLOC_ALLOCATOR_JEMALLOC] = {
		.name = "libjemalloc.so",
		.init = an_malloc_jemalloc
	}
};

/*
 * See the jemalloc man page. nallocx rounds up to the actual
 * allocation size, and sallocx returns the allocation size for ptr.
 * Flags isn't useful for us.
 */
static size_t (*nallocx)(size_t size, int flags) = NULL;
static size_t (*sallocx)(void *ptr, int flags) = NULL;
static size_t (*mallctl)(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) = NULL;

static void (*an_malloc_jemalloc_stats_print)(void (*)(void *, const char *), void *, const char *);

#define STAT_FOREACH(OP, DST, SRC)					\
	do {								\
		(DST).total OP ck_pr_load_64(&(SRC).total);		\
		(DST).active OP ck_pr_load_64(&(SRC).active);		\
		(DST).peak OP ck_pr_load_64(&(SRC).peak);		\
									\
		(DST).count_total OP ck_pr_load_64(&(SRC).count_total);	\
		(DST).count_active OP ck_pr_load_64(&(SRC).count_active); \
		(DST).count_peak OP ck_pr_load_64(&(SRC).count_peak);	\
	} while (0)

#define STAT_ACTIVE(OP, DST, SRC)					\
	do {								\
		(DST).active OP ck_pr_load_64(&(SRC).active);		\
	} while (0)

static inline size_t
allocation_size(size_t size, void *ptr)
{

	if (nallocx != NULL) {
		return nallocx(size, 0);
	}

	return malloc_usable_size(ptr);
}

static void
an_malloc_handler_flot_generic_http(struct evhttp_request *request, enum an_malloc_http_type type)
{
	struct an_malloc_thread *cursor;
	struct an_malloc_stat stat;
	const char *string;
	struct timeval tv;
	uint64_t value = 0;
	size_t i;

	if (request == NULL)
		return;

	an_gettimeofday(&tv, true);

	evhttp_add_header(request->output_headers, "Access-Control-Allow-Origin", "*");
	evhttp_add_header(request->output_headers, "Content-Type", "application/json");

	EVBUFFER_ADD_STRING(request->output_buffer, "[\n");
	for (i = 1; i < global_table.stat_length; i++) {
		STAT_FOREACH(=, stat, global_table.stat[i]);

		pthread_rwlock_rdlock(&global_table_mutex);
		LIST_FOREACH(cursor, &global_table.threads, list_entry) {
			STAT_FOREACH(+=, stat, cursor->stat[i]);
		}
		string = global_table.type[i].string;
		pthread_rwlock_unlock(&global_table_mutex);

		switch (type) {
		case AN_MALLOC_HTTP_ACTIVE:
			value = stat.active;
			break;
		case AN_MALLOC_HTTP_PEAK:
			value = stat.peak;
			break;
		case AN_MALLOC_HTTP_TOTAL:
			value = stat.total;
			break;
		case AN_MALLOC_HTTP_COUNT_ACTIVE:
			value = stat.count_active;
			break;
		case AN_MALLOC_HTTP_COUNT_PEAK:
			value = stat.count_peak;
			break;
		case AN_MALLOC_HTTP_COUNT_TOTAL:
			value = stat.count_total;
			break;
		}

		EVBUFFER_ADD_STRING(request->output_buffer, "\t{\n");
		evbuffer_add_printf(request->output_buffer,
		    "\t\t\"label\" : \"%s\",\n", string);
		evbuffer_add_printf(request->output_buffer,
		    "\t\t\"data\" : [[%ju, %.4f]]\n",
			(uintmax_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000),
			(double)value / 1048576.0);

		if (i < global_table.stat_length - 1)
			EVBUFFER_ADD_STRING(request->output_buffer, "\t},\n");
		else
			EVBUFFER_ADD_STRING(request->output_buffer, "\t}\n");
	}

	EVBUFFER_ADD_STRING(request->output_buffer, "]\n");
	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

static void
an_malloc_handler_flot_active_http(struct evhttp_request *request, void *c)
{

	an_malloc_handler_flot_generic_http(request, AN_MALLOC_HTTP_ACTIVE);
	return;
}

static void
an_malloc_handler_flot_peak_http(struct evhttp_request *request, void *c)
{

	an_malloc_handler_flot_generic_http(request, AN_MALLOC_HTTP_PEAK);
	return;
}

static void
an_malloc_handler_flot_total_http(struct evhttp_request *request, void *c)
{

	an_malloc_handler_flot_generic_http(request, AN_MALLOC_HTTP_TOTAL);
	return;
}

static void
an_malloc_handler_flot_count_active_http(struct evhttp_request *request, void *c)
{

	an_malloc_handler_flot_generic_http(request, AN_MALLOC_HTTP_COUNT_ACTIVE);
	return;
}

static void
an_malloc_handler_flot_count_peak_http(struct evhttp_request *request, void *c)
{

	an_malloc_handler_flot_generic_http(request, AN_MALLOC_HTTP_COUNT_PEAK);
	return;
}

static void
an_malloc_handler_flot_count_total_http(struct evhttp_request *request, void *c)
{

	an_malloc_handler_flot_generic_http(request, AN_MALLOC_HTTP_COUNT_TOTAL);
	return;
}

static void
an_malloc_stats_print(void *r, const char *output)
{
	struct evhttp_request *request = r;

	evbuffer_add_printf(request->output_buffer, "%s", output);
	return;
}

static void
an_malloc_handler_allocator_jemalloc_http(struct evhttp_request *request, void *c)
{

	if (request == NULL)
		return;

	evhttp_add_header(request->output_headers, "Content-Type", "text/plain");
	an_malloc_jemalloc_stats_print(an_malloc_stats_print, request, "g");

	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

static void
an_malloc_handler_allocator_glibc_http(struct evhttp_request *request, void *c)
{
	struct mallinfo m = mallinfo();

	evhttp_add_header(request->output_headers, "Content-Type", "text/plain");
	evbuffer_add_printf(request->output_buffer, "Allocator Statistics\n"
						    "============================================\n"
						    " Total memory: %12ju\n"
						    "    |--- mmap: %12d (%d chunks)\n"
						    "    '--- sbrk: %12d\n\n"
						    "Unused memory: %12d\n",
				(uintmax_t)m.arena + (uintmax_t)m.hblkhd,
				m.hblkhd, m.hblks, m.arena, m.fordblks);
	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

static int
an_malloc_glibc(struct evhttp *httpd)
{

	if (httpd != NULL)
		an_handler_control_register("memory/allocator", an_malloc_handler_allocator_glibc_http, NULL, NULL);

	return 0;
}

static int
an_malloc_jemalloc(struct evhttp *httpd)
{

	an_malloc_jemalloc_stats_print = dlsym(RTLD_DEFAULT, "malloc_stats_print");
	if (an_malloc_jemalloc_stats_print == NULL)
		return -1;

	if (httpd != NULL)
		an_handler_control_register("memory/allocator", an_malloc_handler_allocator_jemalloc_http, NULL, NULL);

	mallctl = dlsym(RTLD_DEFAULT, "mallctl");

	return 0;
}

static int
an_malloc_detect_dlpi(struct dl_phdr_info *info, size_t size, void *data)
{
	size_t i;

	if (allocator != NULL) {
		return allocator->init(data);
	}

	for (i = 1; i < ARRAY_SIZE(allocators); i++) {
		if (info->dlpi_name == NULL || *info->dlpi_name == '\0')
			return 0;

		if (strstr(info->dlpi_name, allocators[i].name) != NULL) {
			allocator = &allocators[i];
			break;
		}
	}

	if (i >= ARRAY_SIZE(allocators)) {
		/* Use the default allocator */
		allocator = &allocators[AN_MALLOC_ALLOCATOR_DEFAULT];
	}

	return allocator->init(data);
}

int
an_malloc_detect(struct evhttp *httpd)
{

	return dl_iterate_phdr(an_malloc_detect_dlpi, httpd);
}

static uint64_t
get_divider(const struct evkeyvalq *kv)
{
	const char *units_str;
	uint64_t divider = 1;

	units_str = evhttp_find_header(kv, "units");
	if (units_str != NULL) {
		switch(units_str[0]) {
		case 'k':
			divider = divider << 10;
			break;
		case 'm':
			divider = divider << 20;
			break;
		case 'g':
			divider = divider << 30;
			break;
		default:
			break;
		}
	}

	return divider;
}

static void
print_memory_row(struct evhttp_request *request, size_t num, const char *string, const struct an_malloc_stat *stat, uint64_t divider)
{

	if (divider > 1) {
		evbuffer_add_printf(request->output_buffer,
		    " %5zu %45s %10.2f %10.2f %25.2f %20" PRIu64 " %20" PRIu64 " %20" PRIu64,
		    num,
		    string,
		    (double)stat->active / divider,
		    (double)stat->peak / divider,
		    (double)stat->total / divider,
		    stat->count_active,
		    stat->count_peak,
		    stat->count_total
		);
	} else {
		evbuffer_add_printf(request->output_buffer,
		    " %5zu %45s %12" PRIu64 " %12" PRIu64 " %25" PRIu64 " %20" PRIu64 " %20" PRIu64 " %20" PRIu64 "\t\t",
		    num,
		    string,
		    stat->active,
		    stat->peak,
		    stat->total,
		    stat->count_active,
		    stat->count_peak,
		    stat->count_total
		);
	}
}

static void
print_memory_summary(struct evhttp_request *request, struct an_malloc_stat *sum, uint64_t divider)
{

	if (divider > 1) {
		evbuffer_add_printf(request->output_buffer,
		    "\n %5s %45s %10.2f %12s %23.2f\n",
		    "TOTAL", " ", (double)sum->active / divider, " ", (double)sum->total / divider);
	} else {
		evbuffer_add_printf(request->output_buffer,
		    "\n %5s %45s %12" PRIu64 " %12s %25" PRIu64 "\n",
		    "TOTAL", " ", sum->active, " ", sum->total);
	}
}

struct an_malloc_handler_row {
	size_t num;
	const char *label;
	struct an_malloc_stat stat;
};

static int
row_comparator(const void *x, const void *y)
{
	const struct an_malloc_handler_row *r1 = x, *r2 = y;
	const struct an_malloc_stat *a = &r1->stat, *b = &r2->stat;

	if (a->active == b->active) {
		return 0;
	}

	return (a->active > b->active) ? -1 : 1;
}

static void
print_stats(struct evhttp_request *request, const struct evkeyvalq *kv,
    struct an_malloc_handler_row *rows, size_t length)
{
	uint64_t divider;
	unsigned int ncols = 1;
	struct an_malloc_stat sum = {
		.total = 0,
		.active = 0
	};
	bool all = false; /* Default to skipping rows that have zero allocation */

	const char *ncols_str = evhttp_find_header(kv, "ncols");
	str2int(ncols_str, (int *)&ncols, 1);
	if (ncols < 1 || ncols > 3) {
		ncols = 1;
	}

	divider = get_divider(kv);

	const char *sort_str = evhttp_find_header(kv, "sort");
	if (sort_str) {
		qsort(rows, length, sizeof(struct an_malloc_handler_row), row_comparator);
	}

	const char *all_str = evhttp_find_header(kv, "all");
	if (all_str) {
		all = true;
	}

	evhttp_add_header(request->output_headers, "Content-Type", "text/plain");
	for (size_t i = 0; i < ncols; i++) {
		evbuffer_add_printf(request->output_buffer,
		    " %5s %45s %12s %12s %25s %20s %20s %20s\t\t",
		    "ID",
		    "Type",
		    "Active",
		    "Peak",
		    "Total",
		    "ActiveObject",
		    "PeakObject",
		    "TotalObject"
		);
	}
	evbuffer_add_printf(request->output_buffer, "\n\n");

	unsigned int nrows = (length / ncols) + (length % ncols ? 1 : 0);
	/* Skipping over the null token */
	for (size_t i = 1; i < nrows; i++) {
		for (size_t j = 0; j < ncols; j++) {
			size_t k = j * nrows + i;
			if (k >= length) {
				break;
			}
			size_t stat_num = rows[k].num;
			const struct an_malloc_stat *stat = &rows[k].stat;
			sum.total += stat->total;
			sum.active += stat->active;
			if (all == false && stat->total == 0 && stat->active == 0) {
				continue;
			}
			print_memory_row(request, stat_num, rows[k].label, stat, divider);
			if (k % ncols == 0) {
				evbuffer_add_printf(request->output_buffer, "\n");
			}
		}
	}

	print_memory_summary(request, &sum, divider);

	return;
}

uint64_t
an_malloc_owner_get_active(uint16_t owner_id)
{
	uint64_t active = 0;

	if ((unsigned int)owner_id >= global_table.owner_length) {
		return 0;
	}

	const struct an_malloc_owner *m = &global_table.owners[owner_id];
	if (m->length == 0) {
		return 0;
	}

	for (size_t i = 0; i < m->length; i++) {
		active += m->stat[i].active;
	}

	return active;
}

static void
an_malloc_handler_owner_http(struct evhttp_request *request, void *c)
{
	struct evkeyvalq kv;
	const char *id_str;
	const struct an_malloc_owner *m;
	uint16_t owner_id = 0;

	if (request == NULL) {
		return;
	}

	evhttp_parse_query(evhttp_request_uri(request), &kv);
	id_str = evhttp_find_header(&kv, "id");
	if (id_str == NULL) {
		goto owner_error;
	}

	owner_id = strtol(id_str, NULL, 0);
	if (owner_id >= global_table.owner_length) {
		goto owner_error;
	}

	m = &global_table.owners[owner_id];
	if (m->length == 0) {
		goto owner_error;
	}

	{
		size_t length = m->length;
		struct an_malloc_handler_row *rows;

		rows = calloc(length, sizeof(struct an_malloc_handler_row));
		for (size_t i = 1; i < length; i++) {
			rows[i].num = i;
			rows[i].label = global_table.type[i].string;
			rows[i].stat = m->stat[i];
		}

		print_stats(request, &kv, rows, length);
		free(rows);
	}

owner_error:
	evhttp_clear_headers(&kv);
	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

void
an_malloc_allocator_metrics_print(struct evbuffer *buf)
{

	if (allocator == NULL) {
		return;
	}

	if (allocator == &allocators[AN_MALLOC_ALLOCATOR_JEMALLOC] && mallctl != NULL) {
		size_t read;
		size_t len = sizeof(read);

		/*
		 * Refresh cached mallctl statistics. Without this, the statistics could be indefinitely stale.  It
		 * doesn't matter what value we pass for 'epoch'. Any input value triggers a stats refresh and an
		 * increment of the epoch. See the jemalloc man page.
		 */
		uint64_t epoch = 1;
		if (mallctl("epoch", NULL, NULL, &epoch, sizeof(epoch)) != 0) {
			return;
		}

#define MAYBE_PRINT(stat)									     \
		if (mallctl("stats."STRINGIFY(stat), &read, &len, NULL, 0) == 0) {		     \
			evbuffer_add_printf(buf, "app.jemalloc."STRINGIFY(stat)"_avg: %zu\n", read); \
		}

		MAYBE_PRINT(allocated);
		MAYBE_PRINT(active);
		MAYBE_PRINT(mapped);
#undef MAYBE_PRINT
	}

	return;
}

void
an_malloc_token_metrics_print(struct evbuffer *buf)
{
	struct an_malloc_thread *cursor;

	pthread_rwlock_rdlock(&global_table_mutex);
	size_t length = global_table.stat_length;
	struct an_malloc_handler_row *rows = calloc(length, sizeof(struct an_malloc_handler_row));

	/* Skipping over the null token */
	for (size_t i = 1; i < length; i++) {
		rows[i].num = i;
		rows[i].label = global_table.type[i].string;

		STAT_ACTIVE(=, rows[i].stat, global_table.stat[i]);

		LIST_FOREACH(cursor, &global_table.threads, list_entry) {
			STAT_ACTIVE(+=, rows[i].stat, cursor->stat[i]);
		}
	}
	pthread_rwlock_unlock(&global_table_mutex);

	for (size_t i = 1; i < length; i++) {
		/* replace ':' with '-' for metrics' sake */
		char *tmp_label = an_string_dup(rows[i].label);
		an_str_replace_char(tmp_label, ':', '_');
		evbuffer_add_printf(buf, "an_malloc.%s_avg: %lu\n", tmp_label, rows[i].stat.active);
		an_string_free(tmp_label);
	}

	free(rows);

	return;
}

static void
an_malloc_handler_http(struct evhttp_request *request, void *c)
{
	struct an_malloc_thread *cursor;
	struct evkeyvalq kv;
	const char *thread_str;
	int thread = -1;

	if (request == NULL) {
		return;
	}

	const char *uri = evhttp_request_uri(request);
	evhttp_parse_query(uri, &kv);

	thread_str = evhttp_find_header(&kv, "thread");
	if (thread_str != NULL) {
		str2int(thread_str, &thread, -1);
	}

	pthread_rwlock_rdlock(&global_table_mutex);
	size_t length = global_table.stat_length;
	struct an_malloc_handler_row *rows = calloc(length, sizeof(struct an_malloc_handler_row));

	for (size_t i = 1; i < length; i++) {
		rows[i].num = i;
		rows[i].label = global_table.type[i].string;

		if (thread == -1) {
			STAT_FOREACH(=, rows[i].stat, global_table.stat[i]);
		}

		LIST_FOREACH(cursor, &global_table.threads, list_entry) {
			if (thread == -1 || thread == cursor->thread_id) {
				STAT_FOREACH(+=, rows[i].stat, cursor->stat[i]);
			}
		}
	}
	pthread_rwlock_unlock(&global_table_mutex);

	print_stats(request, &kv, rows, length);

	free(rows);

	evhttp_clear_headers(&kv);
	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

static void
an_malloc_handler_epoch_http(struct evhttp_request *request, void *c)
{
	struct evkeyvalq kv;
	const char *uri;
	uint64_t cur_epochs_open = ck_pr_load_64(&epoch_stats.epochs_open);
	uint64_t cur_epochs_created = ck_pr_load_64(&epoch_stats.epochs_created);
	uint64_t cur_epochs_destroyed = ck_pr_load_64(&epoch_stats.epochs_destroyed);

	uint64_t cur_epoch_allocations = ck_pr_load_64(&epoch_stats.epoch_allocations);
	uint64_t cur_non_epoch_allocations = ck_pr_load_64(&epoch_stats.non_epoch_allocations);
	uint64_t cur_total_ref_count = ck_pr_load_64(&epoch_stats.total_ref_count);
	uint64_t cur_max_ref_count = ck_pr_load_64(&epoch_stats.max_ref_count);
	uint64_t cur_epoch_allocated_size = ck_pr_load_64(&epoch_stats.epoch_allocated_size);

	uri = evhttp_request_uri(request);
	evhttp_parse_query(uri, &kv);
	evhttp_add_header(request->output_headers, "Content-Type", "text/plain");

	evbuffer_add_printf(request->output_buffer, "epochs_open: %" PRIu64"\n", cur_epochs_open);
	evbuffer_add_printf(request->output_buffer, "epochs_created: %" PRIu64"\n", cur_epochs_created);
	evbuffer_add_printf(request->output_buffer, "epoch_destroyed: %" PRIu64"\n", cur_epochs_destroyed);

	evbuffer_add_printf(request->output_buffer, "total_epoch_allocations: %" PRIu64"\n", cur_epoch_allocations);
	evbuffer_add_printf(request->output_buffer, "total_non_epoch_allocations: %" PRIu64"\n", cur_non_epoch_allocations);
	evbuffer_add_printf(request->output_buffer, "allocations_per_epoch: %f\n", (double)cur_epoch_allocations / cur_epochs_destroyed);

	evbuffer_add_printf(request->output_buffer, "total_epoch_allocated_size: %" PRIu64"\n", cur_epoch_allocated_size);
	evbuffer_add_printf(request->output_buffer, "size_alloced_per_epoch: %f\n", (double)cur_epoch_allocated_size / cur_epochs_destroyed );

	evbuffer_add_printf(request->output_buffer, "avg_refcount: %f\n", (double)cur_total_ref_count / cur_epochs_destroyed);
	evbuffer_add_printf(request->output_buffer, "max_refcount: %" PRIu64"\n", cur_max_ref_count);

	evhttp_clear_headers(&kv);
	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

void
an_malloc_handler_http_enable(struct evhttp *httpd)
{

	if (an_malloc_detect(httpd) == -1)
		abort();

	if (httpd != NULL) {
		an_handler_control_register("memory/list", an_malloc_handler_http, NULL, NULL);
		an_handler_control_register("memory/owner", an_malloc_handler_owner_http, NULL, NULL);
		an_handler_control_register("memory/flot/active", an_malloc_handler_flot_active_http, NULL, NULL);
		an_handler_control_register("memory/flot/peak", an_malloc_handler_flot_peak_http, NULL, NULL);
		an_handler_control_register("memory/flot/total", an_malloc_handler_flot_total_http, NULL, NULL);
		an_handler_control_register("memory/flot/count_active", an_malloc_handler_flot_count_active_http, NULL, NULL);
		an_handler_control_register("memory/flot/count_peak", an_malloc_handler_flot_count_peak_http, NULL, NULL);
		an_handler_control_register("memory/flot/count_total", an_malloc_handler_flot_count_total_http, NULL, NULL);
		an_handler_control_register("memory/epoch", an_malloc_handler_epoch_http, NULL, NULL);
	}

	return;
}

static void
an_malloc_message(const char *s)
{

	an_syslog(LOG_WARNING, "%s", s);
	return;
}

static void
an_malloc_key_destroy(void *p)
{
	struct an_malloc_thread *thread = p;
	uint64_t peak;
	size_t i;

	pthread_rwlock_wrlock(&global_table_mutex);
	LIST_REMOVE(thread, list_entry);
	pthread_rwlock_unlock(&global_table_mutex);

	for (i = 0; i < thread->length; i++) {
		ck_pr_add_64(&global_table.stat[i].total, thread->stat[i].total);
		ck_pr_add_64(&global_table.stat[i].active, thread->stat[i].active);

		peak = ck_pr_load_64(&global_table.stat[i].peak);
		for (;;) {
			if (thread->stat[i].peak <= peak)
				break;

			if (ck_pr_cas_64_value(&global_table.stat[i].peak, peak, thread->stat[i].peak, &peak) == true)
				break;

			ck_pr_stall();
		}
	}

	free(thread->stat);
	free(thread);
	return;
}

#define AN_MALLOC_ROUND_UP_TO_MULTIPLE(N, ROUND_TO_MULTIPLE_OF)		\
	(((N) + (ROUND_TO_MULTIPLE_OF) - 1) & ~((ROUND_TO_MULTIPLE_OF) -1))

/*
 * Malloc's minimal alignment is an ABI guarantee/restriction.
 */
#define AN_MALLOC_GUARANTEED_ALIGNMENT 16ULL

/*
 * Alignment and zero granularity must be powers of two.  Epoch size
 * should be a multiple of both, and probably a power of two as well.
 */
#define AN_MALLOC_EPOCH_ALIGNMENT (1ULL << 20)
#define AN_MALLOC_EPOCH_ZERO_GRANULARITY (1ULL << 12)

#define AN_MALLOC_EPOCH_SIZE (1ULL << 25)
#define AN_MALLOC_EPOCH_AVAILABLE_SIZE					\
	(AN_MALLOC_EPOCH_SIZE -						\
	    AN_MALLOC_ROUND_UP_TO_MULTIPLE(sizeof(struct an_malloc_epoch), \
		AN_MALLOC_GUARANTEED_ALIGNMENT))

/*
 * This is 4x the alignment, so worst-case fragmentation from
 * alignment is 25%.
 *
 * It's also ~1/8th the epoch size, so the worst case space wastage
 * from creating a new epoch without fully using the current one is
 * 12.5%.
 */
#define AN_MALLOC_EPOCH_LARGE_ALLOC (1ULL << 22)

_Static_assert((AN_MALLOC_EPOCH_SIZE % AN_MALLOC_EPOCH_ALIGNMENT == 0),
    "Epoch size should be a multiple of the epoch alignment.");
_Static_assert((AN_MALLOC_EPOCH_AVAILABLE_SIZE > 0),
    "Epoch should have non-empty capacity");
_Static_assert((AN_MALLOC_EPOCH_LARGE_ALLOC <= AN_MALLOC_EPOCH_AVAILABLE_SIZE),
    "Anything bigger than an epoch should be a large allocation.");

/*
 * The maximum number of reclaimed epochs any thread will cache.
 *
 * Lower this value to reduce the per-thread memory usage from epoch caching.
 */
static size_t reclaimed_epochs_limit = 8;

int AN_CC_NO_SANITIZE
an_malloc_init(void)
{
	/*
	 * The next two symbols are created by the linker when the
	 * an_malloc_register_link_list section is non-empty.  Their
	 * *address* correspond to the first byte and one past the
	 * last byte in that section, which is why we declare them as
	 * array of pointers to an_malloc_register_link (the section
	 * is filled with pointers to an_malloc_register_link).
	 */
	extern const struct an_malloc_register_link *__start_an_malloc_register_link_list[];
	extern const struct an_malloc_register_link *__stop_an_malloc_register_link_list[];
	int ret;

	AN_BLOCK_EXECUTE_ONCE_GUARD;
	AN_MALLOC_FORBID_EPOCH_IN_SCOPE;

	if (pthread_rwlock_init(&global_table_mutex, NULL) != 0) {
		return -1;
	}

	ret = an_thread_key_create(&an_malloc_key, an_malloc_key_destroy);
	assert(ret == 0);

	/*
	 * Prevent uninitialized tokens from being able to use
	 * an_malloc, reducing accounting errors.
	 * This is kosher because every registration does a realloc.
	 */
	global_table.stat_length = 1;

	for (const struct an_malloc_register_link **link_ptr = __start_an_malloc_register_link_list;
	     link_ptr < __stop_an_malloc_register_link_list;
	     link_ptr++) {
		const struct an_malloc_register_link *link = *link_ptr;
		an_malloc_token_t token;

		if (link == NULL) {
			continue;
		}

		token = an_malloc_register(link->type);
		if (link->token != NULL) {
			memcpy(link->token, &token, sizeof(*link->token));
		}
	}

	/*
	 * x86_64 only uses 48 VMA bits, so only allocate space for a
	 * bitmap of 2^48 bits / ALIGNMENT instead of
	 * 2^64 bits / ALIGNMENT. This reduces the size of the bitmap
	 * by a factor of 64K.  For our current alignment of 1MB, the
	 * space requirements go from 2TB to 32MB, which is nice.
	 */
	size_t n_pools = (1ULL << CK_MD_VMA_BITS) / AN_MALLOC_EPOCH_ALIGNMENT;

	/* This will be 16 MB */
	epoch_map = malloc(ck_bitmap_size(n_pools));
	ck_bitmap_init(epoch_map, n_pools, false);

	memset(&epoch_stats, 0, sizeof(struct an_malloc_epoch_stats));

	nallocx = dlsym(RTLD_DEFAULT, "nallocx");
	sallocx = dlsym(RTLD_DEFAULT, "sallocx");
	if (nallocx == NULL || sallocx == NULL) {
		nallocx = NULL;
		sallocx = NULL;
	}

	return 0;
}

an_malloc_token_t
an_malloc_register(an_malloc_type_t *type)
{
	struct an_malloc_type copy;
	struct an_malloc_type *entry;
	struct an_malloc_stat *stat;
	an_malloc_token_t ret;

	assert(global_table.stat_length < INT_MAX);

	/* Fixed size types must have a size associated with them. */
	if (type->mode == AN_MEMORY_MODE_FIXED) {
		assert(type->size > 0);
	}

	/* The entry does not exist so create a new entry for the type map. */
	type->id = ck_pr_faa_uint(&global_table.stat_length, 1);
	assert(type->id > 0 && type->id <= INT_MAX);

	entry = realloc(global_table.type, (type->id + 1) * sizeof(struct an_malloc_type));
	assert(entry != NULL);

	stat = realloc(global_table.stat, (type->id + 1) * sizeof(struct an_malloc_stat));
	assert(stat != NULL);

	memset(stat + type->id, 0, sizeof(*stat));

	copy = *type;
	copy.string = strdup(type->string);
	assert(copy.string != NULL);

	global_table.stat = stat;
	global_table.type = entry;
	global_table.type[type->id] = copy;

	memset(&ret, 0, sizeof(ret));
	ret.id = type->id;
	if (type->mode != AN_MEMORY_MODE_VARIABLE) {
		ret.size = type->size;
	}

	if (type->use_pool_allocation == true) {
		ret.use_pool_allocation = 1;
	}

	return ret;
}

static struct an_malloc_stat *
an_malloc_owner_get(unsigned int type_id, uint16_t owner_id)
{
	size_t length;
	struct an_malloc_owner *m;

	if (owner_id >= global_table.owner_length) {
		const unsigned int resize_len = 16; /* Allocate a bit extra to minimize reallocs */
		unsigned int newlen = owner_id + resize_len;
		unsigned int delta = newlen - global_table.owner_length;
		global_table.owners = realloc(global_table.owners, newlen * sizeof(struct an_malloc_owner));
		memset(global_table.owners + global_table.owner_length, 0, delta * sizeof(struct an_malloc_owner));
		global_table.owner_length = newlen;
	}

	m = &global_table.owners[owner_id];
	length = ck_pr_load_uint(&global_table.stat_length);

	if (length != m->length) {
		struct an_malloc_stat *stat = realloc(m->stat, length * sizeof(*stat));
		memset(stat + m->length, 0, sizeof(*stat) * (length - m->length));
		m->length = length;
		m->stat = stat;
	}

	return m->stat + type_id;
}

static struct an_malloc_stat *
an_malloc_thread_get(unsigned int type_id, struct an_malloc_thread **t)
{
	struct an_malloc_thread *thread;
	size_t length;

	/* If a type table is not associated with the thread then create one. */
	length = ck_pr_load_uint(&global_table.stat_length);

	if (current != NULL && current->malloc != NULL) {
		thread = current->malloc;
	} else {
		thread = an_thread_getspecific(an_malloc_key);
	}

	if (thread != NULL) {
		struct an_malloc_stat *stat;

		if (length != thread->length) {
			stat = realloc(thread->stat, length * sizeof *stat);
			if (stat == NULL)
				goto global;

			memset(stat + thread->length, 0, sizeof(*stat) * (length - thread->length));
			thread->length = length;
			thread->stat = stat;
		}

		if (t != NULL)
			*t = thread;

		return thread->stat + type_id;
	}

	thread = calloc(1, sizeof *thread);
	if (thread == NULL)
		goto global;

	thread->length = length;
	thread->thread_id = (current == NULL) ? -1 : (int)current->id;
	thread->stat = calloc(length, sizeof(struct an_malloc_stat));
	if (thread->stat == NULL) {
		goto out_free_thread;
	}

	if (an_thread_setspecific(an_malloc_key, thread) != 0) {
		goto out_free_stat;
	}

	pthread_rwlock_wrlock(&global_table_mutex);
	LIST_INSERT_HEAD(&global_table.threads, thread, list_entry);
	pthread_rwlock_unlock(&global_table_mutex);

	if (current != NULL)
		current->malloc = thread;

	if (t != NULL)
		*t = thread;

	return thread->stat + type_id;

global:
	an_malloc_message("Failed to manage thread-local type table, dropping to global allocation");
	return global_table.stat + type_id;
out_free_stat:
	free(thread->stat);
out_free_thread:
	free(thread);
	goto global;
}

static inline size_t
token_size(an_malloc_token_t token)
{

	return token.size;
}

static inline unsigned int
token_id(an_malloc_token_t token)
{

	return token.id;
}

static inline bool
token_use_pool(an_malloc_token_t token)
{

	return token.use_pool_allocation != 0;
}

static void
update_stat(struct an_malloc_stat *stat, int64_t delta, int64_t delta_count)
{

	/*
	 * We are avoiding atomic read-modify-write operations in order to not incur associated
	 * pipeline flushes.
	 */
	ck_pr_store_64(&stat->active, stat->active + delta);
	ck_pr_store_64(&stat->count_active, stat->count_active + delta_count);

	if (delta > 0) {
		ck_pr_store_64(&stat->total, stat->total + delta);
		/*
		 * Once we are no longer single-threaded, this method for peak usage calculation will
		 * no longer be valid. A management thread will be necessary.
		 */
		if (stat->active > stat->peak) {
			ck_pr_store_64(&stat->peak, stat->active);
		}
	}

	if (delta_count > 0) {
		ck_pr_store_64(&stat->count_total, stat->count_total + 1);
		if (stat->count_active > stat->count_peak) {
			ck_pr_store_64(&stat->count_peak, stat->count_active);
		}
	}
}

static void
account_to_token(an_malloc_token_t token, uint16_t owner_id, int64_t delta, uint64_t delta_count)
{
	struct an_malloc_thread *thread = NULL;
	unsigned int id;

	AN_HOOK(perf, disable_malloc_stats) {
		/*
		 * This will disable malloc stats, so if one hits /control/memory/list
		 * the information will likely be wrong. Use with caution.
		 */
		return;
	}

	id = token_id(token);
	assert((id > 0) && "Unitialised an_malloc_token_t");
	update_stat(an_malloc_thread_get(id, &thread), delta, delta_count);

	if (AN_CC_UNLIKELY(owner_id > 0)) {
		assert(current->id == 0);
		update_stat(an_malloc_owner_get(id, owner_id), delta, delta_count);
	}

	return;
}

/**
 * Misc pool allocation machinery.
 */
static inline bool
an_malloc_epoch_allowed()
{
	return current != NULL &&
		current->malloc_state.allow_epoch_malloc == true;
}

static inline bool
an_malloc_should_use_epoch(struct an_malloc_token token, struct an_malloc_keywords keys)
{

	return keys.non_pool == false &&
		token_use_pool(token) &&
		an_malloc_epoch_allowed();
}

bool
an_malloc_set_epoch_usage(bool new_value)
{
	bool current_allow_state = false;

	if (current != NULL) {
		/* If pool alloc is overall disabled, it doesn't make sense to allow. */
		if (current->malloc_state.use_epoch_malloc == false) {
			new_value = false;
		}

		current_allow_state = current->malloc_state.allow_epoch_malloc;
		current->malloc_state.allow_epoch_malloc = new_value;
	}

	return current_allow_state;
}

void
an_malloc_restore_epoch_usage(bool *old_value)
{

	if (current != NULL) {
		current->malloc_state.allow_epoch_malloc = *old_value;
	}

	return;
}

void
an_malloc_restore_state_cleanup(struct an_malloc_state *state)
{

	if (current != NULL) {
		current->malloc_state = *state;
	}

	return;
}

struct an_malloc_state
an_malloc_fas_state(struct an_malloc_state state)
{

	if (current != NULL) {
		struct an_malloc_state s = an_malloc_gather_state();
		current->malloc_state = state;
		return s;
	}

	return AN_MALLOC_STATE_UNKNOWN;
}

void
an_malloc_restore_state(struct an_malloc_state state)
{

	if (current != NULL) {
		current->malloc_state = state;
	}

	return;
}

struct an_malloc_state
an_malloc_gather_state()
{
	struct an_malloc_state state;

	if (current == NULL) {
		state.use_epoch_malloc = false;
		state.allow_epoch_malloc = false;
		return state;
	}

	state = current->malloc_state;

	return state;
}

/* These two are handy for debugging */
AN_CC_USED static struct an_malloc_epoch *
an_malloc_get_oldest_epoch(void)
{

	if (current == NULL) {
		return NULL;
	}

	return STAILQ_FIRST(&current->open_epochs);
}

AN_CC_USED static struct an_malloc_epoch *
an_malloc_get_newest_epoch(void)
{

	if (current == NULL) {
		return NULL;
	}

	return current->epoch;
}

/**
 * Pool-managed bitmap noise.
 */
static inline bool
an_malloc_epoch_allocated(void *ptr)
{

	/*
	 * True iff the pointer was allocated in an epoch.
	 *
	 * XXX: We should switch to using a specific region of VM
	 * space to hold all pool allocations. This will remove the bitmap
	 * lookup.
	 */
	return ck_bitmap_test(epoch_map, (uintptr_t)ptr / AN_MALLOC_EPOCH_ALIGNMENT);
}

static void
an_malloc_set_allocated(void *ptr, size_t bytes)
{
	uintptr_t address = (uintptr_t)ptr;

	assert((address % AN_MALLOC_EPOCH_ALIGNMENT) == 0);
	for (size_t i = 0; i < bytes; i += AN_MALLOC_EPOCH_ALIGNMENT) {
		size_t bit = (address + i) / AN_MALLOC_EPOCH_ALIGNMENT;

		assert(bit < ck_bitmap_bits(epoch_map));
		assert(ck_bitmap_test(epoch_map, bit) == false);
		ck_bitmap_set(epoch_map, bit);
	}

	return;
}

static void
an_malloc_set_deallocated(void *ptr, size_t bytes)
{
	uintptr_t address = (uintptr_t)ptr;

	assert((address % AN_MALLOC_EPOCH_ALIGNMENT) == 0);
	for (size_t i = 0; i < bytes; i += AN_MALLOC_EPOCH_ALIGNMENT) {
		size_t bit = (address + i) / AN_MALLOC_EPOCH_ALIGNMENT;

		assert(bit < ck_bitmap_bits(epoch_map));
		assert(ck_bitmap_test(epoch_map, bit) == true);
		ck_bitmap_reset(epoch_map, bit);
	}

	return;
}

/**
 * Cleanups.
 */
static void
an_malloc_epoch_run_cleanups(struct an_malloc_epoch *epoch)
{

	while (SLIST_EMPTY(&epoch->cleanups) == false) {
		struct an_malloc_epoch_cleanup *head;

		head = SLIST_FIRST(&epoch->cleanups);
		SLIST_REMOVE_HEAD(&epoch->cleanups, linkage);
		head->cb(head->arg);
	}

	return;
}

static void *an_malloc_epoch_alloc(size_t size, bool clear);

void
an_pool_adopt_internal(an_malloc_epoch_cleanup_cb_t *cleanup, void *arg,
    struct an_malloc_epoch *epoch)
{
	struct an_malloc_epoch_cleanup *c;

	/* If this is not the case, we'll leak. */
	assert_crit(an_malloc_epoch_allowed() == true);
	assert(epoch != NULL);

	if (cleanup == NULL) {
		return;
	}

	/*
	 * We have no guarantee that epoch has enough space, but it's
	 * not younger than the current epoch.
	 */
	c = an_malloc_epoch_alloc(sizeof(struct an_malloc_epoch_cleanup), false);
	c->cb = cleanup;
	c->arg = arg;

	SLIST_INSERT_HEAD(&epoch->cleanups, c, linkage);
	return;
}

/**
 * Epoch create/destroy.
 */
static struct an_malloc_epoch *
an_malloc_epoch_create(void)
{
	size_t size_to_alloc;
	struct an_malloc_epoch *epoch;
	void *addr;
	int ret;

	/* Use a reclaimed epoch if we have one. */
	if (STAILQ_EMPTY(&current->reclaimed_epochs)) {
		/*
		 * This MUST be aligned to an EPOCH_ALIGNMENT boundary
		 * so we can perform bookkeeping on this memory
		 * region.
		 */
		ret = posix_memalign(&addr, AN_MALLOC_EPOCH_ALIGNMENT, AN_MALLOC_EPOCH_SIZE);
		assert(ret == 0 && "posix_memalign failed.");
		an_malloc_set_allocated(addr, AN_MALLOC_EPOCH_SIZE);
		account_to_token(an_epoch_alloc_token, 0, AN_MALLOC_EPOCH_SIZE, 1);

		epoch = addr;
	} else {
		epoch = STAILQ_FIRST(&current->reclaimed_epochs);
		STAILQ_REMOVE_HEAD(&current->reclaimed_epochs, linkage);
		current->num_reclaimed_epochs--;
	}

	size_to_alloc = AN_MALLOC_ROUND_UP_TO_MULTIPLE(sizeof(struct an_malloc_epoch),
	    AN_MALLOC_GUARANTEED_ALIGNMENT);
	memset(epoch, 0,
	    min(AN_MALLOC_EPOCH_SIZE,
		AN_MALLOC_ROUND_UP_TO_MULTIPLE(size_to_alloc, AN_MALLOC_EPOCH_ZERO_GRANULARITY)));

	epoch->ref_count = 0;
	epoch->created_timestamp = an_time(true);

	/* We allocate the epoch itself into the pool. */
	epoch->offset = size_to_alloc;

	SLIST_INIT(&epoch->cleanups);

	ck_pr_add_64(&current->num_open_epochs, 1);
	ck_pr_add_64(&epoch_stats.epochs_open, 1);
	ck_pr_add_64(&epoch_stats.epochs_created, 1);

	STAILQ_INSERT_TAIL(&current->open_epochs, epoch, linkage);
	current->epoch = epoch;
	return epoch;
}

static void
an_malloc_epoch_destroy(struct an_malloc_epoch *epoch)
{
	/*
	 * Run the cleanups, since they may have pointers into the
	 * pool we are about to destroy. If we destroy the pool first,
	 * cleanup functions will erroneously call free() (since we
	 * can't find the pool in the bitmap).
	 */
	an_malloc_epoch_run_cleanups(epoch);

	ck_pr_sub_64(&epoch_stats.epochs_open, 1);
	ck_pr_sub_64(&current->num_open_epochs, 1);
	ck_pr_add_64(&epoch_stats.epochs_destroyed, 1);

	ck_pr_add_64(&epoch_stats.epoch_allocations, epoch->num_allocations);
	ck_pr_add_64(&epoch_stats.total_ref_count, epoch->transactions_created);

	ck_pr_add_64(&epoch_stats.epoch_allocated_size, epoch->allocation_size);

	/* Racy, but this is just for testing. */
	uint64_t cur_max_ref_count = ck_pr_load_64(&epoch_stats.max_ref_count);
	if (epoch->transactions_created > cur_max_ref_count) {
		ck_pr_store_64(&epoch_stats.max_ref_count, epoch->transactions_created);
	}

	if (current->num_reclaimed_epochs < reclaimed_epochs_limit) {
		STAILQ_INSERT_HEAD(&current->reclaimed_epochs, epoch, linkage);
		current->num_reclaimed_epochs++;
	} else {
		an_malloc_set_deallocated(epoch, AN_MALLOC_EPOCH_SIZE);
		account_to_token(an_epoch_alloc_token, 0, -AN_MALLOC_EPOCH_SIZE, -1);
		free(epoch);
	}

	return;
}

void
an_malloc_pool_set_reclaimed_epochs_limit(size_t new_cache_size)
{

	reclaimed_epochs_limit = new_cache_size;
	return;
}

/**
 * Transactions.
 */

/*
 * We have to clean up after transaction here (just before opening a
 * new one), rather than when closing transactions because we close a
 * bit too early on impbus.
 *
 * Some code paths send a response *after* closing the
 * transaction/imp_req.  However, they always do so before re-entering
 * the event loop, so it suffices to delay the actual cleanup until we
 * open a new transaction: to open a new transaction, we must accept a
 * new request, and that only happens via the event loop.
 */
static void
an_malloc_cleanup_transactions(void)
{
	/* While oldest's refcount == 0, destroy oldest. */
	struct an_malloc_epoch *oldest = STAILQ_FIRST(&current->open_epochs);

	while (oldest != NULL && oldest->ref_count == 0) {
		STAILQ_REMOVE_HEAD(&current->open_epochs, linkage);
		if (oldest == current->epoch) {
			current->epoch = NULL;
			assert(STAILQ_EMPTY(&current->open_epochs) && "current->epoch not up to date!?");
		}

		an_malloc_epoch_destroy(oldest);
		oldest = STAILQ_FIRST(&current->open_epochs);
	}

	return;
}

struct an_malloc_epoch *
an_malloc_transaction_open(void)
{
	struct an_malloc_epoch *epoch;

	an_malloc_cleanup_transactions();

	epoch = current->epoch;
	if (epoch == NULL) {
		epoch = an_malloc_epoch_create();
		assert(epoch != NULL);
	}

	ck_pr_add_64(&epoch->ref_count, 1);
	epoch->transactions_created++;
	return epoch;
}

struct an_malloc_pool
an_malloc_pool_open(bool enable)
{
	struct an_malloc_pool ret;

	memset(&ret, 0, sizeof(ret));

	ret.state = an_malloc_gather_state();

	an_thread_set_epoch_malloc(current, true);
	ret.epoch = an_malloc_transaction_open();

	an_malloc_set_epoch_usage(enable);
	return ret;
}

/*
 * Only *close* may be called from a different thread, so there's no
 * race with the refcount transitioning from zero to positive.
 */
void
an_malloc_transaction_close(struct an_malloc_epoch *created)
{

	ck_pr_sub_64(&created->ref_count, 1);
	return;
}

void
an_malloc_pool_close(const struct an_malloc_pool *pool)
{

	an_malloc_transaction_close(pool->epoch);
	an_malloc_restore_state(pool->state);
	return;
}

/**
 * Bump allocation.
 */
static void
an_malloc_epoch_bump_slow(struct an_malloc_epoch *epoch, size_t round_size, size_t offset, size_t new_offset, bool clear)
{
	size_t chunk_begin, chunk_end, old_chunk_end;
	size_t clear_begin;

	old_chunk_end = (offset & ~(AN_MALLOC_EPOCH_ZERO_GRANULARITY - 1)) + AN_MALLOC_EPOCH_ZERO_GRANULARITY;
	chunk_begin = new_offset & ~(AN_MALLOC_EPOCH_ZERO_GRANULARITY - 1);
	chunk_end = min(chunk_begin + AN_MALLOC_EPOCH_ZERO_GRANULARITY, AN_MALLOC_EPOCH_SIZE);

	clear_begin = (clear == true) ? old_chunk_end : chunk_begin;
	memset((char *)epoch + clear_begin, 0, chunk_end - clear_begin);
	return;
}

static inline void *
an_malloc_epoch_bump(struct an_malloc_epoch *epoch, size_t round_size, bool clear)
{
	void *ptr;
	size_t offset, new_offset;

	offset = epoch->offset;
	new_offset = offset + round_size;
	if (new_offset > AN_MALLOC_EPOCH_SIZE) {
		return NULL;
	}

	ptr = (void *)((uintptr_t)epoch + offset);
	epoch->offset = new_offset;
	epoch->num_allocations++;
	epoch->allocation_size += round_size;

	/*
	 * The bump pointer passed at least one zeroing bucket
	 * boundary.  Handle that on-demand zeroing.
	 *
	 * We wish to know whether
	 *   offset / ZERO_GRANULARITY != new_offset / ZERO_GRANULARITY,
	 * where ZERO_GRANULARITY is a power of two.
	 *
	 * That is true iff their bitwise representation differs in
	 * high enough bits, bits with weight greater than or equal to
	 * ZERO_GRANULARITY.
	 *
	 * It's totally clear if you draw bitstrings, I swear. :|
	 */
	if (AN_CC_UNLIKELY((offset ^ new_offset) >= AN_MALLOC_EPOCH_ZERO_GRANULARITY)) {
		an_malloc_epoch_bump_slow(epoch, round_size, offset, new_offset, clear);
	}

	if (ptr == NULL) {
		/* Help flow analysis a bit. */
		__builtin_unreachable();
	}

	return ptr;
}

static void
an_malloc_epoch_large_free(void *ptr)
{

	an_malloc_set_deallocated(ptr, 1);
	account_to_token(an_epoch_large_alloc_token, 0, -(ssize_t)malloc_usable_size(ptr), -1);
	free(ptr);
	return;
}

AN_CC_NOINLINE static void *
an_malloc_epoch_alloc_slow(struct an_malloc_epoch *cur_epoch, size_t size, bool clear)
{
	struct an_malloc_epoch *new_epoch;
	void *ptr;

	assert(cur_epoch != NULL);
	if (size >= AN_MALLOC_EPOCH_LARGE_ALLOC) {
		int ret;

		ret = posix_memalign(&ptr, AN_MALLOC_EPOCH_ALIGNMENT, size);
		assert(ret == 0 && "posix_memalign failure");
		if (clear == true) {
			memset(ptr, 0, size);
		}

		account_to_token(an_epoch_large_alloc_token, 0, malloc_usable_size(ptr), 1);
		/* We only need to protect the allocation itself. */
		an_malloc_set_allocated(ptr, 1);
		an_pool_adopt(an_malloc_epoch_large_free, ptr, cur_epoch);
		return ptr;
	}

	/*
	 * We need a new epoch especially for this allocation.  The
	 * refcount will be zero (this transaction will use the
	 * refcount from the older epoch).
	 *
	 * This is okay because cleanup_transactions will roll up all
	 * epochs with refcount zero younger than and contiguous to
	 * the epoch it was created in.
	 */
	new_epoch = an_malloc_epoch_create();
	assert_crit(new_epoch != NULL);
	return an_malloc_epoch_bump(new_epoch, size, clear);
}

static inline void *
an_malloc_epoch_alloc(size_t size, bool clear)
{
	struct an_malloc_epoch *cur_epoch = current->epoch;
	void *ptr;

	/*
	 * Since we don't expose this function, we rely on
	 * an_malloc_transaction_open to create an epoch if
	 * necessary
	 */
	assert_crit(cur_epoch != NULL);

	/* Round up to 16 bytes of allocation. */
	size = max(size, AN_MALLOC_GUARANTEED_ALIGNMENT);
	size = AN_MALLOC_ROUND_UP_TO_MULTIPLE(size, AN_MALLOC_GUARANTEED_ALIGNMENT);

	/* Normal case, we can fit our allocation into our current epoch */
	ptr = an_malloc_epoch_bump(cur_epoch, size, clear);
	if (AN_CC_LIKELY(ptr != NULL)) {
		return ptr;
	}

	return an_malloc_epoch_alloc_slow(cur_epoch, size, clear);
}

static inline void *
an_malloc_epoch_malloc(struct an_malloc_token token, size_t size, struct an_malloc_keywords keys)
{
	void *ptr;
	int alloc = 1;

	if (an_malloc_should_use_epoch(token, keys) == true) {
		return an_malloc_epoch_alloc(size, false);
	}

	size = max(1U, size);
	ptr = malloc(size);
	assert_crit(ptr != NULL && "malloc failure");
	if (token_size(token) == 0) {
		size = allocation_size(size, ptr);
	}

	account_to_token(token, keys.owner_id, size, alloc);
	return ptr;
}

static inline void *
an_malloc_epoch_calloc(struct an_malloc_token token, size_t nmemb, size_t elsize, struct an_malloc_keywords keys)
{
	__uint128_t total = (__uint128_t)nmemb * elsize;
	size_t size = total;
	void *ptr;
	int alloc = 1;

	assert((total >> (CHAR_BIT * sizeof(size_t))) == 0 &&
	    "calloc overflow");
	size = max(1U, size);
	if (an_malloc_should_use_epoch(token, keys)) {
		return an_malloc_epoch_alloc(size, true);
	}

	ptr = calloc(1, size);
	assert_crit(ptr != NULL && "malloc failure");
	if (token_size(token) == 0) {
		size = allocation_size(size, ptr);
	}

	account_to_token(token, keys.owner_id, size, alloc);
	return ptr;
}

static inline void *
an_malloc_epoch_realloc(struct an_malloc_token token, void *old, size_t from, size_t to,
    struct an_malloc_keywords keys)
{
	ssize_t delta;
	void *new;
	int new_object = 0;

	to = max(1U, to);

	if (old == NULL) {
		return an_malloc_epoch_malloc(token, to, keys);
	}

	if (an_malloc_epoch_allocated(old) == true) {
		if (from >= to) {
			return old;
		}

		new = an_malloc_epoch_alloc(to, false);
		memcpy(new, old, min(from, to));
		return new;
	}

	delta = -((sallocx != NULL) ? sallocx(old, 0) : malloc_usable_size(old));
	new = realloc(old, to);
	assert_crit(new != NULL && "malloc failure");
	delta += allocation_size(to, new);

	account_to_token(token, keys.owner_id, delta, new_object);
	return new;
}

/**
 * malloc(3)-like interface!
 */
void *
an_calloc_object_internal(an_malloc_token_t token, struct an_malloc_keywords keys)
{
	size_t size;

	size = token_size(token);
	assert(size != 0 && "must be AN_MEMORY_MODE_FIXED");
	return an_malloc_epoch_calloc(token, 1, size, keys);
}

void *
an_realloc_region_internal(an_malloc_token_t token, void *pointer, size_t from, size_t to, struct an_malloc_keywords keys)
{

	assert(token_size(token) == 0 && "must be AN_MEMORY_MODE_VARIABLE");
	return an_malloc_epoch_realloc(token, pointer, from, to, keys);
}

void *
an_calloc_region_internal(an_malloc_token_t token, size_t number, size_t bytes, struct an_malloc_keywords keys)
{

	assert(token_size(token) == 0 && "must be AN_MEMORY_MODE_VARIABLE");
	return an_malloc_epoch_calloc(token, number, bytes, keys);
}

void *
an_malloc_region_internal(an_malloc_token_t token, size_t bytes, struct an_malloc_keywords keys)
{

	assert(token_size(token) == 0 && "must be AN_MEMORY_MODE_VARIABLE");
	return an_malloc_epoch_malloc(token, bytes, keys);
}

void
an_free_internal(an_malloc_token_t token, void *pointer, struct an_malloc_keywords keys)
{
	size_t size;

	size = token_size(token);
	/* No-op for pool-allocated stuff. */
	if (pointer == NULL || an_malloc_epoch_allocated(pointer) == true) {
		return;
	}

	if (size == 0) {
		if (sallocx != NULL) {
			size = sallocx(pointer, 0);
		} else {
			size = malloc_usable_size(pointer);
		}
	}

	account_to_token(token, keys.owner_id, -(ssize_t)size, -1);
	free(pointer);
	return;
}

/**
 * Misc. generic module functions
 */

static void
an_malloc_epoch_parse_json(struct json_object *json)
{

	if (json == NULL) {
		return;
	}

	json_object_object_foreach(json, key, value) {
		if (strcmp(key, "an_malloc_pool_reclaimed_epochs_limit") == 0) {
			an_malloc_pool_set_reclaimed_epochs_limit(json_object_get_int(value));
			continue;
		}
	}

	return;
}

static int
an_malloc_epoch_module_load(enum an_generic_action action, struct json_object *json)
{

	switch (action) {
	case AN_GENERIC_DESTROY:
		break;

	case AN_GENERIC_LOAD:
		an_malloc_epoch_parse_json(json);
		break;
	}

	return 0;
}

static void
an_malloc_epoch_metrics_cb(struct evbuffer *buf, double elapsed, bool clear)
{

	(void)buf;
	(void)elapsed;
	(void)clear;
	return;
}

static void
an_malloc_epoch_set_cache_handler(struct evhttp_request *req, void *arg)
{
	struct evkeyvalq kv;
	const char *uri;
	const char *limit_str;
	int64_t reclaimed_epoch_cache_limit = -1;

	uri = evhttp_request_uri(req);
	evhttp_parse_query(uri, &kv);

	limit_str = evhttp_find_header(&kv, "reclaimed_epochs_limit");

	if (str_empty(limit_str) == true) {
		evhttp_send_reply(req, HTTP_BADREQUEST, "Missing 'reclaimed_epochs_limit'", NULL);
		goto done;
	}

	reclaimed_epoch_cache_limit = strtol(limit_str, NULL, 0);
	if (reclaimed_epoch_cache_limit < 0) {
		evhttp_send_reply(req, HTTP_BADREQUEST, "Invalid 'reclaimed_epochs_limit' must be non-negative", NULL);
		goto done;
	}

	an_malloc_pool_set_reclaimed_epochs_limit(reclaimed_epoch_cache_limit);
	evhttp_send_reply(req, HTTP_OK, "OK", NULL);

done:
	evhttp_clear_headers(&kv);
	return;
}

/**
 * Misc. an_malloc utilities.
 */
void *
an_malloc_copy_internal(an_malloc_token_t token, const void *old, size_t size, struct an_malloc_keywords keys)
{
	void *copy;

	if (old == NULL) {
		return NULL;
	}

	copy = an_malloc_region_internal(token, size, keys);
	if (copy != NULL) {
		memcpy(copy, old, size);
	}

	return copy;
}

void *
an_malloc_protobuf_alloc(void *allocator_data, size_t size)
{
	an_malloc_token_t token;

	memcpy(&token, &allocator_data, sizeof(an_malloc_token_t));
	return an_malloc_region(token, size);
}

void
an_malloc_protobuf_free(void *allocator_data, void *ptr)
{
	an_malloc_token_t token;

	if (ptr == NULL) {
		return;
	}

	memcpy(&token, &allocator_data, sizeof(an_malloc_token_t));
	an_free(token, ptr);
}

void *
an_acf_malloc(const void *ctx, size_t size, void *return_addr)
{
	const struct an_acf_allocator *allocator = ctx;
	(void)return_addr;

	return an_malloc_epoch_malloc(allocator->an_token, size, (struct an_malloc_keywords){ .dummy = 0 });
}

void *
an_acf_calloc(const void *ctx, size_t nmemb, size_t size, void *return_addr)
{
	const struct an_acf_allocator *allocator = ctx;
	(void)return_addr;
	return an_calloc_region_internal(allocator->an_token, nmemb, size, (struct an_malloc_keywords){ .dummy = 0 });
}

void *
an_acf_realloc(const void *ctx, void *address, size_t size_from, size_t size_to, void *return_addr)
{
	const struct an_acf_allocator *allocator = ctx;
	(void)return_addr;
	(void)size_from;

	return an_realloc_region_internal(allocator->an_token, address, size_from, size_to, (struct an_malloc_keywords){ .dummy = 0 });
}

void
an_acf_free(const void *ctx, void *ptr, void *return_addr)
{
	const struct an_acf_allocator *allocator = ctx;
	(void)return_addr;

	an_free(allocator->an_token, ptr);
}
