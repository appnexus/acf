#ifndef RTBR_IMPL_H
#define RTBR_IMPL_H
#include <ck_cc.h>
#include <ck_spinlock.h>
#include <ck_stack.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/queue.h>
#include <sys/types.h>

#include "common/rtbr/rtbr.h"

/*
 * Shouldn't need more than a million cycles to spin up 32
 * cores. Experimentally, it's on the order of ~300K cycles for 4
 * sockets.
 */
#define RTBR_DELAY_TICKS (1ULL << 20)
#define RTBR_HARD_POLL_PERIOD_MS 10ULL /* Let's not try to poll hard more than once every 10 milliseconds. */
#define RTBR_POLL_LOG_PERIOD_MS 1000ULL /* Only syslog about polling every ~second. */
#define SLICE_INITIAL_SIZE 8ULL

struct an_rtbr_tid_info {
	uint64_t tid;
	bool running;
	bool dead;
	uint64_t start_time;
	uint64_t total_time;
};

struct an_rtbr_record {
	struct {
		/* tid + start_time. Odds of ABA seem minuscule (: */
		uint64_t lock[2];
		uint64_t id;
		/* Private values. */
		/* Value is even if inactive, odd if active. */
		uint64_t self_epoch;
		/* Priority list of active sections. Head is oldest. */
		TAILQ_HEAD(, an_rtbr_section) active;
		uint64_t active_count;
		char info[32];
		/* Queue of cleanups. Head is oldest. */
		STAILQ_HEAD(, an_rtbr_entry) limbo;
		uint64_t limbo_count;
		/* Local snapshot of the global epoch (safe to reclaim) */
		uint64_t global_epoch;
		struct ck_stack_entry stack_entry;
	} CK_CC_CACHELINE;
	/* Shared values: written by an_rtbr_poll_hard under lock. */
	/* Latest time we know the record was definitely inactive. */
	uint64_t last_safe;
	uint64_t total_time; /* snapshot of total_time value */
	uint64_t as_of; /* at timestamp = as_of */
	uint64_t last_self_epoch; /* snapshot taken by the poll thread(s). */
} CK_CC_CACHELINE;

CK_STACK_CONTAINER(struct an_rtbr_record, stack_entry, an_rtbr_record_container);

/*
 * A slice is a fixed size array of records with a lock-free bump pointer.
 *
 * We get a growable set of stable records by incrementally allocating
 * slices (geometric growth) and publishing them to rtbr_global.slices
 * with a wait-free CAS sequence.
 */
struct an_rtbr_slice {
	uint64_t allocated_records; /* bump index; updated with FAA, so may exceed n_records. */
	uint64_t n_records; /* slice capacity */
	uint64_t id_offset; /* record id for slice->records[i] is id_offset + i. */
	struct an_rtbr_record records[];
};

struct an_rtbr_global {
	/* Only accessed when creating threads or polling hard. */
	ck_spinlock_t lock;
	uint32_t cur_slice;
	uint64_t last_hard_poll;
	uint64_t record_count;

	/* Read on the fast path. */
	struct {
		uint8_t initialized;
		uint64_t minimal_epoch;
	} CK_CC_CACHELINE;

	/* Read and sometimes written on the fast path. */
	uint64_t global_epoch;
	/* Mostly read-only. */
	struct an_rtbr_slice *slices[32];
	/* Write churn when creating threads. */
	struct ck_stack freelist CK_CC_ALIGN(16);
};

bool an_rtbr_record_acquire(struct an_rtbr_record *, const struct an_rtbr_tid_info *);
struct an_rtbr_record *an_rtbr_get_record(struct an_rtbr_global *);
struct an_rtbr_tid_info an_rtbr_tid_info(pid_t tid);
bool an_rtbr_poll_hard(struct an_rtbr_global *, struct an_rtbr_record *self);
uint64_t an_rtbr_poll_easy(struct an_rtbr_global *);
void an_rtbr_reinit(void);
void an_rtbr_reinit_impl(struct an_rtbr_global *);
#endif /* !RTBR_IMPL_H */
