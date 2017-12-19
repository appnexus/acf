#ifndef AN_HOOK_H
#define AN_HOOK_H

#ifndef AN_HOOK_ENABLED
# if defined(__GNUC__)
#  define AN_HOOK_ENABLED 1
# else
#  define AN_HOOK_ENABLED 0
# endif
#endif

#include <evhttp.h>

#include "common/an_cc.h"

#if !AN_HOOK_ENABLED

#define AN_HOOK(KIND, NAME) if (1)
#define AN_HOOK_ON(KIND, NAME) if (1)
#define AN_HOOK_UNSAFE(KIND, NAME) if (0)
#define AN_HOOK_FLIP(KIND, NAME) if (0)
#define AN_HOOK_DUMMY(KIND) do { } while (0)

static inline int
an_hook_activate(const char *regex)
{

	(void)regex;
	return 0;
}

static inline int
an_hook_deactivate(const char *regex)
{

	(void)regex;
	return 0;
}

static inline int
an_hook_unhook(const char *regex)
{

	(void)regex;
	return 0;
}

static inline int
an_hook_rehook(const char *regex)
{

	(void)regex;
	return 0;
}

#define an_hook_activate_kind(KIND, PATTERN) an_hook_activate((PATTERN))
#define an_hook_deactivate_kind(KIND, PATTERN) an_hook_deactivate((PATTERN))

#else /* AN_HOOK_ENABLED */

/**
 * @brief Conditionally enable a block of code with a named hook.
 *
 * Usage:
 *
 *  AN_HOOK(hook_kind, hook_name) {
 *          conditionally-enabled code;
 *  }
 *
 * It should always be *safe* (if inefficient) for an AN_HOOK block to
 * be executed.
 *
 * Implementation details:
 *
 *  The first line introduces a local label just before a 5-byte
 *   testl $..., %eax.
 *  We use that instruction instead of a nop (and declare a clobber on
 *  EFLAGS) to simplify hotpatching with concurrent execution: we
 *  can turn TEST into a JMP REL to foo_hook with a byte write.
 *
 *  The rest stashes metadata in a couple sections.
 *
 *   1. the name of the hook, in the normal read-only section.
 *   2. the hook struct:
 *        - a pointer to the hook instruction;
 *        - the address of the destination;
 *        - a pointer to the hook name (as a C string).
 *   3. a reference to the struct, in the kind's custom section.
 *
 *  The if condition tells the compiler to skip the next block of code
 *  (the conditional is false) and to consider it unlikely to be
 *  executed, despite the asm-visible label.
 *
 * Numerical labels (from 1 to 9) can be repeated however many times
 * as necessary; "1f" refers to the next label named 1 (1 forward),
 * while "1b" searches backward.
 *
 * The push/pop section stuff gives us out of line metadata from a
 * contiguous macro expansion.
 *
 * Inspired by tracepoints in the Linux
 * kernel. <http://lwn.net/Articles/350714/>
 *
 * XXX SUPER IMPORTANT NOTE XXX
 *
 * This code may trigger a hardware bug in Intel chips (erratum 54)
 *  <https://lkml.org/lkml/2009/3/2/194> (H/T Mathieu Desnoyer).
 * The bug is unlikely to be fixed and appears related to instruction
 * predecoding and rollbacks for OOO execution.  We modify code with
 * a single atomic byte write, so there is no chance of executing
 * *bad* code.  However, if the bug is triggered, the thread will hit
 * a protection fault.  For our use case, it's fine: odds are low and
 * we're running on hundreds of machines.
 */

#ifndef AN_HOOK_FALLBACK
# if defined(__clang__) || defined(__COVERITY__)
#   define AN_HOOK_FALLBACK 1
# else
#   define AN_HOOK_FALLBACK 0
# endif
#endif

#if !AN_HOOK_FALLBACK
#define AN_HOOK_VALUE_ACTIVE 0xe9 /* jmp rel 32 */
#define AN_HOOK_VALUE_INACTIVE 0xa9 /* testl $, %eax */

#define AN_HOOK_IMPL_(OPCODE, INITIAL, FLIPPED, KIND, NAME, FILE, LINE, GENSYM)	\
	if (AN_CC_UNLIKELY(({						\
	    asm goto ("1:\n\t"						\
		      ".byte "#OPCODE"\n\t"				\
		      ".long %l[an_hook_"#GENSYM"_label] - (1b + 5)\n\t"\
									\
		      ".pushsection .rodata\n\t"			\
		      "2: .asciz \"" #KIND ":" #NAME "@" FILE ":" #LINE "\"\n\t" \
		      ".popsection\n\t"					\
									\
		      ".pushsection an_hook_list,\"a\",@progbits\n\t"	\
		      "3:\n\t"						\
		      ".quad 1b\n\t"					\
		      ".quad %l[an_hook_"#GENSYM"_label]\n\t" 		\
		      ".quad 2b\n\t"					\
		      ".byte "#INITIAL"\n\t"				\
		      ".byte "#FLIPPED"\n\t"				\
		      ".fill 6\n\t"					\
		      ".popsection\n\t"					\
									\
		      ".pushsection an_hook_"#KIND"_list,\"a\",@progbits\n\t" \
		      ".quad 3b\n\t"					\
		      ".popsection"					\
		      ::: "cc" : an_hook_##GENSYM##_label);		\
	    0;})))							\
	an_hook_##GENSYM##_label:
#else /* Fallback implementation */
/*
 * 0xF4 is HLT, a privileged instruction that shuts down the core.
 * GCC shouldn't ever generate that.
 */
#define AN_HOOK_VALUE_ACTIVE 0xF4
#define AN_HOOK_VALUE_INACTIVE 0

#define AN_HOOK_IMPL_(DEFAULT, INITIAL, FLIPPED, KIND, NAME, FILE, LINE, GENSYM) \
	if (AN_CC_UNLIKELY(({						\
	    uint8_t r;							\
	    asm ("1:\n\t"						\
		 "movb $"#DEFAULT", %0\n\t"				\
									\
		 ".pushsection .rodata\n\t"				\
		 "2: .asciz \"" #KIND ":" #NAME "@" FILE ":" #LINE "\"\n\t" \
		 ".popsection\n\t"					\
									\
		 ".pushsection an_hook_list,\"a\",@progbits\n\t"	\
		 "3:\n\t"						\
		 ".quad 1b\n\t"						\
		 ".quad 0\n\t"						\
		 ".quad 2b\n\t"						\
		 ".byte "#INITIAL"\n\t"					\
		 ".byte "#FLIPPED"\n\t"					\
		 ".fill 6\n\t"						\
		 ".popsection\n\t"					\
									\
		 ".pushsection an_hook_"#KIND"_list,\"a\",@progbits\n\t"\
		 ".quad 3b\n\t"						\
		 ".popsection"						\
		:"=r"(r));						\
	    r;})))
#endif /* AN_HOOK_FALLBACK */

#define AN_HOOK_IMPL(OPCODE, INITIAL, FLIPPED, KIND, NAME, FILE, LINE, GENSYM) \
	AN_HOOK_IMPL_(OPCODE, INITIAL, FLIPPED, KIND, NAME, FILE, LINE, GENSYM)

/**
 * Defaults to inactive, unless the an_hook machinery can't get to it,
 * then it's always active.
 */
#define AN_HOOK(KIND, NAME) AN_HOOK_IMPL(AN_HOOK_VALUE_ACTIVE, AN_HOOK_VALUE_INACTIVE, \
    0, KIND, NAME, __FILE__, __LINE__, __COUNTER__)

/**
 * Same as AN_HOOK, but defaults to active.
 */
#define AN_HOOK_ON(KIND, NAME) AN_HOOK_IMPL(AN_HOOK_VALUE_ACTIVE, AN_HOOK_VALUE_ACTIVE, \
    0, KIND, NAME, __FILE__, __LINE__, __COUNTER__)

/**
 * Defaults to inactive, even if unreachable by the an_hook machinery.
 */
#define AN_HOOK_UNSAFE(KIND, NAME) AN_HOOK_IMPL(AN_HOOK_VALUE_INACTIVE, AN_HOOK_VALUE_INACTIVE, \
    0, KIND, NAME, __FILE__, __LINE__, __COUNTER__)

/**
 * Hook that should be skipped to activate the corresponding code
 * sequence.  Useful for code that is usually executed.
 *
 * Defaults to skipped hook.
 */
#define AN_HOOK_FLIP(KIND, NAME) AN_HOOK_IMPL(AN_HOOK_VALUE_INACTIVE, AN_HOOK_VALUE_INACTIVE, \
    1, KIND, NAME, __FILE__, __LINE__, __COUNTER__)

/**
 * Ensure a hook point exists for kind KIND.
 */
#define AN_HOOK_DUMMY(KIND)						\
	do {								\
		AN_HOOK_UNSAFE(KIND, dummy) {				\
			asm volatile("");				\
		}							\
	} while (0)

/**
 * @brief activate all hooks that match @a regex, regardless of the kind.
 * @return negative on failure, 0 on success.
 */
int an_hook_activate(const char *regex);

/**
 * @brief deactivate all hooks that match @a regex, regardless of the kind.
 * @return negative on failure, 0 on success.
 */
int an_hook_deactivate(const char *regex);

/**
 * @brief disable hooking for all hooks that match @a regex, regardless of the kind.
 * @return negative on failure, 0 on success.
 *
 * If a hook is unhooked, activating that hook does nothing and does
 * not increment the activation count.
 */
int an_hook_unhook(const char *regex);

/**
 * @brief reenable hooking all hooks that match @a regex, regardless of the kind.
 * @return negative on failure, 0 on success.
 */
int an_hook_rehook(const char *regex);

/**
 * @brief (de)activate all hooks of kind @a KIND; if @a PATTERN is
 *  non-NULL, the hook names must match @a PATTERN as a regex.
 */
#define an_hook_activate_kind(KIND, PATTERN)				\
	do {								\
		int an_hook_activate_kind_inner(const void **start,	\
		    const void **end, const char *regex);		\
		extern const void *__start_an_hook_##KIND##_list[];	\
		extern const void *__stop_an_hook_##KIND##_list[]; 	\
									\
		an_hook_activate_kind_inner(__start_an_hook_##KIND##_list, \
		    __stop_an_hook_##KIND##_list,			\
		    (PATTERN));						\
	} while (0)

#define an_hook_deactivate_kind(KIND, PATTERN)				\
	do {								\
		int an_hook_deactivate_kind_inner(const void **start,	\
		    const void **end, const char *regex);		\
		extern const void *__start_an_hook_##KIND##_list[];	\
		extern const void *__stop_an_hook_##KIND##_list[]; 	\
									\
		an_hook_deactivate_kind_inner(__start_an_hook_##KIND##_list, \
		    __stop_an_hook_##KIND##_list,			\
		    (PATTERN));						\
	} while (0)
#endif /* AN_HOOK_ENABLED */

#ifndef DISABLE_DEBUG
# define AN_HOOK_DEBUG(NAME) AN_HOOK_ON(debug, NAME)
#else
# define AN_HOOK_DEBUG(NAME) AN_HOOK_UNSAFE(debug, NAME)
#endif

void an_hook_handler_http_enable(struct evhttp *httpd);

void an_hook_init_lib();

void an_hook_utrace_entry(const char *name, ...);
#endif /* !AN_HOOK_H */
