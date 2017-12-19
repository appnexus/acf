#ifndef AN_UTIL_H
#define AN_UTIL_H

#include "an_cc.h"

AN_EXTERN_C_BEGIN

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "acf_export.h"

#ifndef __cplusplus

#ifndef max
#define max(a, b)					\
	({						\
		__typeof__(a) _a = (a);			\
		__typeof__(b) _b = (b);			\
		_a > _b ? _a : _b;			\
	})
#endif

#ifndef min
#define min(a, b)					\
	({						\
		__typeof__(a) _a = (a);			\
		__typeof__(b) _b = (b);			\
		_b > _a ? _a : _b;			\
	})
#endif

#endif /* __cplusplus */

/* from bithacks */
ACF_EXPORT uint64_t an_next_power_of_2(uint64_t x);

ACF_EXPORT void an_safe_fill(char dest[], const char *source, size_t max_len);

ACF_EXPORT void an_safe_strncpy(char *dest, const char *src, size_t num);

/**
 * print the current time in log format
 *
 * @param tm time struct containing the current time
 * @param log_time buffer to store time in
 * @param size of the buffer
 */
ACF_EXPORT void an_time_print(struct tm *tm, char log_time[], int len);

ACF_EXPORT void an_time_to_str(time_t time, char log_time[], int len);

AN_EXTERN_C_END

#endif
