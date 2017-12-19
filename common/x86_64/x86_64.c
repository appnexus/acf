#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "common/an_md.h"
#include "common/util.h"
#include "common/x86_64/cpuid.h"

static uint64_t
an_md_x86_64_i_rdtsc_fast(void)
{
	uint64_t eax, edx;

	__asm__ __volatile__("rdtsc" : "=a"(eax), "=d"(edx));
	return ((edx << 32) | eax);
}

static uint64_t
an_md_x86_64_i_rdtsc(void)
{
	uint64_t eax = 0, edx;

	__asm__ __volatile__("cpuid;"
			     "rdtsc;"
			        : "+a" (eax), "=d" (edx)
				:
				: "%ecx", "%ebx", "memory");

	__asm__ __volatile__("xorl %%eax, %%eax;"
			     "cpuid;"
				:
				:
				: "%eax", "%ebx", "%ecx", "%edx", "memory");

	return ((edx << 32) | eax);
}

static uint64_t
an_md_x86_64_i_rdtscp(void)
{
	uint64_t eax, edx;

	__asm__ __volatile__("rdtscp"
				: "=a" (eax), "=d" (edx)
				:
				: "%ecx", "memory");

	return ((edx << 32) | eax);
}

an_md_rdtsc_t *
an_md_probe_rdtsc(const char **description, bool fast)
{

	/*
	 * Intel processors typically have a synchronized clock across
	 * sockets. This is not the case for AMD, so we cannot rely on
	 * it as a timer source.
	 */
	if (cpuid_vendor() != CPUID_VENDOR_INTEL) {
		return NULL;
	}

	if (fast == true && cpuid_feature(CPUID_FEATURE_TSC) == true) {
		if (description != NULL) {
			*description = "rdtsc_fast";
		}

		return an_md_x86_64_i_rdtsc_fast;
	}

	if (cpuid_feature(CPUID_FEATURE_RDTSCP) == true) {
		if (description != NULL) {
			*description = "rdtscp";
		}

		return an_md_x86_64_i_rdtscp;
	}

	if (cpuid_feature(CPUID_FEATURE_TSC) == true) {
		if (description != NULL) {
			*description = "rdtsc";
		}

		return an_md_x86_64_i_rdtsc;
	}

	return NULL;
}

static unsigned long long
scan_brand_for_frequency(char *buf, const char *suffix, double scale)
{
	size_t freq_index = 0;
	size_t suffix_index = 0;

	for (;;) {
		char *found;

		found = strcasestr(&buf[suffix_index + 1], suffix);
		if (found == NULL) {
			break;
		}

		suffix_index = found - buf;
	}

	if (suffix_index == 0) {
		return 0;
	}

	for (freq_index = suffix_index; freq_index > 0; ) {
		freq_index--;

		if (isblank(buf[freq_index])) {
			break;
		}
	}

	{
		double parse;
		char old;

		errno = 0;
		old = buf[suffix_index];
		buf[suffix_index] = '\0';

		parse = strtod(&buf[freq_index], NULL);

		buf[suffix_index] = old;
		if (errno != 0 || isfinite(parse) == 0 || parse <= 0) {
			return 0;
		}

		return (unsigned long long)(parse * scale);
	}
}

unsigned long long
an_md_scale_invariant_rdtsc(void)
{
	static const struct {
		const char *str;
		double scale;
	} options[] = {
		{ "thz", 1e12 },
		{ "ghz", 1e9 },
		{ "mhz", 1e6 }
	};
	char brand_string[64] = { 0 };
	unsigned long long ret;

	if (cpuid_vendor() != CPUID_VENDOR_INTEL) {
		return 0;
	}

	if (cpuid_feature(CPUID_FEATURE_INV_TSC) == false) {
		return 0;
	}

	ret = cpuid_core_frequency();
	if (ret != 0) {
		return ret;
	}

	cpuid_brand(brand_string, sizeof(brand_string));
	for (size_t i = 0; i < ARRAY_SIZE(options); i++) {
		ret = scan_brand_for_frequency(brand_string, options[i].str, options[i].scale);
		if (ret != 0) {
			break;
		}
	}

	return ret;
}
