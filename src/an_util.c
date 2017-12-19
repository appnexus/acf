#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>

#include "an_util.h"

/* from bithacks */
uint64_t
an_next_power_of_2(uint64_t x)
{

        --x;
        x |= x >> 1;
        x |= x >> 2;
        x |= x >> 4;
        x |= x >> 8;
        x |= x >> 16;
        x |= x >> 32;

        return ++x;
}

void
an_safe_fill(char dest[], const char *source, size_t max_len)
{

	if (max_len == 0 || source == NULL || *source == '\0') {
		dest[0] = '\0';
		return;
	}


	if (max_len >= 4 && strcasecmp(source, "NULL") == 0) {
		dest[0] = '\0';
		return;
	}

	/* leave room for NULL terminator */
	strncpy(dest, source, max_len - 1);
	dest[max_len - 1] = '\0';
}

void
an_safe_strncpy(char *dest, const char *src, size_t num)
{

	if (dest == NULL || num == 0) {
		return;
	}

	if (src == NULL) {
		dest[0] = '\0';
		return;
	}

	strncpy(dest, src, num-1);
	dest[num-1] = '\0';
}

/**
 * print the current time in log format
 *
 * @param tm time struct containing the current time
 * @param log_time buffer to store time in
 * @param size of the buffer
 */
void
an_time_print(struct tm *tm, char log_time[], int len)
{

        snprintf(log_time, len, "%04d-%02d-%02d %02d:%02d:%02d",
	    tm->tm_year + 1900, tm->tm_mon + 1,
	    tm->tm_mday, tm->tm_hour, tm->tm_min,
	    tm->tm_sec);
}


void
an_time_to_str(time_t time, char log_time[], int len)
{
	static __thread time_t cached_time = 0;
	static __thread struct tm cached_tm;

	/* avoid calling localtime_r unless our time has changed in this thread */
	if (cached_time != time) {
		cached_time = time;
		localtime_r(&cached_time, &cached_tm);
	}

	an_time_print(&cached_tm, log_time, len);
}
