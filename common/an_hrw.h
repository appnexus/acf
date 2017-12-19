#ifndef COMMON_AN_HRW
#define COMMON_AN_HRW

#include "common/an_dict.h"

/*
 * This file implements Highest Random Weight hashing or Rendezvous hashing.
 * Right now, it only supports use cases that want the single highest
 * resource (instead of N highest), since it is trivial to keep track of
 * the maximum element while looping a list, but annoying to track N.
 *
 * Invoke AN_HRW with the same NAME as the dict. Tell it the
 * TYPE, and the KEY_FIELD to use as an identifier for each entry.
 * Note that KEY_FIELD can not be a pointer or contain pointers.
 *
 * AN_HRW should only need to be invoked in a source file,
 * even if the AN_DICT_INLINE call is in a header. If both are going
 * into a source file, use AN_HRW_DICT to declare both at once.
 */
#define AN_HRW_SEED 0x1122334455667788

/* As per above, you should only use this in the source files. */
#define AN_HRW(NAME, TYPE, KEY_FIELD)				\
									\
	AN_CC_UNUSED static TYPE *					\
	an_hrw_single_##NAME(struct an_dict_##NAME *dict,	\
	    const void *key, size_t key_len)				\
	{								\
		TYPE *current_max = NULL;				\
		uint64_t max_murmur_score = 0;				\
		uint64_t key_hash = MurmurHash64A(key, key_len,		\
		    AN_HRW_SEED);				\
									\
		AN_DICT_FOREACH(NAME, dict, cursor) {			\
			uint64_t murmur_score = 0;			\
			size_t entry_len = sizeof(cursor->KEY_FIELD);	\
									\
			murmur_score =					\
			    MurmurHash64A(&cursor->KEY_FIELD,		\
			        entry_len, key_hash);			\
									\
			if (murmur_score < max_murmur_score) {		\
				continue;				\
			}						\
									\
			max_murmur_score = murmur_score;		\
			current_max = cursor;				\
		}							\
									\
		return current_max;					\
	}

/* As per above, you should only use this in the source files. */
#define AN_HRW_DICT(NAME, TYPE, DICT_KEY_FIELD,			\
    HRW_KEY_FIELD)						\
	AN_DICT_INLINE(NAME, TYPE, DICT_KEY_FIELD);			\
	AN_HRW(NAME, TYPE, HRW_KEY_FIELD)

#endif /* COMMON_AN_HRW */
