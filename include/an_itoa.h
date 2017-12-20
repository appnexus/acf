#ifndef AN_ITOA_H
#define AN_ITOA_H
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
