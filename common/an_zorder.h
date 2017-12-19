#ifndef AN_ZORDER_H
#define AN_ZORDER_H

#include <stdint.h>

/**
 * @brief computes the z-order index of 2 16 bit integers
 *
 * Bits of x will be in the zero-indexed even bit positions and bits
 * of y in the odd positions.
 */
uint32_t an_zorder(uint16_t x, uint16_t y);

#endif /* AN_ZORDER_H */
