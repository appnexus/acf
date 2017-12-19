#include <ck_ht.h>
#include <inttypes.h>
#include <stdlib.h>

#include "common/an_malloc.h"
#include "common/an_md.h"
#include "common/an_smr.h"
#include "common/an_syslog.h"
#include "common/an_thread.h"
#include "common/common_types.h"
#include "common/libevent_extras.h"

#define AN_SMR_HT_SIZE 128
#define AN_SMR_MASK (1UL << 63)

/**
 * ifdef implementation noise. sorry.
 */

#ifndef IS_IMPBUS
AN_CC_UNUSED static const bool smr_sections_only_nested = true;
#else
AN_CC_UNUSED static const bool smr_sections_only_nested = false;
#endif

#if defined(DISABLE_SMR)
typedef struct an_smr_entry_impl { } an_smr_entry_impl_t;

#define an_smr_init_impl()

#define an_smr_call_impl(y, z)					\
	do {							\
		(z)(y);						\
	} while (0)

bool
an_smr_is_active(const an_smr_record_t *smr)
{

	return false;
}

static bool
an_smr_poll_impl(void)
{

	return true;
}

static void
an_smr_synchronize_impl(void)
{

	return;
}

#elif defined(USE_EPOCH)

typedef ck_epoch_entry_t an_smr_entry_impl_t;

#define an_smr_init_impl() ck_epoch_init(&global_smr)

#define an_smr_call_impl(y, z)						\
	do {								\
		ck_epoch_call(&current->smr, y, z);	\
	} while (0)

void
an_smr_begin(an_smr_section_t *section)
{

	ck_epoch_begin(&current->smr, section);

	if (smr_sections_only_nested == true) {
		an_thread_push(an_smr_end, section);
	}

	return;
}

void
an_smr_end(an_smr_section_t *section)
{

	if (smr_sections_only_nested == true) {
		an_thread_pop(an_smr_end, section);
	}

	ck_epoch_end(&current->smr, section);
	return;
}

bool
an_smr_is_active(const an_smr_record_t *smr)
{

	if (smr == NULL) {
		return false;
	}

	return (ck_pr_load_uint(&smr->active) != 0);
}

static bool
an_smr_poll_impl(void)
{

	return ck_epoch_poll(&current->smr);
}

static void
an_smr_synchronize_impl(void)
{

	assert(current->smr.active == 0);
	ck_epoch_synchronize(&current->smr);
	return;
}

#elif defined(USE_RTBR)

typedef struct an_rtbr_entry an_smr_entry_impl_t;

#define an_smr_init_impl() do {} while (0)

#define an_smr_call_impl(y, z) an_rtbr_call(y, (void (*)(void *))z, y)

void
an_smr_begin(an_smr_section_t *section)
{

	an_rtbr_begin(section, an_rtbr_prepare(), NULL);

	if (current != NULL && smr_sections_only_nested == true) {
		an_thread_push(an_smr_end, section);
	}

	return;
}

void
an_smr_end(an_smr_section_t *section)
{

	if (current != NULL && smr_sections_only_nested == true) {
		an_thread_pop(an_smr_end, section);
	}

	an_rtbr_end(section);
	return;
}

bool
an_smr_is_active(const an_smr_record_t *smr)
{
	const struct an_rtbr_record *record;

	if (smr == NULL) {
		return false;
	}

	record = *smr;
	if (record == NULL) {
		return false;
	}

	return (an_rtbr_active(record) > 0);
}

static bool
an_smr_poll_impl(void)
{

	if (an_rtbr_id() == 0) {
		return an_rtbr_poll(true);
	}

	return an_rtbr_poll(false);
}

static void
an_smr_synchronize_impl(void)
{

	an_rtbr_synchronize();
	return;
}

#endif

/**
 * End ifdef crap.
 */

struct an_smr_ht {
	an_smr_entry_impl_t smr;
	struct ck_ht ht;
};

struct an_smr_ht_record {
	struct an_smr_ht *smr_ht;
	unsigned int n_pending;
	unsigned int n_dispatch;
	unsigned int n_peak;
};


static AN_MALLOC_DEFINE(ck_ht_smr_token,
	.string = "ck_ht_smr_t",
	.mode = AN_MEMORY_MODE_VARIABLE);

static __thread size_t pause_depth;

static struct an_smr_ht_record all_smr_record[AN_THREAD_LIMIT];

static void *
an_ck_ht_spnc_malloc(size_t size)
{

	return an_malloc_region(ck_ht_smr_token, size);
}

static void
an_ck_ht_spnc_free(void *p, size_t size, bool defer)
{

	(void)defer;
	(void)size;
	an_free(ck_ht_smr_token, p);
	return;
}

/* ck_malloc struct used for spnc safe hashtable */
static struct ck_malloc an_malloc_ck_ht_spnc = {
	.malloc = an_ck_ht_spnc_malloc,
	.free = an_ck_ht_spnc_free
};

static inline bool
smr_is_paused(void)
{

	return pause_depth > 0;
}

void
an_smr_pause(void)
{

	pause_depth++;
	return;
}

void
an_smr_resume(void)
{

	assert(pause_depth > 0);
	if (--pause_depth == 0) {
		an_smr_poll();
	}

	return;
}

size_t
an_smr_get_pause_depth(void)
{

	return pause_depth;
}

unsigned int
an_smr_n_pending(an_smr_record_t *record)
{

	(void)record;
	return all_smr_record[current->id].n_pending;
}

/**
 * @brief actually insert an object for deletion
 *
 * Inserts object and callback into the thread local hashtable
 * and also checks whether the object was already queued for
 * deletion. Thread local hashtables need to be previously
 * initialized with the appropriate ck_malloc struct
 */
static void
an_smr_call_insert_into_hashtable(uintptr_t obj, uintptr_t data)
{
	struct an_smr_ht *smr_ht = all_smr_record[current->id].smr_ht;
	ck_ht_entry_t entry;
	ck_ht_hash_t h;

	/*
	 * Check if value has already been logically deleted by
	 * checking if it's already in the hashtable.
	 */
	ck_ht_hash_direct(&h, &smr_ht->ht, obj);
	ck_ht_entry_key_set_direct(&entry, obj);

	if (ck_ht_get_spmc(&smr_ht->ht, h, &entry) == true) {
		/*
		 * Attempting to free a pointer that previously
		 * existed in the hashtable indicating possible double
		 * free.
		 */
		an_syslog(LOG_ERR, "Double SMR free detected on address %p\n", (void *)obj);
		return;
	}

	/* Insert the value into the hashtable. */
	ck_ht_entry_set_direct(&entry, h, obj, data);
	(void)ck_ht_put_spmc(&smr_ht->ht, h, &entry);
	all_smr_record[current->id].n_pending++;
	return;
}

/**
 * @brief inserts an object into a hashtable for deletion
 *
 * @param obj object being deleted
 * @param func callback function that handles physical deletion
 */
void
an_smr_call_inner(void *obj, void (*func)(void *))
{

	if (func == NULL) {
		return;
	}

	/*
	 * If the object has been deleted, we don't need to defer
	 * and can execute the callback function immediately
	 */
	if (obj == NULL || obj == (void *)(~(uintptr_t)0)) {
		func(obj);
		return;
	}

	an_smr_call_insert_into_hashtable((uintptr_t)obj, (uintptr_t)func);
	return;
}

void
an_smr_free(an_malloc_token_t token, void *obj)
{
	uintptr_t data;

	if (obj == NULL || obj == (void *)(~(uintptr_t)0)) {
		return;
	}

	_Static_assert(sizeof(data) == sizeof(token),
	    "an_malloc_token_t and uintptr_t must be the same size");

	data = ((uintptr_t)token.id << 32) | token.size;

	assert((AN_SMR_MASK & data) == 0);

	data |= AN_SMR_MASK;

	an_smr_call_insert_into_hashtable((uintptr_t)obj, data);
	return;
}

/*
 * Callback executed on the hashtable. Goes through all the keys
 * (object pointers) and executes their values (function pointers) on
 * them. Finally destroys hashtable.
 */
static void
an_smr_ht_callback(an_smr_entry_impl_t *smr)
{
	struct an_smr_ht_record *ht_record = &all_smr_record[current->id];
	struct an_smr_ht *smr_ht = (void*)smr;
	struct ck_ht *ht = &smr_ht->ht;
	ck_ht_iterator_t ht_iterator = CK_HT_ITERATOR_INITIALIZER;
	struct ck_ht_entry *entry;
	unsigned int n_dispatched = 0;

	while (ck_ht_next(ht, &ht_iterator, &entry) == true) {
		void *object;
		uintptr_t value;

		object = (void *)ck_ht_entry_key_direct(entry);
		value = ck_ht_entry_value_direct(entry);

		if ((value & AN_SMR_MASK) == 0) {
			/* We need to invoke the callback function on the object pointer. */
			void (*callback_func)(void *);

			callback_func = (void (*)(void *))value;
			callback_func(object);
		} else {
			/* Value is a token and we need to free object */
			an_malloc_token_t token;

			/* Unset the high bit */
			value &= ~AN_SMR_MASK;

			memset(&token, 0, sizeof(token));

			token.size = (uint32_t)value;
			token.id = (uint32_t)(value >> 32);

			an_free(token, object);
		}

		n_dispatched++;
	}

	if (ht_record->n_pending > ht_record->n_peak) {
		ht_record->n_peak = ht_record->n_pending;
	}

	ht_record->n_pending -= n_dispatched;
	ht_record->n_dispatch += n_dispatched;

	ck_ht_destroy(ht);
	an_ck_ht_spnc_free(smr_ht, sizeof(struct an_smr_ht), false);
	return;
}

/*
 * Initializes a hash table that is safe for thread local usage
 * spnc semantics to be used to store SMR callbacks
 */
static void
an_smr_spnc_ht_init(struct an_smr_ht_record *record)
{

	record->smr_ht = an_ck_ht_spnc_malloc(sizeof(struct an_smr_ht));
	memset(record->smr_ht, 0, sizeof(*record->smr_ht));

	if (ck_ht_init(&record->smr_ht->ht, CK_HT_MODE_DIRECT, NULL,
	    &an_malloc_ck_ht_spnc, AN_SMR_HT_SIZE, an_rand64()) == false) {
		abort();
	}

	return;
}

/* Initialize the underlying smr implementation and the hashtables */
void
an_smr_init(void)
{

	an_smr_init_impl();

	for (size_t i = 0; i < ARRAY_SIZE(all_smr_record); i++) {
		struct an_smr_ht_record *record = &all_smr_record[i];

		memset(record, 0, sizeof(*record));
		an_smr_spnc_ht_init(record);
	}

	return;
}

static void
smr_call_ht(void)
{
	struct an_smr_ht_record *record = &all_smr_record[current->id];

	if (ck_ht_count(&record->smr_ht->ht) > 0) {
		an_smr_call_impl(&record->smr_ht->smr, an_smr_ht_callback);
		an_smr_spnc_ht_init(record);
	}

	return;
}

/*
 * First, inserts the thread local hashtable and allocates and
 * initializes a new one for subsequent an_smr_calls. Finally, calls
 * poll, so that any hashtables waiting to be processed can actually
 * be processed. Most likely the hashtable that was just inserted will
 * be processed in the next call to poll.
 */
bool
an_smr_poll(void)
{

	/* If smr has been forcibly paused by the user, do not poll. */
	if (smr_is_paused() == true) {
		an_syslog(LOG_WARNING, "%s: skipping poll due to non-zero pause depth %lu\n",
		    __func__, pause_depth);
		return false;
	}

	smr_call_ht();
	return an_smr_poll_impl();
}

void
an_smr_synchronize(void)
{

	if (smr_is_paused() == true) {
		an_syslog(LOG_WARNING, "%s: skipping synchronize due to non-zero pause depth %lu\n",
		    __func__, pause_depth);
		return;
	}

	smr_call_ht();
	an_smr_synchronize_impl();
	return;
}

/*
 * Lossily check whether an entry is pending destruction. If it returns
 * true then it definitely is. If it returns false it might
 * be or it might not be
 */
bool
an_smr_entry_pending_destruction(void *obj)
{
	struct an_smr_ht *smr_ht = all_smr_record[current->id].smr_ht;
	ck_ht_entry_t entry;
	ck_ht_hash_t h;

	ck_ht_hash_direct(&h, &smr_ht->ht, (uintptr_t)obj);
	ck_ht_entry_key_set_direct(&entry, (uintptr_t) obj);

	if (ck_ht_get_spmc(&smr_ht->ht, h, &entry) == true) {
		return true;
	}

	return false;
}

void
an_smr_handler_http(struct evhttp_request *request, struct an_thread *cursor)
{

#ifndef DISABLE_SMR
	EVBUFFER_ADD_STRING(request->output_buffer, "\t\t\"smr\"             : {\n");
	evbuffer_add_printf(request->output_buffer, "\t\t\t\"pause_depth\"  : %lu,\n",
		an_smr_get_pause_depth());
#if defined(USE_EPOCH)
	evbuffer_add_printf(request->output_buffer, "\t\t\t\"epoch\"        : %u,\n",
		ck_pr_load_uint(&cursor->smr.epoch));
	evbuffer_add_printf(request->output_buffer, "\t\t\t\"refs\"         : "
		"[ { \"value\": %u, \"count\": %u }, { \"value\": %u, \"count\": %u } ],\n",
		ck_pr_load_uint(&cursor->smr.local.bucket[0].epoch),
		ck_pr_load_uint(&cursor->smr.local.bucket[0].count),
		ck_pr_load_uint(&cursor->smr.local.bucket[1].epoch),
		ck_pr_load_uint(&cursor->smr.local.bucket[1].count));
	evbuffer_add_printf(request->output_buffer, "\t\t\t\"active\"       : %u,\n",
		ck_pr_load_uint(&cursor->smr.active));
#elif defined(USE_RTBR)
	const struct an_rtbr_record *record = cursor->smr;

	an_rtbr_poll(false);
	if (record != NULL) {
		uint64_t now = an_md_rdtsc();

		evbuffer_add_printf(request->output_buffer, "\t\t\t\"local_epoch\"  : %.6f,\n",
		    an_md_rdtsc_scale(now - min(now, an_rtbr_local_epoch(record))) * 1e-6);
		evbuffer_add_printf(request->output_buffer, "\t\t\t\"epoch\"        : %.6f,\n",
		    an_md_rdtsc_scale(now - min(now, an_rtbr_epoch())) * 1e-6);
		evbuffer_add_printf(request->output_buffer, "\t\t\t\"active\"       : %"PRIu64",\n",
		    an_rtbr_active(record));
		evbuffer_add_printf(request->output_buffer, "\t\t\t\"reason\"       : \"%s\",\n",
		    an_rtbr_info(record));
	}
#endif
	evbuffer_add_printf(request->output_buffer, "\t\t\t\"pending\"      : %u,\n",
		ck_pr_load_uint(&all_smr_record[cursor->id].n_pending));
	evbuffer_add_printf(request->output_buffer, "\t\t\t\"peak\"         : %u,\n",
		ck_pr_load_uint(&all_smr_record[cursor->id].n_peak));
	evbuffer_add_printf(request->output_buffer, "\t\t\t\"reclamations\" : %u\n",
		ck_pr_load_uint(&all_smr_record[cursor->id].n_dispatch));

	EVBUFFER_ADD_STRING(request->output_buffer, "\t\t}\n");
#endif /* defined(DISABLE_SMR) */

	return;
}
