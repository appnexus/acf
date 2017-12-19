#ifndef _AN_CC_H
#define _AN_CC_H

#ifdef __cplusplus
#define AN_EXTERN_C_BEGIN	extern "C" {
#define AN_EXTERN_C_END		}
#else
#define AN_EXTERN_C_BEGIN
#define AN_EXTERN_C_END
#endif

#ifndef AN_CC_PACKED
#if defined(DISABLE_SMR)
#define AN_CC_PACKED __attribute__((packed))
#else
#define AN_CC_PACKED
#endif /* DISABLE_SMR */
#endif /* AN_CC_PACKED */

#ifndef AN_CC_UNUSED
#define AN_CC_UNUSED __attribute__((unused))
#endif

#ifndef AN_CC_USED
#define AN_CC_USED __attribute__((used))
#endif

#if defined(__GNUC__)
#define AN_CC_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#define AN_CC_GCC_VERSION_AT_LEAST(major, minor, patch) (AN_CC_GCC_VERSION >= (major * 10000 + minor * 100 + patch))
#else /* __GNUC__ not defined */
#define AN_CC_GCC_VERSION 0
#define AN_CC_GCC_VERSION_AT_LEAST(major, minor, patch) 0
#endif /* __GNUC__ */

#if defined(__GNUC__) || defined(__clang__)
#define AN_CC_LIKELY(x) __builtin_expect(!!(x), 1)
#define AN_CC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define AN_CC_INLINE inline __attribute__((always_inline))
#define AN_CC_NOINLINE __attribute__((noinline))
#define AN_CC_PREFETCH(ADDR, ...) __builtin_prefetch((ADDR), __VA_ARGS__)
/* Cast X to OUTPUT if it is compatible with EXPECTED. */
#define AN_CC_CAST_IF_COMPATIBLE(X, EXPECTED, OUTPUT)		       \
	(__builtin_choose_expr(					       \
	    __builtin_types_compatible_p(__typeof__(0 ? (X) : (X)),    \
		EXPECTED),					       \
		(OUTPUT)(X),					       \
		(X)))

/*
 * Cast CB from a function that accepts ARG and returns an RTYPE to a
 * generic function that returns an RTYPE.
 */
#define AN_CC_CAST_CB(RTYPE, CB, ARG)					\
	AN_CC_CAST_IF_COMPATIBLE((CB), RTYPE (*)(__typeof__(ARG)), RTYPE (*)(void *))
/* ARG1 is the first fixed-type parameter to CB, ARG2 will be cast */
#define AN_CC_CAST_CB2(RTYPE, CB, ARG1, ARG2)				\
	AN_CC_CAST_IF_COMPATIBLE((CB), RTYPE (*)(ARG1, __typeof__(ARG2)), RTYPE (*)(ARG1, void *))
/* ARG1 and ARG2 are the first and second fixed-type parameters to CB, respectively, and ARG3 will be cast */
#define AN_CC_CAST_CB3(RTYPE, CB, ARG1, ARG2, ARG3)			\
	AN_CC_CAST_IF_COMPATIBLE((CB), RTYPE (*)(ARG1, ARG2, __typeof__(ARG3)), RTYPE (*)(ARG1, ARG2, void *))

#define AN_CC_CAST_CONST_CB(RTYPE, CB, ELEM)                            \
        AN_CC_CAST_IF_COMPATIBLE((CB), RTYPE (*)(__typeof__(ELEM) const *), RTYPE (*)(const void *))

#define AN_CC_FLATTEN __attribute__((flatten))
#define AN_CC_FORCE_EVAL(X) do { asm(""::"r"(X)); } while (0)
#define AN_CC_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else /* !__GNUC__ && !__clang__ */
#define AN_CC_INLINE inline
#define AN_CC_NOINLINE
#define AN_CC_LIKELY(x) (x)
#define AN_CC_UNLIKELY(x) (x)
#define AN_CC_PREFETCH(ADDR, ...) do { (void)(ADDR); } while (0)
#define AN_CC_CAST_IF_COMPATIBLE(X, EXPECTED, OUTPUT) ((OUTPUT)(X))
#define AN_CC_CAST_CB(RTYPE, CB, ARG) ((RTYPE (*)(void *))(CB))
#define AN_CC_FLATTEN
#define AN_CC_FORCE_EVAL(X) (X)
#define AN_CC_WARN_UNUSED_RESULT
#endif /* __GNUC__ || __clang__ */

/* Cast a comparator function for elements of type TYPE to a generic comparator. */
#define AN_CC_CAST_COMPARATOR(CMP, TYPE)				\
	AN_CC_CAST_IF_COMPATIBLE((CMP), int (*)(TYPE const *, TYPE const *), \
	    int (*)(const void *, const void *))

#ifndef __has_extension
#define __has_extension		__has_feature
#endif
#ifndef __has_feature
#define __has_feature(x)	0
#endif

#ifndef AN_CC_NO_SANITIZE
#if __has_feature(address_sanitizer)
#define AN_CC_NO_SANITIZE __attribute__((no_sanitize_address))
#else
#define AN_CC_NO_SANITIZE
#endif /* feature check */
#endif /* AN_CC_NO_SANITIZE */

#if !__has_extension(c_static_assert)
#if (defined(__cplusplus) && __cplusplus >= 201103L) || \
    __has_extension(cxx_static_assert)
#define _Static_assert(x, y)    static_assert(x, y)
#elif defined(__COUNTER__)
#define _Static_assert(x, y)    __Static_assert(x, __COUNTER__)
#define __Static_assert(x, y)   ___Static_assert(x, y)
#define ___Static_assert(x, y)  typedef char __assert_ ## y[(x) ? 1 : -1] AN_CC_UNUSED
#else
#define _Static_assert(x, y)    struct __hack
#endif
#endif

#ifndef AN_CC_PACKED
#if defined(DISABLE_SMR)
#define AN_CC_PACKED __attribute__((packed))
#else
#define AN_CC_PACKED
#endif /* DISABLE_SMR */
#endif /* AN_CC_PACKED */

#ifndef AN_CC_UNUSED
#define AN_CC_UNUSED __attribute__((unused))
#endif

#ifndef AN_CC_USED
#define AN_CC_USED __attribute__((used))
#endif

/*
 * BUILD_ASSERT, BUILD_ASSERT_OR_ZERO, _array_size_check were derived from
 * Rusty Russell's http://git.ozlabs.org/
 */

/* Compiler will fail if condition isn't true */
#define BUILD_ASSERT(cond)      \
        do { (void) sizeof(char [1 - 2*!(cond)]); } while(0)

/* Compiler will fail if condition isn't true */
#define BUILD_ASSERT_OR_ZERO(cond)      \
        (sizeof(char [1 - 2*!(cond)]) - 1)

/* 0 or fail to build */
#define _array_size_check(arr)  \
        BUILD_ASSERT_OR_ZERO(!__builtin_types_compatible_p(__typeof__(arr), \
                                __typeof__(&(arr)[0])))

/* number of elements in array, with type checking */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + _array_size_check(arr))

#ifndef STRINGIFY
#define XSTRINGIFY(x) #x
#define STRINGIFY(x) XSTRINGIFY(x)
#endif

#ifndef AN_CC_WEAK
#define AN_CC_WEAK __attribute__((__weak__))
#endif

#endif /* _AN_CC_H */
