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

#ifndef _CPUID_H
#define _CPUID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * This is the expected maximum length of the brand string.
 */
#define CPUID_BRAND_LENGTH 49

/*
 * List of vendors that I care about.
 */
enum {
	CPUID_VENDOR_AMD,
	CPUID_VENDOR_INTEL,
	CPUID_VENDOR_UNKNOWN,
	CPUID_VENDOR_LENGTH
};

/*
 * List of CPU features.
 */
enum {
	CPUID_FEATURE_FPU,
	CPUID_FEATURE_VME,
	CPUID_FEATURE_DE,
	CPUID_FEATURE_PSE,
	CPUID_FEATURE_TSC,
	CPUID_FEATURE_MSR,
	CPUID_FEATURE_PAE,
	CPUID_FEATURE_MCE,
	CPUID_FEATURE_CX8,
	CPUID_FEATURE_APIC,
	CPUID_FEATURE_SEP,
	CPUID_FEATURE_MTRR,
	CPUID_FEATURE_PGE,
	CPUID_FEATURE_MCA,
	CPUID_FEATURE_CMOV,
	CPUID_FEATURE_PAT,
	CPUID_FEATURE_PSE36,
	CPUID_FEATURE_PSN,
	CPUID_FEATURE_CLFSH,
	CPUID_FEATURE_DS,
	CPUID_FEATURE_ACPI,
	CPUID_FEATURE_MMX,
	CPUID_FEATURE_FXSR,
	CPUID_FEATURE_SSE,
	CPUID_FEATURE_SSE2,
	CPUID_FEATURE_SS,
	CPUID_FEATURE_HTT,
	CPUID_FEATURE_TM,
	CPUID_FEATURE_PBE,
	CPUID_FEATURE_SSE3,
	CPUID_FEATURE_PCLMULQDQ,
	CPUID_FEATURE_DTES64,
	CPUID_FEATURE_MONITOR,
	CPUID_FEATURE_DSCPL,
	CPUID_FEATURE_VMX,
	CPUID_FEATURE_SMX,
	CPUID_FEATURE_EST,
	CPUID_FEATURE_TM2,
	CPUID_FEATURE_SSSE3,
	CPUID_FEATURE_CNXTID,
	CPUID_FEATURE_FMA,
	CPUID_FEATURE_CMPXCHG16B,
	CPUID_FEATURE_XTPR,
	CPUID_FEATURE_PDCM,
	CPUID_FEATURE_PCID,
	CPUID_FEATURE_DCA,
	CPUID_FEATURE_SSE41,
	CPUID_FEATURE_SSE42,
	CPUID_FEATURE_X2APIC,
	CPUID_FEATURE_MOVBE,
	CPUID_FEATURE_POPCNT,
	CPUID_FEATURE_TSCD,
	CPUID_FEATURE_AESNI,
	CPUID_FEATURE_XSAVE,
	CPUID_FEATURE_OSXSAVE,
	CPUID_FEATURE_AVX,
	CPUID_FEATURE_RDTSCP,
	CPUID_FEATURE_NX,
	CPUID_FEATURE_GBP,
	CPUID_FEATURE_SYSCALL,
	CPUID_FEATURE_X86_64,
	CPUID_FEATURE_INV_TSC,
	CPUID_FEATURE_LENGTH
};

/*
 * Returns true if the feature specified in the only argument
 * (a value from the CPUID_FEATURE_ namespace) is available on
 * the processor that executes this function. Returns false
 * otherwise.
 */
bool cpuid_feature(int);

/*
 * Returns a string representation of feature specified in
 * the first argument (a value from the CPUID_FEATURE_ namespace
 * except for CPUID_FEATURE_LENGTH).
 */
const char *cpuid_feature_string(int);

/*
 * Returns an integer that identifies the vendor identified for
 * the processor executing the function. This integer will have a
 * mapping to the CPUID_VENDOR namespace.
 */
int cpuid_vendor(void);

/*
 * Returns a string representation of the vendor specified in
 * the first argument (from CPUID_VENDOR).
 */
const char *cpuid_vendor_string(int);

/*
 * Stores up to length bytes of the processor brand string into the
 * area pointed to by buffer. The brand string is null terminated.
 */
void cpuid_brand(char *buffer, size_t length);

void cpuid_address_size(uint8_t *physical, uint8_t *virtual);

/*
 * @brief Use CPUID feature 0x15 to compute the core frequency.
 * @return 0 on failure (e.g., CPU does not have that feature), the
 * core frequency in Hz otherwise.
 *
 * See Intel's software development manual, Section 17.16.4 (Invariant
 * Time-Keeping).  I can't find documentation on ECX, but Linux does
 * the same thing (modulo some extra logic to fill in some missing
 * values) in arch/x86/kernel/tsc.c:native_calibrate_tsc.
 */
unsigned long long cpuid_core_frequency(void);
#endif /* _CPUID_H */
