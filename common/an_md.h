#ifndef _AN_MD_H
#define _AN_MD_H

#include <evhttp.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef __x86_64__
#error "Unsupported platform."
#endif

typedef uint64_t an_md_rdtsc_t(void);
an_md_rdtsc_t *an_md_probe_rdtsc(const char **, bool fast);
/* Return the invariant tick frequency in Hz, 0 on failure. */
unsigned long long an_md_scale_invariant_rdtsc(void);

void an_md_probe(void);
void an_md_handler_http_enable(struct evhttp *);

/*
 * Public functions.
 */

extern an_md_rdtsc_t *an_md_rdtsc;
/* Like an_md_rdtsc, but sloppy (e.g., RDTSC instead of RDTSCP). */
extern an_md_rdtsc_t *an_md_rdtsc_fast;

/**
 * Returns number of microseconds from ticks.
 */
unsigned long long an_md_us_to_rdtsc(unsigned long long);

/**
 * Returns number of microseconds in a tick interval.
 * Provides nanosecond granularity.
 */
double an_md_rdtsc_scale(uint64_t);

/**
 * Returns number of microseconds in a tick interval
 * in integer format. The number of microseconds is
 * rounded up to the nearest microsecond.
 *
 * For example,
 *   0.50 microseconds -> 1 microsecond
 *   0.49 microseconds -> 0 microseconds
 */
unsigned long long an_md_rdtsc_to_us(uint64_t);

#endif /* _AN_MD_H */
