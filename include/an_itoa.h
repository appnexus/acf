#ifndef AN_ITOA_H
#define AN_ITOA_H
/**
 * Fast unsigned integer to decimal string conversion code.  The code
 * ONLY WORKS ON LITTLE ENDIAN machines, and will be slow on hardware
 * with bad support for unaligned stores.
 *
 * Unlike regular itoa, the result is *not* NUL-terminated, and the
 * routines are allowed to write garbage bytes in the destination
 * buffer after the decimal string, as long as they stay in bounds:
 * `an_itoa` never writes past `out[9]`, and `an_ltoa` past `out[19]`.
 *
 * Porting this to big endian should only necessitate a byteswap
 * before writing to memory, but we've never needed to support BE.
 */

#include "an_cc.h"

AN_EXTERN_C_BEGIN

#include <stdint.h>

#include "acf_export.h"

/**
 * @brief Output a non-NUL terminated string representation of x to
 * out, using at most 10 characters.
 * @result a pointer to the character after the last character in the
 * representation of x.
 *
 * The string representation may have trailing noise, but never writes
 * past out[9] and the return value disregards this padding.
 */
ACF_EXPORT char *an_itoa(char *out, uint32_t x);

/**
 * @brief Output a non-NUL terminated string representation of x to
 * out, using at most 20 characters.
 * @result a pointer to the character after the last character in the
 * representation of x.
 *
 * Again, there may be trailing noise bytes after the return value of
 * an_ltoa, but this function never writes past out[19].
 */
ACF_EXPORT char *an_ltoa(char *out, uint64_t x);

AN_EXTERN_C_END
#endif
