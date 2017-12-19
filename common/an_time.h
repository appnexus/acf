#ifndef _AN_TIME_H
#define _AN_TIME_H

#include "an_thread.h"
#include "an_md.h"
#include <stdbool.h>
#include <stdint.h>
#include <event2/event.h>
#include <sys/time.h>

#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600
#define MICROSECONDS_PER_SECOND 1000000ULL
#define HOURS_PER_DAY 24

#if defined(CLOCK_MONOTONIC_RAW)
#define AN_TIME_MONOTONIC_CLOCK CLOCK_MONOTONIC_RAW
#else
#define AN_TIME_MONOTONIC_CLOCK CLOCK_MONOTONIC
#endif

static inline uint64_t
an_time_tvtous(const struct timeval *tv)
{

	return (tv->tv_sec * MICROSECONDS_PER_SECOND) + (uint64_t)tv->tv_usec;
}

static inline void
an_time_ustotv(uint64_t micros, struct timeval *tv)
{
	tv->tv_sec = micros / MICROSECONDS_PER_SECOND;
	tv->tv_usec = micros % MICROSECONDS_PER_SECOND;
}

static inline int
an_gettimeofday(struct timeval *tv, bool cache)
{

	if (current == NULL) {
		return gettimeofday(tv, NULL);
	}

	if (cache == false)
		event_base_update_cache_time(current->event_base);

	return event_base_gettimeofday_cached(current->event_base, tv);
}

static inline uint64_t
an_time_now_us(bool cache)
{
	struct timeval tv;

	if (an_gettimeofday(&tv, cache) == -1)
		return 0;

	return an_time_tvtous(&tv);
}

static inline time_t
an_time(bool cache)
{
	struct timeval tv;

	if (an_gettimeofday(&tv, cache) == -1)
		return 0;

	return tv.tv_sec;
}

static inline uint64_t
an_rdtsc(void)
{

	return an_md_rdtsc();
}

static inline int64_t
an_time_tstons(struct timespec *ts)
{

	return (ts->tv_sec * 1000000000UL) + ts->tv_nsec;
}

static inline int64_t
an_time_monotonic_ns()
{
	struct timespec ts;

	clock_gettime(AN_TIME_MONOTONIC_CLOCK, &ts);

	return an_time_tstons(&ts);
}

#endif /* _AN_TIME_H */
