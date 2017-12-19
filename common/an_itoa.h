#ifndef AN_ITOA_H
#define AN_ITOA_H
#include <inttypes.h>

/*
 * @brief Outputs a non-NUL terminated string representation of x to out, using at most 10 characters.
 * @result a pointer to the character after the last character in the representation of x.
 *
 * The string representation may be padded with zero bytes, but never writes past out[9] and the
 * return value disregards this padding.
 */
char *an_itoa(char *out, uint32_t x);

/*
 * @brief Outputs a non-NUL terminated string representation of x to out, using at most 20 characters.
 * @result a pointer to the character after the last character in the representation of x.
 *
 * Again, there may be trailing zero bytes after the return value of an_ltoa, but this function never
 * writes past out[19].
 */
char *an_ltoa(char *out, uint64_t x);
#endif
