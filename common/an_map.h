#ifndef AN_MAP_H
#define AN_MAP_H
/**
 * A SPMC typed hash map from uintptr_t-sized keys or c string keys to
 * uintptr_t-sized values.  Use it only if you must (e.g., for int to
 * int mappings).
 *
 * AN_MAP(NAME, KEY_TYPE, VALUE_TYPE, DEFAULT_VALUE) create a new type,
 * an_map_NAME that maps KEY_TYPE to VALUE_TYPE and uses DEFAULT_VALUE
 * as the default for VALUE_TYPE.
 *
 * AN_MAP_STRING: new type that maps string keys to VALUE_TYPE, with
 * DEFAULT_VALUE as the default.  Keys are copied and owned by the
 * dict.
 *
 * an_map_init/deinit: initialize/deinitialize preallocated map
 *
 * an_map_create/destroy: heap allocated and initialize a map or
 * deinitialize/free.
 *
 * an_map_compact: rebuild the hash map to optimize performance and
 * memory usage.
 *
 * an_map_size: number of items in the hash map.
 *
 * an_map_reset: clear the hash map of its contents
 *
 * an_map_reset_size: clear the hash map and resize it.
 *
 * an_map_grow: resize the hash map for at least the new size.
 *
 * an_map_get_pred: try to get the value associated with key in the
 * map.  Returns true and overwrites the OUT value pointer with the
 * value in the map if it exists, return false otherwise.
 *
 * an_map_get_default: return the value associated with key if it
 * exists, and the third argument otherwise.
 *
 * an_map_get: return the value associated with key if it exists, and
 * DEFAULT_VALUE otherwise.
 *
 * an_map_remove_pred: remove the value associated with key if it
 * exists; if so, return true and set the OUT value pointer to the old
 * value -- AN_MAP_STRING also deallocate the key.  Otherwise return
 * false.
 *
 * an_map_remove_default: remove the value associated with key if it
 * exists and return the old value, return the third argument
 * otherwise.  AN_MAP_STRING also deallocate the key.
 *
 * an_map_remove: remove the value associated with key if it exists
 * and return the old value, or return DEFAULT_VALUE otherwise.
 * AN_MAP_STRING also deallocate the key.
 *
 * an_map_ensure: map key to value and return true if no such entry
 * exists; return false if there is already a mapping with that key.
 * For AN_MAP_STRING, the key is copied, and deallocated on failure.
 *
 * an_map_put: blind an_map_ensure.
 *
 * an_map_insert: an_map_ensure, but assert that the write succeeds.
 *
 * an_map_replace_pred: map key to value; AN_MAP_STRING copies the
 * key.  If there was no prior mapping, return false.  If there was a
 * prior mapping, write the old value in the OUT argument pointer and
 * return false.  AN_MAP_STRING also deallocates the old key.
 *
 * an_map_replace_default: an_map_replace_pred but return the old
 * value directly if it exists, third argument otherwise.
 *
 * an_map_replace: an_map_replace_default, but return DEFAULT_VALUE if
 * there is no prior mapping.
 *
 * an_map_map: call callback on each key/value pair in the map.
 *
 * an_map_iterator_init/an_map_next: standard iterator interface.
 *
 * AN_MAP_FOREACH: wrap the iterator interface in a standard macro.
 * AN_MAP_FOREACH(NAME, pointer to map, variable name for key, var name for value)
 */
#include <assert.h>
#include <ck_ht.h>
#include <string.h>

#include "common/an_cc.h"
#include "common/common_types.h"

#define AN_MAP(NAME, KEY_TYPE, VALUE_TYPE, DEFAULT_VALUE)	\
	AN_MAP_INNER(NAME, KEY_TYPE, VALUE_TYPE, DEFAULT_VALUE, 0)

#define AN_MAP_STRING(NAME, VALUE_TYPE, DEFAULT_VALUE)	\
	AN_MAP_INNER(NAME, const char *, VALUE_TYPE, DEFAULT_VALUE, 1)

#define AN_MAP_INNER(NAME, KEY_TYPE, VALUE_TYPE, DEFAULT_VALUE, STRINGP) \
	typedef KEY_TYPE an_map_key_##NAME##_t;				\
	typedef VALUE_TYPE an_map_value_##NAME##_t;			\
									\
	struct an_map_##NAME {						\
		ck_ht_t ht;						\
	};								\
									\
	struct an_map_iterator_##NAME {					\
		ck_ht_iterator_t it;					\
	};								\
									\
	AN_CC_UNUSED static void					\
	an_map_init_##NAME(struct an_map_##NAME *map, size_t capacity)	\
	{								\
									\
		_Static_assert(sizeof(KEY_TYPE) <= sizeof(uintptr_t),	\
		    "pointer_sized_key");				\
		_Static_assert(sizeof(VALUE_TYPE) <= sizeof(uintptr_t),	\
		    "pointer_sized_value");				\
									\
		an_ht_init(&map->ht,					\
		    (STRINGP) ? CK_HT_MODE_BYTESTRING : CK_HT_MODE_DIRECT, \
		    (capacity < 8) ? 16 : 2 * capacity);		\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_string_string_free_cb_##NAME(KEY_TYPE key, VALUE_TYPE value, \
	    void *data)							\
	{								\
		char *ptr = NULL;					\
									\
		(void)value;						\
		(void)data;						\
		memcpy(&ptr, &key, sizeof(ptr));			\
		an_string_free(ptr);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_map_##NAME(struct an_map_##NAME *map,			\
	    void (cb)(KEY_TYPE key, VALUE_TYPE value, void *data),	\
	    void *data);						\
									\
	AN_CC_UNUSED static void					\
	an_map_deinit_##NAME(struct an_map_##NAME *map)			\
	{								\
									\
		if (map == NULL) {					\
			return;						\
		}							\
									\
		if (STRINGP) {						\
			an_map_map_##NAME(map,				\
			    an_map_string_string_free_cb_##NAME,	\
			    NULL);					\
		}							\
									\
		ck_ht_destroy(&map->ht);				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static struct an_map_##NAME *			\
	an_map_create_##NAME(size_t capacity)				\
	{								\
		struct an_map_##NAME *ret;				\
									\
		ret = an_calloc_object(an_map_token);			\
		an_map_init_##NAME(ret, capacity);			\
		return ret;						\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_destroy_##NAME(struct an_map_##NAME *map)		\
	{								\
									\
		if (map == NULL) {					\
			return;						\
		}							\
									\
		an_map_deinit_##NAME(map);				\
		an_free(an_map_token, map);				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_compact_##NAME(struct an_map_##NAME *map)		\
	{								\
		bool r;							\
									\
		r = ck_ht_gc(&map->ht, 0, 0);				\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static size_t					\
	an_map_size_##NAME(struct an_map_##NAME *map)			\
	{								\
									\
		return ck_ht_count(&map->ht);				\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_reset_##NAME(struct an_map_##NAME *map)			\
	{								\
		bool r;							\
									\
		if (STRINGP) {						\
			an_map_map_##NAME(map,				\
			    an_map_string_string_free_cb_##NAME,	\
			    NULL);					\
		}							\
									\
		r = ck_ht_reset_spmc(&map->ht);				\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_reset_size_##NAME(struct an_map_##NAME *map, size_t new_size) \
	{								\
		bool r;							\
									\
		if (STRINGP) {						\
			an_map_map_##NAME(map,				\
			    an_map_string_string_free_cb_##NAME,	\
			    NULL);					\
		}							\
									\
		r = ck_ht_reset_size_spmc(&map->ht,			\
		    (new_size < 8) ? 16 : 2 * new_size);		\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_grow_##NAME(struct an_map_##NAME *map, size_t new_size)	\
	{								\
		bool r;							\
									\
		r = ck_ht_grow_spmc(&map->ht,				\
		    (new_size < 8) ? 16 : 2 * new_size);		\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_map_get_pred_##NAME(struct an_map_##NAME *map,		\
	    KEY_TYPE key, VALUE_TYPE *value)				\
	{								\
		ck_ht_entry_t entry;					\
		ck_ht_hash_t h;						\
		uintptr_t full_key = 0;					\
		uintptr_t full_value;					\
									\
		if (STRINGP) {						\
			const char *str = NULL;				\
			size_t length;					\
									\
			memcpy(&str, &key, sizeof(str));		\
			length = strlen(str) + 1;			\
			if (length > UINT16_MAX) {			\
				/* No key that long. */			\
				return false;				\
			}						\
									\
			ck_ht_entry_key_set(&entry, str, length);	\
			ck_ht_hash(&h, &map->ht, str, length);		\
		} else {						\
			memcpy(&full_key, &key, sizeof(key));		\
									\
			if (full_key == 0 || full_key == UINTPTR_MAX) {	\
				/* Can't store such keys. */		\
				return false;				\
			}						\
									\
			ck_ht_entry_key_set_direct(&entry, full_key);	\
			ck_ht_hash_direct(&h, &map->ht, full_key);	\
		}							\
									\
		if (ck_ht_get_spmc(&map->ht, h, &entry) == false) {	\
			return false;					\
		}							\
									\
		if (value != NULL) {					\
			if (STRINGP) {					\
				full_value = (uintptr_t)ck_ht_entry_value(&entry); \
			} else {					\
				full_value = ck_ht_entry_value_direct(&entry); \
			}						\
									\
			memcpy(value, &full_value, sizeof(VALUE_TYPE));	\
		}							\
									\
		return true;						\
	}								\
									\
	AN_CC_UNUSED static VALUE_TYPE					\
	an_map_get_default_##NAME(struct an_map_##NAME *map,		\
	    KEY_TYPE key, VALUE_TYPE default_value)			\
	{								\
									\
		(void)an_map_get_pred_##NAME(map, key, &default_value); \
		return default_value;					\
	}								\
									\
	AN_CC_UNUSED static VALUE_TYPE					\
	an_map_get_##NAME(struct an_map_##NAME *map, KEY_TYPE key)	\
	{								\
		VALUE_TYPE default_value = DEFAULT_VALUE;		\
									\
		return an_map_get_default_##NAME(map, key,		\
		    default_value);					\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_map_remove_pred_##NAME(struct an_map_##NAME *map,		\
	    KEY_TYPE key, VALUE_TYPE *value)				\
	{								\
		ck_ht_entry_t entry;					\
		ck_ht_hash_t h;						\
		uintptr_t full_key = 0;					\
		uintptr_t full_value;					\
									\
		if (STRINGP) {						\
			const char *str = NULL;				\
			size_t length;					\
									\
			memcpy(&str, &key, sizeof(str));		\
			length = strlen(str) + 1;			\
									\
			if (length > UINT16_MAX) {			\
				/* No such entry. */			\
				return false;				\
			}						\
									\
			ck_ht_entry_key_set(&entry, str, length);	\
			ck_ht_hash(&h, &map->ht, str, length);		\
		} else {						\
			memcpy(&full_key, &key, sizeof(key));		\
									\
			if (full_key == 0 || full_key == UINTPTR_MAX) {	\
				/* No such key. */			\
				return false;				\
			}						\
									\
			ck_ht_entry_key_set_direct(&entry, full_key);	\
			ck_ht_hash_direct(&h, &map->ht, full_key);	\
		}							\
									\
		if (ck_ht_remove_spmc(&map->ht, h, &entry) == false) {	\
			return false;					\
		}							\
									\
		if (value != NULL) {					\
			if (STRINGP) {					\
				full_value = (uintptr_t)ck_ht_entry_value(&entry); \
			} else {					\
				full_value = ck_ht_entry_value_direct(&entry); \
			}						\
									\
			memcpy(value, &full_value, sizeof(VALUE_TYPE));	\
		}							\
									\
		if (STRINGP) {						\
			an_string_free(ck_ht_entry_key(&entry));	\
		}							\
									\
		return true;						\
	}								\
									\
	AN_CC_UNUSED static VALUE_TYPE					\
	an_map_remove_default_##NAME(struct an_map_##NAME *map,		\
	    KEY_TYPE key, VALUE_TYPE default_value)			\
	{								\
									\
		(void)an_map_remove_pred_##NAME(map, key, &default_value); \
		return default_value;					\
	}								\
									\
	AN_CC_UNUSED static VALUE_TYPE					\
	an_map_remove_##NAME(struct an_map_##NAME *map, KEY_TYPE key)	\
	{								\
		VALUE_TYPE default_value = DEFAULT_VALUE;		\
									\
		return an_map_remove_default_##NAME(map, key,		\
		    default_value);					\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_map_ensure_##NAME(struct an_map_##NAME *map,			\
	    KEY_TYPE key, VALUE_TYPE value)				\
	{								\
		ck_ht_entry_t entry;					\
		ck_ht_hash_t h;						\
		char *str = NULL;					\
		size_t length;						\
		uintptr_t full_key = 0;					\
		uintptr_t full_value = 0;				\
		bool r;							\
									\
		memcpy(&full_value, &value, sizeof(value));		\
		if (STRINGP) {						\
			memcpy(&str, &key, sizeof(str));		\
			length = strlen(str) + 1;			\
			str = an_string_dup(str);			\
									\
			assert(length <= UINT16_MAX);			\
			ck_ht_hash(&h, &map->ht, str, length);		\
			assert((full_value >> CK_MD_VMA_BITS) == 0);	\
			ck_ht_entry_set(&entry, h, str, length,		\
			    (void *)full_value);			\
		} else {						\
			memcpy(&full_key, &key, sizeof(key));		\
			assert(full_key != 0 && full_key != UINTPTR_MAX); \
									\
			ck_ht_hash_direct(&h, &map->ht, full_key);	\
			ck_ht_entry_set_direct(&entry, h, full_key, full_value); \
		}							\
									\
		r = ck_ht_put_spmc(&map->ht, h, &entry);		\
		if (r == false && (STRINGP)) {				\
			an_string_free(str);				\
		}							\
									\
		return r;						\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_put_##NAME(struct an_map_##NAME *map,			\
	    KEY_TYPE key, VALUE_TYPE value)				\
	{								\
									\
		(void)an_map_ensure_##NAME(map, key, value);		\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_insert_##NAME(struct an_map_##NAME *map,			\
	    KEY_TYPE key, VALUE_TYPE value)				\
	{								\
		bool r;							\
									\
		r = an_map_ensure_##NAME(map, key, value);		\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_map_replace_pred_##NAME(struct an_map_##NAME *map,		\
	    KEY_TYPE key, VALUE_TYPE value, VALUE_TYPE *old)		\
	{								\
		ck_ht_entry_t entry;					\
		ck_ht_hash_t h;						\
		char *str = NULL;					\
		size_t length;						\
		uintptr_t full_key = 0;					\
		uintptr_t full_value = 0;				\
		bool r;							\
									\
		memcpy(&full_value, &value, sizeof(value));		\
		if (STRINGP) {						\
			memcpy(&str, &key, sizeof(str));		\
			length = strlen(str) + 1;			\
			str = an_string_dup(str);			\
									\
			assert(length <= UINT16_MAX);			\
			ck_ht_hash(&h, &map->ht, str, length);		\
			assert((full_value >> CK_MD_VMA_BITS) == 0);	\
			ck_ht_entry_set(&entry, h, str, length,		\
			    (void *)full_value);			\
		} else {						\
			memcpy(&full_key, &key, sizeof(key));		\
			assert(full_key != 0 && full_key != UINTPTR_MAX); \
									\
			ck_ht_hash_direct(&h, &map->ht, full_key);	\
			ck_ht_entry_set_direct(&entry, h, full_key, full_value); \
		}							\
									\
		r = ck_ht_set_spmc(&map->ht, h, &entry);		\
		assert(r == true);					\
									\
		if (ck_ht_entry_empty(&entry) == true) {		\
			return false;					\
		}							\
									\
		if (old != NULL) {					\
			if (STRINGP) {					\
				full_value = (uintptr_t)ck_ht_entry_value(&entry); \
			} else {					\
				full_value = ck_ht_entry_value_direct(&entry); \
			}						\
									\
			memcpy(old, &full_value, sizeof(*old));		\
		}							\
									\
		if (STRINGP) {						\
			an_string_free(ck_ht_entry_key(&entry));	\
		}							\
									\
		return true;						\
	}								\
									\
	AN_CC_UNUSED static VALUE_TYPE					\
	an_map_replace_default_##NAME(struct an_map_##NAME *map,	\
	    KEY_TYPE key, VALUE_TYPE value, VALUE_TYPE default_old)	\
	{								\
									\
		(void)an_map_replace_pred_##NAME(map, key, value,	\
		    &default_old);					\
		return default_old;					\
	}								\
									\
	AN_CC_UNUSED static VALUE_TYPE					\
	an_map_replace_##NAME(struct an_map_##NAME *map,		\
	    KEY_TYPE key, VALUE_TYPE value)				\
	{								\
		VALUE_TYPE default_value = DEFAULT_VALUE;		\
									\
		return an_map_replace_default_##NAME(map,		\
		    key, value, default_value);				\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_map_##NAME(struct an_map_##NAME *map,			\
	    void (cb)(KEY_TYPE key, VALUE_TYPE value, void *data),	\
	    void *data)							\
	{								\
		ck_ht_iterator_t it;					\
		ck_ht_entry_t *entry;					\
		KEY_TYPE key;						\
		VALUE_TYPE value;					\
		uintptr_t full_key, full_value;				\
									\
		ck_ht_iterator_init(&it);				\
		while (ck_ht_next(&map->ht, &it, &entry) == true) {	\
			if (STRINGP) {					\
				full_key = (uintptr_t)ck_ht_entry_key(entry); \
				full_value = (uintptr_t)ck_ht_entry_value(entry); \
			} else {					\
				full_key = ck_ht_entry_key_direct(entry); \
				full_value = ck_ht_entry_value_direct(entry); \
			}						\
									\
			memcpy(&key, &full_key, sizeof(key));		\
			memcpy(&value, &full_value, sizeof(value));	\
									\
			cb(key, value, data);				\
		}							\
									\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_map_iterator_init_##NAME(struct an_map_iterator_##NAME *it)	\
	{								\
									\
		ck_ht_iterator_init(&it->it);				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_map_next_##NAME(struct an_map_##NAME *map,			\
	    struct an_map_iterator_##NAME *it,				\
	    KEY_TYPE *key, VALUE_TYPE *value)				\
	{								\
		ck_ht_entry_t *entry;					\
		uintptr_t full_key, full_value;				\
									\
		if (ck_ht_next(&map->ht, &it->it, &entry) == false) {	\
			return false;					\
		}							\
									\
		if (key != NULL) {					\
			if (STRINGP) {					\
				full_key = (uintptr_t)ck_ht_entry_key(entry); \
			} else {					\
				full_key = ck_ht_entry_key_direct(entry); \
			}						\
									\
			memcpy(key, &full_key, sizeof(*key));		\
		}							\
									\
		if (value != NULL) {					\
			if (STRINGP) {					\
				full_value = (uintptr_t)ck_ht_entry_value(entry); \
			} else {					\
				full_value = ck_ht_entry_value_direct(entry); \
			}						\
									\
			memcpy(value, &full_value, sizeof(VALUE_TYPE));	\
		}							\
									\
		return true;						\
	}

#define an_map_map(NAME, MAP, CB, DATA)					\
	an_map_map_##NAME((MAP),					\
	    AN_CC_CAST_IF_COMPATIBLE((CB),				\
		void (*)(an_map_key_##NAME##_t, an_map_value_##NAME##_t, \
		    __typeof__(DATA))),					\
	    (DATA))

#define AN_MAP_FOREACH_IMPL(NAME, MAP, KEY, VALUE, GENSYM)		\
	struct an_map_iterator_##NAME it_##NAME##_##GENSYM;		\
	struct an_map_##NAME *map_##NAME##_##GENSYM = (MAP);		\
	an_map_key_##NAME##_t KEY;					\
	an_map_value_##NAME##_t VALUE;					\
									\
	memset(&(KEY), 0, sizeof(KEY));					\
	memset(&(VALUE), 0, sizeof(VALUE));				\
	for (an_map_iterator_init_##NAME(&it_##NAME##_##GENSYM);	\
	     an_map_next_##NAME(map_##NAME##_##GENSYM, &it_##NAME##_##GENSYM, \
		 &KEY, &VALUE) == true; )

#define AN_MAP_FOREACH(NAME, MAP, KEY, VALUE) AN_MAP_FOREACH_IMPL(NAME, MAP, KEY, VALUE, __COUNTER__)
#endif /* !AN_MAP_H */
