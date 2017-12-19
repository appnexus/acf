#ifndef RTBR_H
#define RTBR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/queue.h>

struct an_rtbr_entry {
	void (*function)(void *);
	void *argument;
	uint64_t timestamp;
	STAILQ_ENTRY(an_rtbr_entry) next;
};

struct an_rtbr_timestamp {
	uint64_t timestamp;
};

struct an_rtbr_section {
	struct an_rtbr_timestamp timestamp;
	void *cookie;
	TAILQ_ENTRY(an_rtbr_section) next;
	char info[32];
};

struct an_rtbr_record;

struct an_rtbr_iterator {
	size_t slice;
	size_t index;
};

#define AN_RTBR_ITERATOR_INITIALIZER { 0 }

/**
 * A full RTBR sequence is:
 *  1. an_rtbr_prepare for section;
 *  2. allocate section;
 *  3. an_rtbr_begin section
 *  ...
 *  4. an_rtbr_end section.
 */
struct an_rtbr_timestamp an_rtbr_prepare(void);
void an_rtbr_begin(struct an_rtbr_section *, struct an_rtbr_timestamp, const char *info);
void an_rtbr_end(struct an_rtbr_section *);

#define AN_RTBR_SCOPE(NAME, INFO)					\
	struct an_rtbr_section NAME					\
	    __attribute__((__cleanup__(an_rtbr_end))) = {		\
		.cookie = NULL						\
	};								\
	an_rtbr_begin(&NAME, an_rtbr_prepare(), (INFO))

void an_rtbr_call(struct an_rtbr_entry *, void (*fn)(void *), void *);

uint64_t an_rtbr_active(const struct an_rtbr_record *);
const char *an_rtbr_info(const struct an_rtbr_record *);
uint64_t an_rtbr_epoch(void);
uint64_t an_rtbr_local_epoch(const struct an_rtbr_record *);

bool an_rtbr_poll(bool hard);
void an_rtbr_synchronize(void);

/* Force forward progress up to now - latency_ms. */
void an_rtbr_force_progress(uint64_t latency_ms);

uint64_t an_rtbr_record_count(void);
const struct an_rtbr_record *an_rtbr_self(void);
const struct an_rtbr_record *an_rtbr_record_by_id(size_t id);
uint64_t an_rtbr_id(void);
uint64_t an_rtbr_record_id(const struct an_rtbr_record *);
bool an_rtbr_record_oldest_section(struct an_rtbr_section *, const struct an_rtbr_record *);

const struct an_rtbr_record *an_rtbr_iterate(struct an_rtbr_iterator *);
#endif /* !RTBR_H */
