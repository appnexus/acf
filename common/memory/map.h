#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H
#include <stddef.h>

/**
 * @brief map at least @a at_least bytes at @a address, and at most @a at_most.
 * @return 0 if the mapping failed, number of bytes mapped on success.
 *
 * Assumes the address is already reserved via map_reserve.
 * TODO: NUMA and hugepage hints.
 */
size_t an_memory_map(void *address, size_t at_least, size_t at_most);
#endif /* !MEMORY_MAP_H */
