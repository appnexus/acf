#ifndef _AN_SWLOCK_H
#define _AN_SWLOCK_H

#include <assert.h>
#include <ck_swlock.h>
#include <stdbool.h>

#include "common/an_latency.h"
#include "common/an_thread.h"
#include "common/util.h"

struct an_swlock {
	ck_swlock_t swlock;
	an_latency_context_t read_acquire_latency;
	an_latency_context_t write_acquire_latency;
	an_latency_context_t read_hold_time;
	an_latency_context_t write_hold_time;
};
typedef struct an_swlock an_swlock_t;

static void an_swlock_read_unlock_impl(an_swlock_t *mutex);

static inline void
an_swlock_read_lock_impl(an_swlock_t *mutex)
{

	ck_swlock_read_lock(&mutex->swlock);
	an_thread_push(an_swlock_read_unlock_impl, mutex);
	return;
}

static inline void
an_swlock_read_unlock_impl(an_swlock_t *mutex)
{

	an_thread_pop(an_swlock_read_unlock_impl, mutex);
	ck_swlock_read_unlock(&mutex->swlock);
	return;
}

static inline bool
an_swlock_write_trylock_impl(an_swlock_t *mutex)
{

	if (ck_swlock_write_trylock(&mutex->swlock) == true) {
		an_thread_push_poison(ck_swlock_write_unlock);
		return true;
	}

	return false;
}

static inline void
an_swlock_write_lock_impl(an_swlock_t *mutex)
{

	ck_swlock_write_lock(&mutex->swlock);
	an_thread_push_poison(ck_swlock_write_unlock);
	return;
}

static inline bool
an_swlock_write_locked(an_swlock_t *mutex)
{
	return ck_swlock_locked_writer(&mutex->swlock);
}

static inline void
an_swlock_write_unlock_impl(an_swlock_t *mutex)
{

	an_thread_pop_poison(ck_swlock_write_unlock);
	ck_swlock_write_unlock(&mutex->swlock);
	return;
}

static inline void
an_swlock_write_latch_impl(an_swlock_t *mutex)
{

	ck_swlock_write_latch(&mutex->swlock);
	return;
}

static inline void
an_swlock_write_unlatch_impl(an_swlock_t *mutex)
{

	ck_swlock_write_unlatch(&mutex->swlock);
	return;
}

#ifdef PERF_BUILD
#define AN_SWLOCK_READ_ACQ_START(x) do {					\
	an_swlock_t *_rd_acq_lock = (x);					\
	if (_rd_acq_lock->read_acquire_latency != NULL)				\
		an_latency_track_start(_rd_acq_lock->read_acquire_latency);	\
} while (0)

#define AN_SWLOCK_READ_ACQ_END(x) do {						\
	an_swlock_t *_rd_acq_lock = (x);					\
	uint64_t t = 0;								\
	if (_rd_acq_lock->read_acquire_latency != NULL) {			\
		t = an_md_rdtsc();						\
		an_latency_track_end_at(_rd_acq_lock->read_acquire_latency, t); \
	}									\
	if (_rd_acq_lock->read_hold_time != NULL) {				\
		if (t == 0)							\
			t = an_md_rdtsc();					\
		an_latency_track_start_at(_rd_acq_lock->read_hold_time, t);	\
	}									\
} while (0)

#define AN_SWLOCK_READ_HOLD_END(x) do {						\
	an_swlock_t *_rd_hold_lock = (x);					\
	if (_rd_hold_lock->read_hold_time != NULL)				\
		an_latency_track_end(_rd_hold_lock->read_hold_time);		\
} while (0)

#define AN_SWLOCK_WRITE_ACQ_START(y) do {					\
	an_swlock_t *_wr_acq_lock = (y);					\
	if (_wr_acq_lock->write_acquire_latency != NULL)			\
		an_latency_track_start(_wr_acq_lock->write_acquire_latency);	\
} while (0)

#define AN_SWLOCK_WRITE_ACQ_END(y) do {						\
	an_swlock_t *_wr_acq_lock = (y);					\
	uint64_t t = 0;								\
	if (_wr_acq_lock->write_acquire_latency != NULL) {			\
		t = an_md_rdtsc();						\
		an_latency_track_end_at(_wr_acq_lock->write_acquire_latency, t);\
	}									\
	if (_wr_acq_lock->write_hold_time != NULL) {				\
		if (t == 0)							\
			t = an_md_rdtsc();					\
		an_latency_track_start_at(_wr_acq_lock->write_hold_time, t);	\
	}									\
} while (0)

#define AN_SWLOCK_WRITE_HOLD_END(y) do {				\
	an_swlock_t *_wr_hold_lock = (y);				\
	if (_wr_hold_lock->write_hold_time != NULL)			\
		an_latency_track_end(_wr_hold_lock->write_hold_time);	\
} while (0)
#else
#define AN_SWLOCK_READ_ACQ_START(x) do {			\
	(void)(x);						\
} while (0)

#define AN_SWLOCK_READ_ACQ_END(x) do {				\
	(void)(x);						\
} while (0)

#define AN_SWLOCK_READ_HOLD_END(x) do {				\
	(void)(x);						\
} while (0)

#define AN_SWLOCK_WRITE_ACQ_START(x) do {			\
	(void)(x);						\
} while (0)

#define AN_SWLOCK_WRITE_ACQ_END(x) do {				\
	(void)(x);						\
} while (0)

#define AN_SWLOCK_WRITE_HOLD_END(x) do {			\
	(void)(x);						\
} while (0)

#define AN_SWLOCK_HOLD_END(x) do {				\
	(void)(x);						\
} while (0)
#endif /* !PERF_BUILD */

#define an_swlock_read_lock(x) do {			\
	an_swlock_t *_x = (x);				\
	AN_SWLOCK_READ_ACQ_START(_x);			\
	an_swlock_read_lock_impl(_x);			\
	AN_SWLOCK_READ_ACQ_END(_x);			\
} while (0)

#define an_swlock_read_unlock(x) do {			\
	an_swlock_t *_x = (x);				\
	an_swlock_read_unlock_impl(_x);			\
	AN_SWLOCK_READ_HOLD_END(_x);			\
} while (0)

#define an_swlock_write_lock(y) do {			\
	an_swlock_t *_y = (y);				\
	AN_SWLOCK_WRITE_ACQ_START(_y);			\
	an_swlock_write_lock_impl(_y);			\
	AN_SWLOCK_WRITE_ACQ_END(_y);			\
} while (0)

#define an_swlock_write_unlock(y) do {			\
	an_swlock_t *_y = (y);				\
	an_swlock_write_unlock_impl(_y);		\
	AN_SWLOCK_WRITE_HOLD_END(_y);			\
} while (0)

#define an_swlock_write_latch(y) do {			\
	an_swlock_t *_y = (y);				\
	AN_SWLOCK_WRITE_ACQ_START(_y);			\
	an_swlock_write_latch_impl(_y);			\
	AN_SWLOCK_WRITE_ACQ_END(_y);			\
} while (0)

#define an_swlock_write_unlatch(y) do {			\
	an_swlock_t *_y = (y);				\
	an_swlock_write_unlatch_impl(_y);		\
	AN_SWLOCK_WRITE_HOLD_END(_y);			\
} while (0)

#define an_swlock_init(y) do {				\
	ck_swlock_init(&(y)->swlock);			\
} while (0)

#define an_swlock_write_trylock(y) ({			\
	bool r;						\
	AN_SWLOCK_WRITE_ACQ_START(y);			\
	r = an_swlock_write_trylock_impl(y);		\
	AN_SWLOCK_WRITE_ACQ_END(y);			\
	if (r == false) {				\
		AN_SWLOCK_WRITE_HOLD_END(y);		\
	}						\
	r;						\
})

#ifdef PERF_BUILD
#define an_swlock_latency_init(m, w_a, w_h, r_a, r_h) do {	\
	(m)->write_acquire_latency = w_a;			\
	(m)->write_hold_time = w_h;				\
	(m)->read_acquire_latency = r_a;			\
	(m)->read_hold_time = r_h;				\
} while (0)
#else
#define an_swlock_latency_init(m, w_a, w_h, r_a, r_h) do {	\
	(void)m;						\
	(void)w_a;						\
	(void)w_h;						\
	(void)r_a;						\
	(void)r_h;						\
} while (0)
#endif /* !PERF_BUILD */

#define AN_SWLOCK_INITIALIZER {				\
	.swlock = CK_SWLOCK_INITIALIZER			\
}

#endif /* _AN_SWLOCK_H */
