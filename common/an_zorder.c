#include "common/an_zorder.h"

/*
 * See https://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN for more info
 */
uint32_t
an_zorder(uint16_t x, uint16_t y)
{
	uint64_t key = x;

	key |= ((uint64_t)y << 32);

	key = (key | (key << 8)) & 0x00FF00FF00FF00FFULL;
	key = (key | (key << 4)) & 0x0F0F0F0F0F0F0F0FULL;
	key = (key | (key << 2)) & 0x3333333333333333ULL;
	key = (key | (key << 1)) & 0x5555555555555555ULL;

	return (uint32_t)(key | key >> 31);
}
