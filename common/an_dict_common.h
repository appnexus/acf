/*
 * Do not include this file directly.
 */

#ifndef AN_DICT_COMMON_H
#define AN_DICT_COMMON_H

AN_CC_UNUSED static void
an_dict_string_free_string_cb(void *string, void *data)
{

	(void)data;
	an_string_free(string);
	return;
}

#define AN_DICT_COMMON(NAME, TYPE, OWNED_STRING, INSERT_CONSTNESS)	\
	typedef TYPE an_dict_value_##NAME##_t;				\
									\
	struct an_dict_##NAME {						\
		ck_hs_t hs;						\
	};								\
									\
	struct an_dict_iterator_##NAME {				\
		ck_hs_iterator_t it;					\
	};								\
									\
	AN_CC_UNUSED static void					\
	an_dict_map_##NAME(struct an_dict_##NAME *dict,			\
	    void (cb)(void *key, void *data), void *data);		\
									\
	AN_CC_UNUSED static void					\
	an_dict_init_private_##NAME(struct an_dict_##NAME *dict, size_t capacity) \
	{								\
									\
		an_hs_init_private(&dict->hs, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC, \
		    an_dict_hash_##NAME, an_dict_cmp_##NAME,		\
		    /* ck_hs </3 small tables, and <3 50% util. */	\
		    (capacity < 8) ? 16 : 2 * capacity); 		\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_init_##NAME(struct an_dict_##NAME *dict, size_t capacity) \
	{								\
									\
		an_hs_init(&dict->hs, CK_HS_MODE_OBJECT | CK_HS_MODE_SPMC, \
		    an_dict_hash_##NAME, an_dict_cmp_##NAME,		\
		    /* ck_hs </3 small tables, and <3 50% util. */	\
		    (capacity < 8) ? 16 : 2 * capacity);		\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_deinit_##NAME(struct an_dict_##NAME *dict)		\
	{								\
									\
		if (dict == NULL) {					\
			return;						\
		}							\
									\
		if (OWNED_STRING) {					\
			an_dict_map_##NAME(dict,			\
			    an_dict_string_free_string_cb, NULL);	\
		}							\
									\
		ck_hs_destroy(&dict->hs);				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static struct an_dict_##NAME *			\
	an_dict_create_##NAME(size_t capacity)				\
	{								\
		struct an_dict_##NAME *ret;				\
									\
		ret = an_calloc_object(an_dict_token);			\
		an_dict_init_##NAME(ret, capacity);			\
		return ret;						\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_destroy_##NAME(struct an_dict_##NAME *dict)		\
	{								\
									\
		if (dict == NULL) {					\
			return;						\
		}							\
									\
		an_dict_deinit_##NAME(dict);				\
		an_free(an_dict_token, dict);				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_compact_##NAME(struct an_dict_##NAME *dict)		\
	{								\
		bool r;							\
									\
		r = ck_hs_rebuild(&dict->hs);				\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static size_t					\
	an_dict_size_##NAME(struct an_dict_##NAME *dict)		\
	{								\
									\
		return ck_hs_count(&dict->hs);				\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_reset_##NAME(struct an_dict_##NAME *dict)		\
	{								\
		bool r;							\
									\
		if (OWNED_STRING) {					\
			an_dict_map_##NAME(dict,			\
			    an_dict_string_free_string_cb, NULL);	\
		}							\
									\
		r = ck_hs_reset(&dict->hs);				\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_reset_size_##NAME(struct an_dict_##NAME *dict, size_t new_size)	\
	{								\
		bool r;							\
									\
		if (OWNED_STRING) {					\
			an_dict_map_##NAME(dict,			\
			    an_dict_string_free_string_cb, NULL);	\
		}							\
									\
		r = ck_hs_reset_size(&dict->hs,				\
		    (new_size < 8) ? 16 : 2 * new_size);		\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_grow_##NAME(struct an_dict_##NAME *dict, size_t new_size) \
	{								\
		bool r;							\
									\
		r = ck_hs_grow(&dict->hs,				\
		    (new_size < 8) ? 16 : 2 * new_size);		\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static TYPE *					\
	an_dict_remove_##NAME(struct an_dict_##NAME *dict, const TYPE *object) \
	{								\
		unsigned long hash;					\
									\
		hash = CK_HS_HASH(&dict->hs, an_dict_hash_##NAME, object); \
		return ck_hs_remove(&dict->hs, hash, object);		\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_dict_delete_##NAME(struct an_dict_##NAME *dict, const TYPE *object) \
	{								\
		TYPE *ret;						\
									\
		ret = an_dict_remove_##NAME(dict, object);		\
		if (ret != NULL && (OWNED_STRING)) {			\
			an_dict_string_free_string_cb(ret, NULL);	\
		}							\
									\
		return ret != NULL;					\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_dict_ensure_##NAME(struct an_dict_##NAME *dict,		\
	    INSERT_CONSTNESS TYPE *obj_)				\
	{								\
		TYPE *obj = (TYPE *)obj_;				\
		unsigned long hash;					\
		bool r;							\
									\
		if (OWNED_STRING) {					\
			obj = (void *)an_string_dup((char *)obj);	\
		}							\
									\
		hash = CK_HS_HASH(&dict->hs, an_dict_hash_##NAME, obj);	\
		r = ck_hs_put(&dict->hs, hash, obj);			\
		if (r == false && (OWNED_STRING)) {			\
			an_dict_string_free_string_cb(obj, NULL);	\
		}							\
									\
		return r;						\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_put_##NAME(struct an_dict_##NAME *dict,			\
	    INSERT_CONSTNESS TYPE *obj)					\
	{								\
									\
		(void)an_dict_ensure_##NAME(dict, obj);			\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_insert_##NAME(struct an_dict_##NAME *dict,		\
	    INSERT_CONSTNESS TYPE *obj)					\
	{								\
		bool r;							\
									\
		r = an_dict_ensure_##NAME(dict, obj);			\
		assert(r == true);					\
		return;							\
	}								\
									\
	AN_CC_UNUSED static TYPE *					\
	an_dict_replace_##NAME(struct an_dict_##NAME *dict,		\
	    INSERT_CONSTNESS TYPE *obj_)				\
	{								\
		TYPE *obj = (TYPE *)obj_;				\
		unsigned long hash;					\
		void *ret = NULL;					\
		bool ok;						\
									\
		if (OWNED_STRING) {					\
			obj = (void *)an_string_dup((char *)obj);	\
		}							\
									\
		hash = CK_HS_HASH(&dict->hs, an_dict_hash_##NAME, obj);	\
		ok = ck_hs_set(&dict->hs, hash, obj, &ret);		\
									\
		assert(ok);						\
		return ret;						\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_map_##NAME(struct an_dict_##NAME *dict,			\
	    void (cb)(void *key, void *data), void *data)		\
	{								\
		ck_hs_iterator_t it;					\
		void *entry;						\
									\
		ck_hs_iterator_init(&it);				\
		while (ck_hs_next(&dict->hs, &it, &entry) == true) {	\
			cb(entry, data);				\
		}							\
									\
		return;							\
	}								\
									\
	AN_CC_UNUSED static void					\
	an_dict_iterator_init_##NAME(struct an_dict_iterator_##NAME *it) \
	{								\
									\
		ck_hs_iterator_init(&it->it);				\
		return;							\
	}								\
									\
	AN_CC_UNUSED static bool					\
	an_dict_next_##NAME(struct an_dict_##NAME *dict, 		\
	    struct an_dict_iterator_##NAME *it,				\
	    TYPE **entry)						\
	{								\
									\
		return ck_hs_next(&dict->hs, &it->it, (void **)entry);	\
	}

#define an_dict_map(NAME, DICT, CB, DATA)				\
	an_dict_map_##NAME((DICT),					\
	    AN_CC_CAST_IF_COMPATIBLE((CB),				\
		void (*)(an_dict_value_##NAME##_t *, __typeof__(DATA)),	\
		void (*)(void *, void *)),				\
	    (DATA))

#define AN_DICT_FOREACH_IMPL(NAME, DICT, ENTRY, GENSYM)			\
	struct an_dict_##NAME *dict_##NAME##_##GENSYM = (DICT);		\
	an_dict_value_##NAME##_t *ENTRY = NULL;				\
	ck_hs_iterator_t it_##NAME##_##GENSYM = CK_HS_ITERATOR_INITIALIZER; \
									\
	while (ck_hs_next(&dict_##NAME##_##GENSYM->hs,			\
	    &it_##NAME##_##GENSYM, (void **)&ENTRY) == true)

#define AN_DICT_CONTINUE_ITER_IMPL(NAME, DICT, ENTRY, ITER, GENSYM)	\
	struct an_dict_##NAME *dict_##NAME##_##GENSYM = (DICT);		\
	an_dict_value_##NAME##_t *ENTRY = NULL;				\
									\
	while (ck_hs_next(&dict_##NAME##_##GENSYM->hs,			\
	    (void *)&ITER, (void **)&ENTRY) == true)

#define AN_DICT_FOREACH(NAME, DICT, ENTRY) AN_DICT_FOREACH_IMPL(NAME, DICT, ENTRY, __COUNTER__)

#define AN_DICT_CONTINUE_ITER(NAME, DICT, ENTRY, ITER) AN_DICT_CONTINUE_ITER_IMPL(NAME, DICT, ENTRY, ITER, __COUNTER__)

#endif /* !AN_DICT_COMMON_H */
