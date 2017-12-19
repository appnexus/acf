#ifndef _COMMON_AN_SMR_H
#define _COMMON_AN_SMR_H
#include <stddef.h>
#include <stdbool.h>

#include "autoconf_globals.h"
#include "common/an_hook.h"
#include "common/an_malloc.h"

/*
 * Pause memory reclamation for this thread.
 *
 * When an_smr_poll() is called by this thread, it will no-op and return false.
 */
void an_smr_pause(void);

/*
 * Resume memory reclamation for this thread.
 *
 * an_smr_poll() will no longer be forced to no-op when called by this thread.
 */
void an_smr_resume(void);

/**
 * Get the current pause depth for this thread.
 */
size_t an_smr_get_pause_depth(void);

#if defined(DISABLE_SMR)
typedef struct an_smr_record { } an_smr_record_t;
typedef struct an_smr_section { } an_smr_section_t;
typedef struct an_smr { } an_smr_t;

#define an_smr_begin(z)						\
	do {							\
		(void)(z);					\
	} while (0)
#define an_smr_end(z)						\
	do {							\
		(void)(z);					\
	} while (0)
#define an_smr_unregister(y)					\
	do {							\
		(void)(y);					\
	} while (0)
#define an_smr_register(y)					\
	do {							\
		(void)(y);					\
	} while (0)

#elif defined(USE_EPOCH)
#include <ck_epoch.h>

typedef ck_epoch_record_t an_smr_record_t;
typedef ck_epoch_section_t an_smr_section_t;
typedef ck_epoch_t an_smr_t;

void an_smr_begin(an_smr_section_t *section);
void an_smr_end(an_smr_section_t *section);

#define an_smr_register(y) ck_epoch_register(&global_smr, y)
#define an_smr_unregister(y) ck_epoch_unregister(&global_smr, y)

#elif defined(USE_RTBR)
#include "common/rtbr/rtbr.h"

typedef const struct an_rtbr_record *an_smr_record_t;
typedef struct an_rtbr_section an_smr_section_t;
typedef struct { } an_smr_t;

void an_smr_begin(an_smr_section_t *section);
void an_smr_end(an_smr_section_t *section);

#define an_smr_register(y)					\
	do {							\
		const struct an_rtbr_record **dst = (y);	\
								\
		if (*dst == NULL) {				\
			*dst = an_rtbr_self();			\
		}						\
	} while (0)
#define an_smr_unregister(y) do { (void)(y); } while (0)
#endif /* defined(USE_SMR) */

/**
 * @brief determines if smr record has any active read sections
 * @param smr smr record to check
 *
 * @return true if there is at least one active smr read section
 */
bool an_smr_is_active(const an_smr_record_t *smr);

bool an_smr_entry_pending_destruction(void *obj);

/* really declared in an_thread.h, but circular deps. */
extern __thread unsigned int an_thread_current_id;

#if defined(__COVERITY__)
#define an_smr_call(OBJECT, FUNCTION)					\
	do {								\
		(FUNCTION)((OBJECT));					\
	} while (0);
#else
#define an_smr_call(OBJECT, FUNCTION)					\
	({								\
		void *AN_SMR_CALL_obj = (OBJECT);			\
		void (*AN_SMR_CALL_func)(void *) =			\
		    AN_CC_CAST_CB(void, (FUNCTION), (OBJECT));		\
									\
		do {							\
			AN_HOOK(an_smr_call, disable) {			\
				if (an_thread_current_id == 0) {	\
					break;				\
				}					\
			}						\
									\
			an_smr_call_inner(AN_SMR_CALL_obj, AN_SMR_CALL_func); \
		} while (0);						\
	})
#endif

void an_smr_call_inner(void* obj, void(*func)(void *));
void an_smr_free(an_malloc_token_t token, void *obj);

void an_smr_init();
bool an_smr_poll();
void an_smr_synchronize(void);

unsigned int an_smr_n_pending(an_smr_record_t *record);

struct an_thread; /* Forward declaration for function below */
void an_smr_handler_http(struct evhttp_request *request, struct an_thread *cursor);
#endif /* _COMMON_AN_SMR_H */
