#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <evhttp.h>
#include <ck_spinlock.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <limits.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include "common/an_array.h"
#include "common/an_handler.h"
#include "common/an_hook.h"
#include "common/util.h"
#include "common/libevent_extras.h"

#if !AN_HOOK_ENABLED

/* dummy stubs */
void
an_hook_init_lib()
{

	return;
}

void
an_hook_handler_http_enable(struct evhttp *httpd)
{

	(void)httpd;
	return;
}

#else /* AN_HOOK_ENABLED */

struct patch_record {
	void *hook;
	void *destination;
	const char *name;
	uint8_t initial_opcode; /* initial value for fallback impl. */
	uint8_t flipped;
	uint8_t padding[6];
};

struct patch_count {
	/* If a hook is unhook, do not increment its activation count. */
	uint64_t activation;
	uint64_t unhook;
};

AN_ARRAY(patch_count, activation);

static ck_spinlock_t patch_lock = CK_SPINLOCK_INITIALIZER;
/* Hook records are in an array. For each hook record, count # of activations and disable calls. */
static AN_ARRAY_INSTANCE(activation) counts;

extern const struct patch_record __start_an_hook_list[], __stop_an_hook_list[];

static void init_all();

static void
lock()
{

	ck_spinlock_lock(&patch_lock);
	if (AN_CC_UNLIKELY(an_array_initialized_activation(&counts) == false)) {
		size_t n = __stop_an_hook_list - __start_an_hook_list;

		an_array_init_activation(&counts, n);
		an_array_grow_to_activation(&counts, n, NULL);
		init_all();
	}

	return;
}

static void
unlock()
{

	ck_spinlock_unlock(&patch_lock);
	return;
}

/* Make sure there's at least one hook point. */
AN_CC_USED static void
dummy()
{

	AN_HOOK_DUMMY(none);
}

#if !AN_HOOK_FALLBACK
#define HOOK_SIZE 5

static void
patch(const struct patch_record *record)
{
	uint8_t *address = record->hook;
	void *dst = record->destination;
	int32_t *target = (int32_t *)(address + 1);
	intptr_t offset = (uint8_t *)dst - (address + 5); /* IP offset from end of instruction. */

	assert((*address == 0xe9 || *address == 0xa9) &&
	    "Target should be a jmp rel or a testl $..., %eax");
	assert((offset == (intptr_t)*target) &&
	    "Target's offset should match with the hook destination.");

	*address = 0xe9; /* jmp rel */
	return;
}

static void
unpatch(const struct patch_record *record)
{
	uint8_t *address = record->hook;
	void *dst = record->destination;
	int32_t *target = (int32_t *)(address + 1);
	intptr_t offset = (uint8_t *)dst - (address + 5);

	assert((*address == 0xe9 || *address == 0xa9) &&
	    "Target should be a jmp rel or a testl $..., %eax");
	assert((offset == (intptr_t)*target) &&
	    "Target's offset should match with the hook destination.");

	*address = 0xa9; /* testl $..., %eax */
	return;
}

#else
#define HOOK_SIZE 3 /* REX byte + mov imm8 */

static void
patch(const struct patch_record *record)
{
	uint8_t *address = record->hook;
	uint8_t *field = address + 1;

	/* F4 is HLT, 0x0 is ADD [AL], AL */
	if (field[0] != AN_HOOK_VALUE_ACTIVE && field[0] != AN_HOOK_VALUE_INACTIVE) {
		field++;
	}

	assert((field[0] == AN_HOOK_VALUE_ACTIVE) || (field[0] == AN_HOOK_VALUE_INACTIVE));
	field[0] = AN_HOOK_VALUE_ACTIVE;
	return;
}

static void
unpatch(const struct patch_record *record)
{
	uint8_t *address = record->hook;
	uint8_t *field = address + 1;

	if (field[0] != AN_HOOK_VALUE_ACTIVE && field[0] != AN_HOOK_VALUE_INACTIVE) {
		field++;
	}

	assert((field[0] == AN_HOOK_VALUE_ACTIVE) || (field[0] == AN_HOOK_VALUE_INACTIVE));
	field[0] = AN_HOOK_VALUE_INACTIVE;
	return;
}
#endif

static void
default_patch(const struct patch_record *record)
{
	struct patch_count *count;
	size_t i, n;

	count = counts.values;
	n = an_array_length_activation(&counts);
	i = record - __start_an_hook_list;

	assert(i < (size_t)n && "Hook out of bounds?!");

	switch (record->initial_opcode) {
	case AN_HOOK_VALUE_ACTIVE:
		count[i].activation = (record->flipped != 0) ? 0 : 1;
		patch(record);
		break;
	case AN_HOOK_VALUE_INACTIVE:
		count[i].activation = (record->flipped != 0) ? 1 : 0;
		unpatch(record);
		break;
	default:
		assert((record->initial_opcode == AN_HOOK_VALUE_ACTIVE ||
		    record->initial_opcode == AN_HOOK_VALUE_INACTIVE) &&
		    "Initial opcode/value must be ACTIVE or INACTIVE (JMP REL32 or TEST / 0xF4 or 0)");
	}

	__builtin___clear_cache(record->hook, (char *)record->hook + HOOK_SIZE);
	return;
}

static void
activate(const struct patch_record *record)
{

	if (record->flipped != 0) {
		unpatch(record);
	} else {
		patch(record);
	}

	__builtin___clear_cache(record->hook, (char *)record->hook + HOOK_SIZE);
	return;
}

static void
deactivate(const struct patch_record *record)
{

	if (record->flipped != 0) {
		patch(record);
	} else {
		unpatch(record);
	}

	__builtin___clear_cache(record->hook, (char *)record->hook + HOOK_SIZE);
	return;
}

static void
amortize(const struct patch_record **records, size_t n, void (*cb)(const struct patch_record *))
{
	uintptr_t first_page = UINTPTR_MAX;
	uintptr_t last_page = 0;
	uintptr_t page_size;
	size_t i, section_begin = 0;

	page_size = sysconf(_SC_PAGESIZE);

#define PATCH() do {							\
		if (section_begin < i) {				\
			mprotect((void *)(first_page * page_size),	\
			    (1 + last_page - first_page) * page_size,	\
			    PROT_READ | PROT_WRITE | PROT_EXEC);	\
			for (size_t j = section_begin; j < i; j++) {	\
				cb(records[j]);				\
			}						\
									\
			mprotect((void *)(first_page * page_size),	\
			    (1 + last_page - first_page) * page_size,	\
			    PROT_READ | PROT_EXEC);			\
		}							\
	} while (0)

	for (i = 0; i < n; i++) {
		const struct patch_record *record = records[i];
		uintptr_t begin_page = (uintptr_t)record->hook / page_size;
		uintptr_t end_page = ((uintptr_t)record->hook + HOOK_SIZE - 1) / page_size;

		if ((first_page - 1) <= begin_page ||
		    end_page <= (last_page + 1)) {
			first_page = min(first_page, begin_page);
			last_page = max(last_page, end_page);
		} else {
			PATCH();
			section_begin = i;
			first_page = begin_page;
			last_page = end_page;
		}
	}

	PATCH();
	return;
}

static int
find_records(const char *pattern, an_array_t *acc)
{
	regex_t regex;
	size_t n = __stop_an_hook_list - __start_an_hook_list;

	an_array_init(acc, 16);
	if (regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
		return -1;
	}

	for (size_t i = 0; i < n; i++) {
		const struct patch_record *record = __start_an_hook_list + i;

		if (regexec(&regex, record->name, 0, NULL, 0) != REG_NOMATCH) {
			an_array_push(acc, (void *)record);
		}
	}

	regfree(&regex);
	return 0;
}

static int
find_records_kind(const void **start, const void **end, const char *pattern,
    an_array_t *acc)
{
	regex_t regex;
	size_t n = end - start;

	an_array_init(acc, 16);
	if (pattern != NULL && regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB) != 0) {
		return -1;
	}

	for (size_t i = 0; i < n; i++) {
		const struct patch_record *record = start[i];

		if (pattern == NULL ||
		    regexec(&regex, record->name, 0, NULL, 0) != REG_NOMATCH) {
			an_array_push(acc, (void *)record);
		}
	}

	if (pattern != NULL) {
		regfree(&regex);
	}

	return 0;
}

static int
cmp_patches(const void *x, const void *y)
{
	const struct patch_record * const *a = x;
	const struct patch_record * const *b = y;

	if ((*a)->hook == (*b)->hook) {
		return 0;
	}

	return ((*a)->hook < (*b)->hook) ? -1 : 1;
}

static int
cmp_patches_alpha(const void *x, const void *y)
{
	const struct patch_record * const *a = x;
	const struct patch_record * const *b = y;
	const char *a_name = (*a)->name;
	const char *b_name = (*b)->name;
	const char *a_colon, *b_colon;
	unsigned long long a_line, b_line;
	ssize_t colon_idx;
	int r;

	a_colon = strrchr(a_name, ':');
	b_colon = strrchr(b_name, ':');
	colon_idx = a_colon - a_name;

	/* If the prefixes definitely don't match, just strcmp. */
	if (a_colon == NULL || b_colon == NULL ||
	    (colon_idx != b_colon - b_name)) {
		return strcmp(a_name, b_name);
	}

	r = strncmp(a_name, b_name, colon_idx);
	if (r != 0) {
		return r;
	}

	a_line = strtoull(a_colon + 1, NULL, 10);
	b_line = strtoull(b_colon + 1, NULL, 10);
	if (a_line == b_line) {
		return 0;
	}

	return (a_line < b_line) ? -1 : 1;
}


static void
init_all()
{
	const struct patch_record **records;
	an_array_t acc;
	unsigned int n;
	int r;

	r = find_records("", &acc);
	assert(r == 0 && "an_hook init failed.");

	records = an_array_buffer(&acc, &n);
	qsort(records, n, sizeof(struct patch_record *), cmp_patches);
	amortize(records, n, default_patch);
	an_array_deinit(&acc);
	return;
}

static size_t
activate_all(an_array_t *arr)
{
	an_array_t to_patch;
	const struct patch_record **records;
	unsigned int n;

	records = an_array_buffer(arr, &n);
	qsort(records, n, sizeof(struct patch_record *), cmp_patches);

	an_array_init(&to_patch, n);
	lock();
	for (unsigned int i = 0; i < n; i++) {
		struct patch_count *count;
		const struct patch_record *record = records[i];
		size_t offset = record - __start_an_hook_list;

		count = AN_ARRAY_VALUE(activation, &counts, offset);
		if (count->unhook > 0) {
			continue;
		}

		if (count->activation++ == 0) {
			an_array_push(&to_patch, (void *)record);
		}
	}

	records = an_array_buffer(&to_patch, &n);
	amortize(records, n, activate);
	unlock();

	an_array_deinit(&to_patch);
	return n;
}

static size_t
deactivate_all(an_array_t *acc)
{
	an_array_t to_patch;
	const struct patch_record **records;
	unsigned int n;

	records = an_array_buffer(acc, &n);
	qsort(records, n, sizeof(struct patch_record *), cmp_patches);

	an_array_init(&to_patch, n);
	lock();
	for (unsigned int i = 0; i < n; i++) {
		struct patch_count *count;
		const struct patch_record *record = records[i];
		size_t offset = record - __start_an_hook_list;

		count = AN_ARRAY_VALUE(activation, &counts, offset);
		if (count->activation > 0 && --count->activation == 0) {
			an_array_push(&to_patch, (void *)record);
		}
	}

	records = an_array_buffer(&to_patch, &n);
	amortize(records, n, deactivate);
	unlock();

	an_array_deinit(&to_patch);
	return n;
}

static size_t
rehook_all(an_array_t *arr)
{
	an_array_t to_patch;
	const struct patch_record **records;
	unsigned int n;

	records = an_array_buffer(arr, &n);

	an_array_init(&to_patch, n);
	lock();
	for (unsigned int i = 0; i < n; i++) {
		struct patch_count *count;
		const struct patch_record *record = records[i];
		size_t offset = record - __start_an_hook_list;

		count = AN_ARRAY_VALUE(activation, &counts, offset);
		if (count->unhook > 0) {
			count->unhook--;
		}
	}

	unlock();

	an_array_deinit(&to_patch);
	return n;
}

static size_t
unhook_all(an_array_t *arr)
{
	an_array_t to_patch;
	const struct patch_record **records;
	unsigned int n;

	records = an_array_buffer(arr, &n);

	an_array_init(&to_patch, n);
	lock();
	for (unsigned int i = 0; i < n; i++) {
		struct patch_count *count;
		const struct patch_record *record = records[i];
		size_t offset = record - __start_an_hook_list;

		count = AN_ARRAY_VALUE(activation, &counts, offset);
		count->unhook++;
	}

	unlock();

	an_array_deinit(&to_patch);
	return n;
}

int
an_hook_activate(const char *regex)
{
	an_array_t acc;
	int r;

	r = find_records(regex, &acc);
	if (r != 0) {
		goto out;
	}

	activate_all(&acc);

out:
	an_array_deinit(&acc);
	return r;
}

int
an_hook_deactivate(const char *regex)
{
	an_array_t acc;
	int r;

	r = find_records(regex, &acc);
	if (r != 0) {
		goto out;
	}

	deactivate_all(&acc);

out:
	an_array_deinit(&acc);
	return r;
}

int
an_hook_unhook(const char *regex)
{
	an_array_t acc;
	int r;

	r = find_records(regex, &acc);
	if (r != 0) {
		goto out;
	}

	unhook_all(&acc);

out:
	an_array_deinit(&acc);
	return r;
}

int
an_hook_rehook(const char *regex)
{
	an_array_t acc;
	int r;

	r = find_records(regex, &acc);
	if (r != 0) {
		goto out;
	}

	rehook_all(&acc);

out:
	an_array_deinit(&acc);
	return r;
}

int
an_hook_activate_kind_inner(const void **start, const void **end, const char *regex)
{
	an_array_t acc;
	int r;

	r = find_records_kind(start, end, regex, &acc);
	if (r != 0) {
		goto out;
	}

	activate_all(&acc);

out:
	an_array_deinit(&acc);
	return r;
}

int
an_hook_deactivate_kind_inner(const void **start, const void **end, const char *regex)
{
	an_array_t acc;
	int r;

	r = find_records_kind(start, end, regex, &acc);
	if (r != 0) {
		goto out;
	}

	deactivate_all(&acc);

out:
	an_array_deinit(&acc);
	return r;
}

static void
print_record_names(struct evbuffer *buffer, an_array_t *records)
{
	void **cursor;

	an_array_sort(records, cmp_patches_alpha);
	evbuffer_add_printf(buffer, "    %-100s\tactivations\tunhooks\tsymbol\n", "name");

	AN_ARRAY_FOREACH(records, cursor) {
		Dl_info info;
		const struct patch_record *record = *cursor;
		size_t idx = record - __start_an_hook_list;
		size_t n = an_array_length_activation(&counts);

		assert(idx < n && "Hook out of bounds?!");
		if (dladdr(record->hook, &info) != 0) {
			evbuffer_add_printf(buffer, "    %-100s\t%11"PRIu64"\t%7"PRIu64"\t%s+%lx\n",
			    record->name, AN_ARRAY_VALUE(activation, &counts, idx)->activation,
			    AN_ARRAY_VALUE(activation, &counts, idx)->unhook,
			    info.dli_sname, (char *)record->hook - (char *)info.dli_saddr);
		} else {
			evbuffer_add_printf(buffer, "    %-100s\t%11"PRIu64"\t%7"PRIu64"\tunknown_symbol\n",
			    record->name, AN_ARRAY_VALUE(activation, &counts, idx)->activation,
			    AN_ARRAY_VALUE(activation, &counts, idx)->unhook);
		}
	}

	return;
}

static void
hook_handler(struct evhttp_request *request, void *c)
{
	an_array_t acc;
	struct evkeyvalq kv;
	struct evbuffer *buffer;
	const char *pattern, *uri;
	int r;

	if (request == NULL) {
		return;
	}

	buffer = request->output_buffer;
	uri = evhttp_request_uri(request);
	evhttp_parse_query(uri, &kv);
	pattern = evhttp_find_header(&kv, "pattern");
	if (pattern == NULL) {
		if (c == NULL) {
			pattern = "";
		} else {
			EVBUFFER_ADD_STRING(buffer, "no pattern parameter found.\n");
			goto out;
		}
	}

	r = find_records(pattern, &acc);
	if (r != 0) {
		evbuffer_add_printf(buffer, "Pattern compilation failure for \"%s\".\n", pattern);
		goto out;
	}

	if (c == NULL) {
		evbuffer_add_printf(buffer,
		    "Found %u patch points for pattern \"%s\".\n", an_array_length(&acc), pattern);
	} else {
		evbuffer_add_printf(buffer,
		    "Found %u patch points for pattern \"%s\"... ", an_array_length(&acc), pattern);

		if (c == activate_all) {
			evbuffer_add_printf(buffer, "activated %zd point(s).\n",
			    activate_all(&acc));
		} else if (c == deactivate_all) {
			evbuffer_add_printf(buffer, "deactivated %zd point(s).\n",
			    deactivate_all(&acc));
		} else if (c == unhook_all) {
			evbuffer_add_printf(buffer, "unhooked %zd point(s).\n",
			    unhook_all(&acc));
		} else if (c == rehook_all) {
			evbuffer_add_printf(buffer, "rehooked %zd point(s).\n",
			    rehook_all(&acc));
		} else {
			assert(0 && "Unexpected handler argument");
		}
	}

	print_record_names(buffer, &acc);
	an_array_deinit(&acc);
out:
	evhttp_clear_headers(&kv);
	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

void
an_hook_init_lib()
{

	lock();
	unlock();
	return;
}

void
an_hook_handler_http_enable(struct evhttp *httpd)
{

	an_handler_control_register("hook/activate", hook_handler, activate_all,
	    "Conditionally enable a block of code with a named hook");
	an_handler_control_register("hook/deactivate", hook_handler, deactivate_all,
	    "Conditionally disable a block of code with a named hook");
	an_handler_control_register("hook/unhook", hook_handler, unhook_all,
	    "Conditionally remove a hook from activation");
	an_handler_control_register("hook/rehook", hook_handler, rehook_all,
	    "Conditionally reallow a hook to be activated");
	an_handler_control_register("hook/list", hook_handler, NULL, NULL);
	return;
}

#endif /* AN_HOOK_ENABLED */

/* Utility function for utrace breakpoints. */
void
an_hook_utrace_entry(const char * name, ...)
{

	(void)name;
	return;
}
