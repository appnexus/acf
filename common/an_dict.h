#ifndef AN_DICT_H
#define AN_DICT_H
/**
 * A SPMC typed set of objects with intrinsic keys, or, as a special
 * case, a set of c strings.  Use this instead of an_map if possible.
 *
 * The common case is that we want a set of certain type (e.g.,
 * member) keyed on one field, (e.g., member_id).
 * AN_DICT_INLINE(suffix, object type, key field) wraps ck_hs to
 * provide a typed set of pointer to object type, keyed on key field.
 *
 * Another case has string keys, either inline or as pointers in the
 * object; n.b., if the string key is inline we still can't use
 * AN_DICT_INLINE because that would compare with memcmp, not strcmp.
 *
 * AN_DICT_STRING(NAME, TYPE, KEY_FIELD, true) defines a new dict type
 * of TYPE keyed on KEY_FIELD, treated as a string (e.g., char key[10]).
 *
 * AN_DICT_STRING(NAME, TYPE, KEY_FIELD, false) defines a new dict
 * type of TYPE keyed on KEY_FIELD, treated as a char *.
 *
 * Finally, an_dict_string is a pre-defined set of c strings; in that
 * special case, the dict owns the strings.
 *
 * an_dict_init/deinit: initialize/deinitialize a preallocated inline
 * dict.  an_dict_string release owned strings on deinit.
 *
 * an_dict_create/destroy: return a new heap allocated dict/deinit &
 * deallocate it.
 *
 * an_dict_compact: rebuild the hash set to optimize performance and
 * memory usage.
 *
 * an_dict_size: number of values in the hash set
 *
 * an_dict_reset: clear the hash set. For an_dict_string, also release
 * the memory for c strings.
 *
 * an_dict_reset_size: clear the hash set and recreate for new_size.
 *
 * an_dict_grow: prepare the hash set for new_size.
 *
 * an_dict_get: find the value associated with the key (passed as a
 * pointer), or NULL if none.
 *
 * an_dict_member: return whether there is a value with that key in
 * the dict.
 *
 * an_dict_remove: remove any object with the same key and return it
 * if it exists (NULL otherwise).
 *
 * an_dict_remove_key: an_dict_remove, but receive a pointer to a key
 * instead of a pointer to an object.  Does not exist for
 * an_dict_string.
 *
 * an_dict_delete: like an_dict_remove, but returns true if something
 * was deleted and false otherwise.  For owned strings, the string is
 * released on deletion.
 *
 * an_dict_ensure: insert the object in the dict if nothing already
 * exists with that key, returning true iff an object was inserted.
 * For owned strings, a copy is allocated and inserted in the dict
 * (and deallocated on failure).
 *
 * an_dict_put: explicitly discard the return value from an_dict_ensure.
 *
 * an_dict_insert: assert that an_dict_ensure succeeded.
 *
 * an_dict_replace: insert or overwrite the object in the dict and
 * return the old value (or NULL if none).
 *
 * an_dict_map: call a callback on each value in the dict
 *
 * an_dict_iterator_init/next: iteration interface.
 *
 * AN_DICT_FOREACH: wrap the iteration interface in a for-style macro.
 * AN_DICT_FOREACH(NAME, pointer to dict, variable name for value).
 *
 * TODO: an_dict_register (c.f. an_table.c).
 */

#include <assert.h>
#include <ck_hs.h>
#include <string.h>

#include "common/an_cc.h"
#include "common/an_malloc.h"
#include "common/an_table_hash.h"
#include "common/common_types.h"

#include "common/an_dict_common.h"

#define AN_DICT_INLINE(NAME, TYPE, KEY_FIELD)				\
	typedef __typeof__(((TYPE *)NULL)->KEY_FIELD) an_dict_key_##NAME##_t; \
									\
	AN_CC_UNUSED static unsigned long				\
	an_dict_hash_##NAME(const void *vkey, unsigned long seed)	\
	{								\
									\
		/* N.B., we could use a container_of trick, but that */	\
		/* makes walking hashes in gdb much harder. */		\
		return MurmurHash64A(&((TYPE *)vkey)->KEY_FIELD,	\
		    sizeof(((TYPE *)vkey)->KEY_FIELD), seed);		\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_dict_cmp_##NAME(const void *vx, const void *vy)		\
	{								\
		const void *x = &((TYPE *)vx)->KEY_FIELD;		\
		const void *y = &((TYPE *)vy)->KEY_FIELD;		\
									\
		return memcmp(x, y, sizeof(an_dict_key_##NAME##_t)) == 0; \
	}								\
									\
	AN_DICT_COMMON(NAME, TYPE, 0, );				\
									\
	AN_CC_UNUSED static TYPE *					\
	an_dict_get_##NAME(struct an_dict_##NAME *dict,			\
	    const an_dict_key_##NAME##_t *key)				\
	{								\
		TYPE dummy;						\
		unsigned long hash;					\
									\
		memcpy(&dummy.KEY_FIELD, key, sizeof(*key));		\
		hash = CK_HS_HASH(&dict->hs, an_dict_hash_##NAME, &dummy); \
		return ck_hs_get(&dict->hs, hash, &dummy);		\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_dict_member_##NAME(struct an_dict_##NAME *dict,		\
	    const an_dict_key_##NAME##_t *key)				\
	{								\
									\
		return an_dict_get_##NAME(dict, key) != NULL;		\
	}								\
									\
	AN_CC_UNUSED static TYPE *					\
	an_dict_remove_key_##NAME(struct an_dict_##NAME *dict,		\
	    const an_dict_key_##NAME##_t *key)				\
	{								\
		TYPE dummy;						\
									\
		memcpy(&dummy.KEY_FIELD, key, sizeof(*key));		\
		return an_dict_remove_##NAME(dict, &dummy);		\
	}

/*
 * coverity[buffer_size_warning : FALSE]
 * Wrapper to mute coverity warnings about strncpy use in an_dict
 */
static inline char *
an_dict_strncpy(char *destination, const char *source, size_t num)
{

	return strncpy(destination, source, num);
}

#define AN_DICT_STRING(NAME, TYPE, KEY_FIELD, INLINE)			\
	AN_CC_UNUSED static unsigned long				\
	an_dict_hash_##NAME(const void *vkey, unsigned long seed)	\
	{								\
		const TYPE *obj = vkey;					\
		const char *key = obj->KEY_FIELD;			\
		size_t len;						\
									\
		if ((INLINE) == true) {					\
			len = 1 + strnlen(key,				\
			    (INLINE) ? sizeof(obj->KEY_FIELD) - 1 : 0); \
		} else {						\
			len = strlen(key) + 1;				\
		}							\
									\
		return MurmurHash64A(key, len, seed);			\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_dict_cmp_##NAME(const void *vx, const void *vy)		\
	{								\
		const TYPE *x = vx;					\
		const TYPE *y = vy;					\
									\
		if ((INLINE) == false) {				\
			return strcmp(x->KEY_FIELD, y->KEY_FIELD) == 0;	\
		} else {						\
			return strncmp(x->KEY_FIELD, y->KEY_FIELD,	\
			    (INLINE) ? sizeof(x->KEY_FIELD) : 1) == 0;	\
		}							\
	}								\
									\
	AN_DICT_COMMON(NAME, TYPE, 0, );				\
									\
	AN_CC_UNUSED static TYPE *					\
	an_dict_get_##NAME(struct an_dict_##NAME *dict, const char *key) \
	{								\
		TYPE dummy;						\
		unsigned long hash;					\
									\
		_Static_assert(!!(INLINE) ==				\
		    !(__builtin_types_compatible_p(__typeof__(dummy.KEY_FIELD), const char *) || \
			__builtin_types_compatible_p(__typeof__(dummy.KEY_FIELD), char *)) , \
		    "inline_string_field_inline");			\
									\
		_Static_assert(__builtin_types_compatible_p(__typeof__(dummy.KEY_FIELD[0]), char), \
		    "key_is_string");					\
									\
		if ((INLINE) == false) {				\
			memcpy(&dummy.KEY_FIELD, &key,			\
			    (INLINE) ? 0 : sizeof(const char *));	\
		} else {						\
			an_dict_strncpy((void *)dummy.KEY_FIELD, key,	\
			    (INLINE) ? sizeof(dummy.KEY_FIELD) : 0);	\
		}							\
									\
		hash = CK_HS_HASH(&dict->hs, an_dict_hash_##NAME, &dummy); \
		return ck_hs_get(&dict->hs, hash, &dummy);		\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_dict_member_##NAME(struct an_dict_##NAME *dict, const char *key) \
	{								\
									\
		return an_dict_get_##NAME(dict, key) != NULL;		\
	}								\
									\
	AN_CC_UNUSED static TYPE *					\
	an_dict_remove_key_##NAME(struct an_dict_##NAME *dict, const char *key) \
	{								\
		TYPE dummy;						\
									\
		if ((INLINE) == false) {				\
			memcpy(&dummy.KEY_FIELD, &key,			\
			    (INLINE) ? 0 : sizeof(const char *));	\
		} else {						\
			an_dict_strncpy((void *)&dummy.KEY_FIELD, key,	\
			    (INLINE) ? sizeof(dummy.KEY_FIELD) : 0);	\
		}							\
									\
		return an_dict_remove_##NAME(dict, &dummy);		\
	}

AN_CC_UNUSED static unsigned long
an_dict_hash_string(const void *vkey, unsigned long seed)
{

	return MurmurHash64A(vkey, strlen(vkey), seed);
}

AN_CC_UNUSED static bool
an_dict_cmp_string(const void *x, const void *y)
{

	return strcmp(x, y) == 0;
}

AN_DICT_COMMON(string, char, 1, const);

AN_CC_UNUSED static char *
an_dict_get_string(struct an_dict_string *dict, const char *key)
{
	unsigned long hash;

	hash = CK_HS_HASH(&dict->hs, an_dict_hash_string, key);
	return ck_hs_get(&dict->hs, hash, key);
}

AN_CC_UNUSED static bool
an_dict_member_string(struct an_dict_string *dict, const char *key)
{

	return an_dict_get_string(dict, key) != NULL;
}
#endif /* !AN_DICT_H */
