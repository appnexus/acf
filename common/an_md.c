#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ck_pr.h>
#include <evhttp.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdio.h>

#include "common/an_handler.h"
#include "common/an_md.h"
#include "common/an_monitor.h"
#include "common/an_syslog.h"
#include "common/an_thread.h"
#include "common/util.h"

#ifndef AN_MD_CALIBRATE_FREQUENCY
#define AN_MD_CALIBRATE_FREQUENCY 300
#endif

enum facilities {
	AN_MD_RDTSC,
	AN_MD_LENGTH
};

struct an_md_table {
	const char *resource;
	const char *implementation;
	const char *notes;
};

static struct an_md_table table[] = {
	[AN_MD_RDTSC] = {
		.resource = "rdtsc",
		.implementation = NULL
	}
};

static double rdtsc_scale;
static double rdtsc_scale_inv; /* 1.0 / rdtsc_scale. */

#define MIN_GETTIME_SAMPLES   50
#define MAX_GETTIME_SAMPLES   100000
#define GETTIME_STDERR_TARGET 850.0

#define MIN_SLEEP_SAMPLES     50
#define MAX_SLEEP_SAMPLES     1000
#define SLEEP_STDERR_TARGET   50.0

#define MIN_STDERR_BOUNDED    5

an_md_rdtsc_t *an_md_rdtsc;
an_md_rdtsc_t *an_md_rdtsc_fast;

double
an_md_rdtsc_scale(uint64_t ticks)
{

	return (double)ticks * ck_pr_load_double(&rdtsc_scale_inv);
}

unsigned long long
an_md_us_to_rdtsc(unsigned long long us)
{

	return us * ck_pr_load_double(&rdtsc_scale);
}

unsigned long long
an_md_rdtsc_to_us(uint64_t ticks)
{

	return (unsigned long long)llround(an_md_rdtsc_scale(ticks));
}

static bool
an_md_rdtsc_calibrate_scale(an_md_rdtsc_t *rdtsc)
{
	int i, j, k;
	uint64_t start, end;
	double gettime_sum, gettime_sum_sq, gettime_mean, gettime_std_err,
	       rdtsc_sum, rdtsc_sum_sq, rdtsc_mean, rdtsc_std_err, diff;

	gettime_sum = 0.0;
	gettime_sum_sq = 0.0;
	gettime_mean = 0.0;
	gettime_std_err = 0.0;
	rdtsc_mean = 0.0;
	rdtsc_std_err = 0.0;

	for (i = 1, j = k = 0; i <= MAX_GETTIME_SAMPLES; ++i) {
		struct timespec ts;

		start = rdtsc();
		clock_gettime(CLOCK_MONOTONIC, &ts);
		end = rdtsc();
		if (end < start)
			continue;
		++j;
		diff = end - start;
		gettime_sum += diff;
		gettime_sum_sq += diff * diff;
		if (i >= MIN_GETTIME_SAMPLES) {
			gettime_mean = gettime_sum / j;
			gettime_std_err = (gettime_sum_sq - gettime_sum * gettime_sum / j) / (j - 1);
			gettime_std_err = sqrt(max(gettime_std_err, 0.0));
			if (gettime_std_err > GETTIME_STDERR_TARGET)
				k = 0;
			else if (++k >= MIN_STDERR_BOUNDED)
				break;
		}
	}

	debug(5, "[rdtsc] clock_gettime() mean %lf ticks, sample "
		 "deviation = %lf, %d iterations\n", gettime_mean,
		 gettime_std_err, j);

	if (gettime_std_err > GETTIME_STDERR_TARGET) {
		an_syslog(LOG_WARNING, "[rdtsc] clock_gettime() timing sample "
					 "deviation = %lf, above target of %lf\n",
					 gettime_std_err, GETTIME_STDERR_TARGET);
	}

	rdtsc_sum = 0.0;
	rdtsc_sum_sq = 0.0;
	for (i = 1, j = k = 0; i <= MAX_SLEEP_SAMPLES; ++i) {
		struct timespec start_ts, end_ts,
			sleep_ts = {
				.tv_sec = 0,
				.tv_nsec = 1000 * 1000 * ((i % 10) + 1),
			};

		start = rdtsc();
		clock_gettime(CLOCK_MONOTONIC, &start_ts);
		nanosleep(&sleep_ts, NULL);
		clock_gettime(CLOCK_MONOTONIC, &end_ts);
		end = rdtsc();
		diff = 1.0e+6 * (end_ts.tv_sec - start_ts.tv_sec)
				+ 1.0e-3 * (end_ts.tv_nsec - start_ts.tv_nsec);
		if (diff <= 0.0)
			continue;
		++j;
		diff = ((double)(end - start) - gettime_mean) / diff;
		rdtsc_sum += diff;
		rdtsc_sum_sq += diff * diff;
		if (j >= MIN_SLEEP_SAMPLES) {
			rdtsc_mean = rdtsc_sum / j;
			rdtsc_std_err = (rdtsc_sum_sq - rdtsc_sum * rdtsc_sum / j) / (j - 1);
			rdtsc_std_err = sqrt(max(rdtsc_std_err, 0.0));
			if (rdtsc_std_err > SLEEP_STDERR_TARGET)
				k = 0;
			else if (++k >= MIN_STDERR_BOUNDED)
				break;
		}
	}

	if (rdtsc_std_err > SLEEP_STDERR_TARGET) {
		an_syslog(LOG_WARNING, "[rdtsc] nanosleep() timing sample "
		    "deviation = %lf, above target of %lf\n",
		        rdtsc_std_err, SLEEP_STDERR_TARGET);

		return false;
	} else if (rdtsc_scale != 0.0) {
		double drift = rdtsc_mean - rdtsc_scale;

		if (drift != 0.0) {
			debug(5, "[rdtsc] drifted %lf\n", drift);
		}
	}

	ck_pr_store_double(&rdtsc_scale, rdtsc_mean);
	ck_pr_store_double(&rdtsc_scale_inv, 1.0 / max(1e-12, rdtsc_mean));

	if (table[AN_MD_RDTSC].notes != NULL) {
		free((char *)table[AN_MD_RDTSC].notes);
		table[AN_MD_RDTSC].notes = NULL;
	}

	an_asprintf((char **)&table[AN_MD_RDTSC].notes, "%lf ticks/us, deviation %lf, %d iterations",
	        rdtsc_scale, rdtsc_std_err, j);

	debug(5, "[rdtsc] %lf ticks/us, deviation %lf, %d iterations\n",
	    rdtsc_scale, rdtsc_std_err, j);

	return true;
}

static uint64_t
an_md_rdtsc_gettime(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void
an_md_rdtsc_calibrate(void)
{

	an_md_rdtsc_calibrate_scale(an_md_rdtsc);
	return;
}

void
an_md_probe(void)
{
	an_md_rdtsc_t *r;

	r = an_md_probe_rdtsc(&table[AN_MD_RDTSC].implementation, false);
	if (r != NULL) {
		an_md_rdtsc_t *fast_r;
		unsigned long long invariant_scale;

		fast_r = an_md_probe_rdtsc(NULL, true);
		invariant_scale = an_md_scale_invariant_rdtsc();
		if (invariant_scale != 0) {
			an_md_rdtsc = r;
			an_md_rdtsc_fast = fast_r;
			ck_pr_store_double(&rdtsc_scale, invariant_scale * 1e-6);
			ck_pr_store_double(&rdtsc_scale_inv, 1.0 / rdtsc_scale);

			an_asprintf((char **)&table[AN_MD_RDTSC].notes,
			    "%lf ticks/us invariant tsc", rdtsc_scale);
			debug(5, "[rdtsc] invariant %lf ticks/us\n", rdtsc_scale);
		} else if (an_md_rdtsc_calibrate_scale(r) == true && current != NULL) {
			an_monitor_t *monitor;

			monitor = an_monitor_create("rdtsc", AN_MD_CALIBRATE_FREQUENCY);
			if (monitor != NULL)
				an_monitor_enable(monitor, AN_MONITOR_HEARTBEAT, an_md_rdtsc_calibrate);

			an_md_rdtsc = r;
			an_md_rdtsc_fast = fast_r;
		}
	}

	if (an_md_rdtsc == NULL) {
		rdtsc_scale = 1000.0;
		rdtsc_scale_inv = 1.0 / rdtsc_scale;
		an_md_rdtsc = an_md_rdtsc_gettime;
		table[AN_MD_RDTSC].implementation = "clock_gettime(CLOCK_MONOTONIC)";
	}

	if (an_md_rdtsc_fast == NULL) {
		an_md_rdtsc_fast = an_md_rdtsc;
	}

	return;
}

static void
an_md_handler_http(struct evhttp_request *request, void *c)
{
	size_t i;

	evbuffer_add_printf(request->output_buffer, "%20s %35s %s\n\n", "Resource", "Implementation", "          Notes");
	for (i = 0; i < AN_MD_LENGTH; i++) {
		evbuffer_add_printf(request->output_buffer, "%20s %35s           %s\n",
			table[i].resource, table[i].implementation, table[i].notes ? table[i].notes : "");
	}

	evhttp_send_reply(request, HTTP_OK, "OK", NULL);
	return;
}

void
an_md_handler_http_enable(struct evhttp *httpd)
{

	an_handler_control_register("md/list", an_md_handler_http, NULL, NULL);
	return;
}
