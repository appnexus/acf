/*
 * Copyright 2009-2011 Samy Al Bahra.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common/x86_64/cpuid.h"

#define CPUID_BIT(b) (1ULL << (b))

struct cpuid {
	uint32_t eax;
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
} __attribute__((packed));

static const char *cpuid_vendor_strings[] = {
	[CPUID_VENDOR_UNKNOWN] = "other",
	[CPUID_VENDOR_AMD]     = "amd",
	[CPUID_VENDOR_INTEL]   = "intel",
	[CPUID_VENDOR_LENGTH]  = NULL
};

static const char *cpuid_feature_strings[] = {
	[CPUID_FEATURE_FPU]        = "FPU",
	[CPUID_FEATURE_VME]        = "VME",
	[CPUID_FEATURE_DE]         = "DE",
	[CPUID_FEATURE_PSE]        = "PSE",
	[CPUID_FEATURE_TSC]        = "TSC",
	[CPUID_FEATURE_MSR]        = "MSR",
	[CPUID_FEATURE_PAE]        = "PAE",
	[CPUID_FEATURE_MCE]        = "MCE",
	[CPUID_FEATURE_CX8]        = "CX8",
	[CPUID_FEATURE_APIC]       = "APIC",
	[CPUID_FEATURE_SEP]        = "SEP",
	[CPUID_FEATURE_MTRR]       = "MTRR",
	[CPUID_FEATURE_PGE]        = "PGE",
	[CPUID_FEATURE_MCA]        = "MCA",
	[CPUID_FEATURE_CMOV]       = "CMOV",
	[CPUID_FEATURE_PAT]        = "PAT",
	[CPUID_FEATURE_PSE36]      = "PSE36",
	[CPUID_FEATURE_PSN]        = "PSN",
	[CPUID_FEATURE_CLFSH]      = "CLFSH",
	[CPUID_FEATURE_DS]         = "DS",
	[CPUID_FEATURE_ACPI]       = "ACPI",
	[CPUID_FEATURE_MMX]        = "MMX",
	[CPUID_FEATURE_FXSR]       = "FXSR",
	[CPUID_FEATURE_SSE]        = "SSE",
	[CPUID_FEATURE_SSE2]       = "SSE2",
	[CPUID_FEATURE_SS]         = "SS",
	[CPUID_FEATURE_HTT]        = "HTT",
	[CPUID_FEATURE_TM]         = "TM",
	[CPUID_FEATURE_PBE]        = "PBE",
	[CPUID_FEATURE_SSE3]       = "SSE3",
	[CPUID_FEATURE_PCLMULQDQ]  = "PCLMULQDQ",
	[CPUID_FEATURE_DTES64]     = "DTES64",
	[CPUID_FEATURE_MONITOR]    = "MONITOR",
	[CPUID_FEATURE_DSCPL]      = "DSCPL",
	[CPUID_FEATURE_VMX]        = "VMX",
	[CPUID_FEATURE_SMX]        = "SMX",
	[CPUID_FEATURE_EST]        = "EST",
	[CPUID_FEATURE_TM2]        = "TM2",
	[CPUID_FEATURE_SSSE3]      = "SSSE3",
	[CPUID_FEATURE_CNXTID]     = "CNXTID",
	[CPUID_FEATURE_FMA]        = "FMA",
	[CPUID_FEATURE_CMPXCHG16B] = "CMPXCHG16B",
	[CPUID_FEATURE_XTPR]       = "XTPR",
	[CPUID_FEATURE_PDCM]       = "PDCM",
	[CPUID_FEATURE_PCID]       = "PCID",
	[CPUID_FEATURE_DCA]        = "DCA",
	[CPUID_FEATURE_SSE41]      = "SSE4.1",
	[CPUID_FEATURE_SSE42]      = "SSE4.2",
	[CPUID_FEATURE_X2APIC]     = "X2APIC",
	[CPUID_FEATURE_MOVBE]      = "MOVBE",
	[CPUID_FEATURE_POPCNT]     = "POPCNT",
	[CPUID_FEATURE_TSCD]       = "TSCD",
	[CPUID_FEATURE_AESNI]      = "AESNI",
	[CPUID_FEATURE_XSAVE]      = "XSAVE",
	[CPUID_FEATURE_OSXSAVE]    = "OSXSAVE",
	[CPUID_FEATURE_AVX]        = "AVX",
	[CPUID_FEATURE_RDTSCP]     = "RDTSCP",
	[CPUID_FEATURE_NX]         = "NX",
	[CPUID_FEATURE_GBP]        = "GBP",
	[CPUID_FEATURE_X86_64]     = "X86_64",
	[CPUID_FEATURE_SYSCALL]    = "SYSCALL",
	[CPUID_FEATURE_INV_TSC]    = "CONSTANT_TSC"
};

static uint8_t cpuid_feature_lut_ecx_edx[] = {
	[CPUID_FEATURE_FPU]        = 1,
	[CPUID_FEATURE_VME]        = 2,
	[CPUID_FEATURE_DE]         = 3,
	[CPUID_FEATURE_PSE]        = 4,
	[CPUID_FEATURE_TSC]        = 5,
	[CPUID_FEATURE_MSR]        = 6,
	[CPUID_FEATURE_PAE]        = 7,
	[CPUID_FEATURE_MCE]        = 8,
	[CPUID_FEATURE_CX8]        = 9,
	[CPUID_FEATURE_APIC]       = 10,
	[CPUID_FEATURE_SEP]        = 12,
	[CPUID_FEATURE_MTRR]       = 13,
	[CPUID_FEATURE_PGE]        = 14,
	[CPUID_FEATURE_MCA]        = 15,
	[CPUID_FEATURE_CMOV]       = 16,
	[CPUID_FEATURE_PAT]        = 17,
	[CPUID_FEATURE_PSE36]      = 18,
	[CPUID_FEATURE_PSN]        = 19,
	[CPUID_FEATURE_CLFSH]      = 20,
	[CPUID_FEATURE_DS]         = 22,
	[CPUID_FEATURE_ACPI]       = 23,
	[CPUID_FEATURE_MMX]        = 24,
	[CPUID_FEATURE_FXSR]       = 25,
	[CPUID_FEATURE_SSE]        = 26,
	[CPUID_FEATURE_SSE2]       = 27,
	[CPUID_FEATURE_SS]         = 28,
	[CPUID_FEATURE_HTT]        = 29,
	[CPUID_FEATURE_TM]         = 30,
	[CPUID_FEATURE_PBE]        = 32,
	[CPUID_FEATURE_SSE3]       = 33,
	[CPUID_FEATURE_PCLMULQDQ]  = 34,
	[CPUID_FEATURE_DTES64]     = 35,
	[CPUID_FEATURE_MONITOR]    = 36,
	[CPUID_FEATURE_DSCPL]      = 37,
	[CPUID_FEATURE_VMX]        = 38,
	[CPUID_FEATURE_SMX]        = 39,
	[CPUID_FEATURE_EST]        = 40,
	[CPUID_FEATURE_TM2]        = 41,
	[CPUID_FEATURE_SSSE3]      = 42,
	[CPUID_FEATURE_CNXTID]     = 43,
	[CPUID_FEATURE_FMA]        = 45,
	[CPUID_FEATURE_CMPXCHG16B] = 46,
	[CPUID_FEATURE_XTPR]       = 47,
	[CPUID_FEATURE_PDCM]       = 48,
	[CPUID_FEATURE_PCID]       = 50,
	[CPUID_FEATURE_DCA]        = 51,
	[CPUID_FEATURE_SSE41]      = 52,
	[CPUID_FEATURE_SSE42]      = 53,
	[CPUID_FEATURE_X2APIC]     = 54,
	[CPUID_FEATURE_MOVBE]      = 55,
	[CPUID_FEATURE_POPCNT]     = 56,
	[CPUID_FEATURE_TSCD]       = 57,
	[CPUID_FEATURE_AESNI]      = 58,
	[CPUID_FEATURE_XSAVE]      = 59,
	[CPUID_FEATURE_OSXSAVE]    = 60,
	[CPUID_FEATURE_AVX]        = 61,
};

static inline void
cpuid(struct cpuid *r, uint32_t eax)
{

	__asm__ __volatile__("cpuid"
				: "=a" (r->eax),
				  "=b" (r->ebx),
				  "=c" (r->ecx),
				  "=d" (r->edx)
				: "a"  (eax)
				: "memory");

	return;
}

void
cpuid_brand(char *buffer, size_t length)
{
	char *p = buffer;
	struct cpuid r;
	uint32_t i;

	cpuid(&r, 0x80000000);
	if ((r.eax & 0x80000000) == 0 ||
	     r.eax < 0x80000004       ||
	     length <= 1)
		goto unsupported;

	length -= 1;
	for (i = 0x80000002; i <= 0x80000004; i++) {
		cpuid(&r, i);
		if (length <= 16) {
			memcpy(p, &r, length);
			p += length;
			break;
		}

		memcpy(p, &r, 16);
		length -= 16;
		p += 16;
	}

	*p = '\0';
	return;

unsupported:
	*buffer = '\0';
	return;
}

void
cpuid_address_size(uint8_t *physical, uint8_t *linear)
{
	struct cpuid r;

	cpuid(&r, 0x80000008);
	*physical = r.eax & 0xFF;
	*linear = (r.eax >> 8) & 0xFF;
	return;
}

bool
cpuid_feature(int feature)
{
	struct cpuid r;
	uint64_t features;

	if (feature < 0 || feature >= CPUID_FEATURE_LENGTH)
		return false;

	if (feature == CPUID_FEATURE_INV_TSC) {
		cpuid(&r, 0x80000007);
		return r.edx & CPUID_BIT(8);
	}

	if (feature >= CPUID_FEATURE_RDTSCP) {
		cpuid(&r, 0x80000001);

		switch (feature) {
		case CPUID_FEATURE_RDTSCP:
			return r.edx & CPUID_BIT(27);
		case CPUID_FEATURE_NX:
			return r.edx & CPUID_BIT(20);
		case CPUID_FEATURE_GBP:
			return r.edx & CPUID_BIT(26);
		case CPUID_FEATURE_X86_64:
			return r.edx & CPUID_BIT(29);
		case CPUID_FEATURE_SYSCALL:
			return r.edx & CPUID_BIT(11);
		}
	}

	if (cpuid_feature_lut_ecx_edx[feature] == 0)
		return false;

	cpuid(&r, 1);
	features = ((uint64_t)r.ecx << 32) | r.edx;

	return CPUID_BIT(cpuid_feature_lut_ecx_edx[feature] - 1) & features;
}

const char *
cpuid_feature_string(int feature)
{

	if (feature < 0 || feature >= CPUID_FEATURE_LENGTH)
		return NULL;

	return cpuid_feature_strings[feature];
}

int
cpuid_vendor(void)
{
	struct cpuid r;
	int vendor;

	cpuid(&r, 0);

	if (r.ebx == 0x756E6547 && r.ecx == 0x6C65746E && r.edx == 0x49656E69)
		vendor = CPUID_VENDOR_INTEL;
	else if (r.ebx == 0x68747541 && r.ecx == 0x444D4163 && r.edx == 0x69746E65)
		vendor = CPUID_VENDOR_AMD;
	else
		vendor = CPUID_VENDOR_UNKNOWN;

	return vendor;
}

const char *
cpuid_vendor_string(int v)
{

	if (v < 0 || v >= CPUID_VENDOR_LENGTH)
		return NULL;

	return cpuid_vendor_strings[v];
}

unsigned long long
cpuid_core_frequency(void)
{
	struct cpuid result;
	unsigned long long ret = 0;

	cpuid(&result, 0);
	if (result.eax < 0x15) {
		return 0;
	}

	/* CPUID Leaf 0x15 (clock frequency) is supported, use it! */
	cpuid(&result, 0x15);
	if (result.eax == 0 || result.ebx == 0 || result.ecx == 0) {
		/*
		 * This should never happen, but using the string
		 * parsing fallback is better than crashing.
		 *
		 * Refer to linux's tsc.c if only ecx (crystal
		 * frequency) is zero, which can happen on consumer
		 * (?) parts.
		 */
		return 0;
	}

	/*
	 * ecx: crystal frequency;
	 * ebx/eax: frequency pumping ratio for TSC.
	 */
	ret = result.ecx;
	ret *= result.ebx;
	ret /= result.eax;
	return ret;
}
