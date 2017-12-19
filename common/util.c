#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <event.h>
#include <evhttp.h>
#include <fcntl.h>
#include <float.h>
#include <modp_ascii.h>
#include <modp_burl.h>
#include <modp_numtoa.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <acf/an_charset.h>

#include "common/an_idna.h"
#include "common/an_malloc.h"
#include "common/an_string.h"
#include "common/an_syslog.h"
#include "common/an_time.h"
#include "common/an_thread.h"
#include "common/assert_dev.h"
#include "common/common_types.h"
#include "common/debug.h"
#include "common/libevent_extras.h"
#include "common/server_config.h"
#include "common/util.h"

#define MICROS_PER_SEC 1000000LL

int
int64_val_comparator(int64_t one, int64_t two)
{

	if (one < two) {
		return -1;
	}

	if (one > two) {
		return 1;
	}

	return 0;
}

_Static_assert(sizeof(unsigned long long) >= sizeof(uint64_t),
    "uint64_t must fit inside unsigned long long or next_power_of_2 is broken");

uint64_t
next_power_of_2(uint64_t x)
{
	unsigned long long ull_bits = (sizeof(unsigned long long) * CHAR_BIT);

	/*
	 * our old implementation returned 0 on input of 0,
	 * and clzll is undefined on input of 0, so we may as well preserve
	 * bithacks's behavior on 0
	 *
	 * on input of 1 we do clzll(0) which is undefined, so return 1
	 */
	return x <= 1 ? x : 0x1ULL << (ull_bits - __builtin_clzll(x - 1ULL));
}

const char *
strnprefix(const char *str, const char *prefix, size_t prefixlen)
{

	if (strncmp(str, prefix, prefixlen) != 0) {
		return NULL;
	}

	return str + prefixlen;
}

const char *
ltrim(const char *str)
{

	while (isspace(*str)) {
		str++;
	}

	return str;
}

static int
is_negative_int_str(const char *str)
{
	str = ltrim(str);

	if (*str == '-') {
		return 1;
	}

	return 0;
}

int
str2float(const char *str, float *result, float default_value)
{
	char *end_ptr;
	long double num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtold(str, &end_ptr);
	if (errno != 0) {
		debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (num > FLT_MAX || num < -FLT_MAX || (num < FLT_MIN && num > 0) || (num > -FLT_MIN && num < 0)) {
		debug(4, "Number is greater than max float or min float values: %s \n", str);
		*result = default_value;
		return 1;
	}

	if (end_ptr == str) {
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = (float)num;
	return 0;
}

int
str2double(const char *str, double *result, double default_value)
{
	char *end_ptr;
	long double num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtold(str, &end_ptr);
	if (errno != 0) {
		debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (num > DBL_MAX || num < -DBL_MAX || (num < DBL_MIN && num > 0) || (num > -DBL_MIN && num < 0)) {
		debug(4, "Number is greater than max double or min double values: %s \n", str);
		*result = default_value;
		return 1;
	}

	if (end_ptr == str) {
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = (double)num;
	return 0;
}

int
str2int(const char *str, int *result, int default_value)
{
	char *end_ptr;
	long long int num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtoll(str, &end_ptr, 10);
	if (errno) {
		debug(4, "Could not convert %s: %s \n",
			  str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (num > INT_MAX || num < INT_MIN) {
		debug(4, "Number is greater than max int or min int values: %s \n", str);
		*result = default_value;
		return 1;
	}

	if (end_ptr == str){
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = (int) num;
	return 0;
}

int
str2uint8(const char *str, uint8_t *result, uint8_t default_value)
{
	char *end_ptr;
	unsigned long int num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	if (is_negative_int_str(str)) {
		debug(4, "Can't convert negative int to unsigned\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtoul(str, &end_ptr, 10);
	if (errno) {
		debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (num > UINT8_MAX) {
		debug(4, "Number is greater than max int: %s \n", str);
		*result = default_value;
		return 1;
	}

	if (end_ptr == str){
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = (uint8_t) num;
	return 0;
}

int
str2uint16(const char *str, uint16_t *result, uint16_t default_value)
{
	char *end_ptr;
	unsigned long int num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	if (is_negative_int_str(str)) {
		debug(4, "Can't convert negative int to unsigned\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtoul(str, &end_ptr, 10);
	if (errno) {
		debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (num > UINT16_MAX) {
		debug(4, "Number is greater than max int: %s \n", str);
		*result = default_value;
		return 1;
	}

	if (end_ptr == str){
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = (uint16_t) num;
	return 0;
}

int
str2uint32(const char *str, uint32_t *result, uint32_t default_value)
{
	char *end_ptr;
	unsigned long int num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	if (is_negative_int_str(str)) {
		debug(4, "Can't convert negative int to unsigned\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtoul(str, &end_ptr, 10);
	if (errno) {
		debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (num > UINT_MAX) {
		debug(4, "Number is greater than max int: %s \n", str);
		*result = default_value;
		return 1;
	}

	if (end_ptr == str){
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = (uint32_t) num;
	return 0;
}

int
str2uint64(const char *str, uint64_t *result, uint64_t default_value)
{
	char *end_ptr;
	unsigned long long num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	if (is_negative_int_str(str)) {
		debug(4, "Can't convert negative int to unsigned\n");
		*result = default_value;
		return 1;
	}

        errno = 0;
        num = strtoull(str, &end_ptr, 10);
        if (errno) {
                debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
                *result = default_value;
                return 1;
        }

        if (end_ptr == str){
                debug(4, "Found no digits when converting: %s \n", str);
                *result = default_value;
                return 1;
        }

        *result = num;
        return 0;
}

int
str2int32(const char *str, int32_t *result, int32_t default_value)
{
	char *end_ptr;
	long long num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtol(str, &end_ptr, 10);
	if (errno) {
		debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (end_ptr == str){
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = num;
	return 0;
}

int
str2int64(const char *str, int64_t *result, int64_t default_value)
{
	char *end_ptr;
	long long num;

	if (str == NULL) {
		debug(4, "String is null, can't convert\n");
		*result = default_value;
		return 1;
	}

	errno = 0;
	num = strtoll(str, &end_ptr, 10);
	if (errno) {
		debug(4, "Could not convert %s: %s \n", str, an_strerror(errno));
		*result = default_value;
		return 1;
	}

	if (end_ptr == str){
		debug(4, "Found no digits when converting: %s \n", str);
		*result = default_value;
		return 1;
	}

	*result = num;
	return 0;
}

bool
str_empty(const char *str)
{

	if (str == NULL || *str == '\0') {
		return true;
	}

	return false;
}

size_t
hex2binary_output_len(size_t hex_len)
{

	return (hex_len / 2 + 2);
}

size_t
hex2binary(unsigned char *binary, char *hex, size_t hex_len)
{

	if (hex == NULL) {
		return 0;
	}

	unsigned char *p;
	char *tmp2 = "z";

	char save;
	unsigned int i = 0;
	for (p = binary; i < hex_len / 2; p++, i++, hex += 2) {
		if (i < (hex_len / 2)) {
			save = *(hex + 2);
			*(hex + 2) = '\0';
		}

		errno = 0;
		*p = strtoul(hex, &tmp2, 16);
		if (i < (hex_len / 2)) {
			*(hex + 2) = save;
		}
	}

	return p - binary;
}

size_t
binary2hex_output_len(size_t binary_len)
{

	return 2 * binary_len + 1;
}

size_t
binary2hex(char *hex, const unsigned char *binary, size_t binary_len)
{

	if (binary == NULL) {
		return 0;
	}

	size_t j = 0;
	for (size_t i = 0; i < binary_len; i++) {
		j += sprintf(&hex[j], "%02x", binary[i]);
	}
	hex[j] = '\0';

	return j;
}

#ifndef AN_LOGTIME_FMT
#define AN_LOGTIME_FMT "%Y-%m-%d %H:%M:%S"
#endif /* AN_LOGTIME_FMT */

time_t
an_logtime(const char *timestamp)
{
	struct tm tm;

	memset(&tm, 0, sizeof(tm));
	strptime(timestamp, AN_LOGTIME_FMT, &tm);
	return mktime(&tm);
}

void
get_timestamp(char log_time[], int len)
{
	time_t rawtime;

	time(&rawtime);
	time_to_str(rawtime, log_time, len);
}

void
time_to_str(time_t time, char log_time[], int len)
{
	static __thread time_t cached_time;
	static __thread char cached_log_time[256];

	if (cached_time != time) {
		struct tm tm;
		localtime_r(&time, &tm);
		cached_time = time;
		snprintf(cached_log_time, sizeof(cached_log_time), "%04d-%02d-%02d %02d:%02d:%02d",
		    tm.tm_year + 1900, tm.tm_mon + 1,
		    tm.tm_mday, tm.tm_hour, tm.tm_min,
		    tm.tm_sec);

	}
	strncpy(log_time, cached_log_time, len);
}

uint8_t
get_current_hour_utc(void)
{
	time_t now = an_time(true);

	return (uint8_t)((now % SECONDS_PER_DAY) / SECONDS_PER_HOUR);
}

bool
is_whitespace_str(const char *str, size_t len)
{
	const char *end;

	if (str == NULL || len == 0) {
		return true;
	}

	end = str + len;

	while (str < end && isspace(*str)) {
		str++;
	}

	if (str == end) {
		return true;
	}

	return false;
}

char *
trim(char *str)
{
	char *end;

	if (str == NULL) {
		return NULL;
	}

	while (isspace(*str)) {
		str++;
	}

        if (*str == '\0') {
                return str;
        }


	end = str + strlen(str) - 1;
	while (end > str && isspace(*end)){
		end--;
	}

	*(end +1 ) = '\0';

	return str;
}

void
trim_inplace(char *str)
{
        char *end;
        char *p = str;
        size_t len;

	if (str == NULL) {
		return;
	}

        while (isspace(*p)) {
                p++;
        }

        /* All zeros */
        if (*p == '\0') {
                *str = '\0';
                return;
        }

        len = strlen(p);
        memmove(str, p, len);

        end = str + len - 1;
        while (end > str && isspace(*end)) {
                end--;
        }

        *(end + 1) = '\0';

        return;
}

void
tolower_str(char *str)
{
	if (str == NULL) {
		return;
	}

	modp_tolower(str, strlen(str));
}

void
toupper_str(char *str)
{
	if (str == NULL) {
		return;
	}

	modp_toupper(str, strlen(str));
}

void
safe_fill(char dest[], const char *source, size_t max_len)
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

char *
safe_dup(const char *source, size_t max_len)
{

	if (source == NULL || *source == '\0' || (max_len == 4 && strcasecmp(source, "NULL") == 0)) {
		return NULL;
	}

	return strndup(source, max_len);
}

void
an_string_escaped(char *dest, const char *src, size_t len)
{

	if (str_empty(src) == true) {
		*dest = '\0';
		return;
	}

	for (size_t i = 0; i < len; i++) {
		ptrdiff_t delta = 2;

		switch (src[i]) {
		case '\b':
			memcpy(dest, "\\b", delta);
			break;

		case '\n':
			memcpy(dest, "\\n", delta);
			break;

		case '\r':
			memcpy(dest, "\\r", delta);
			break;

		case '\t':
			memcpy(dest, "\\t", delta);
			break;

		case '"':
			memcpy(dest, "\\\"", delta);
			break;

		case '\\':
			memcpy(dest, "\\\\", delta);
			break;

		default:
			*dest = src[i];
			delta = 1;
		}

		dest += delta;
	}

	*dest = '\0';

	return;
}

size_t
an_string_escaped_len(const char *src)
{

	if (str_empty(src) == true) {
		return 1;
	}

	return strlen(src) * 2 + 1;
}

size_t
an_str_replace_char(char *haystack, char needle, char replace)
{

	if (haystack == NULL) {
		return 0;
	}

	size_t num_replaced = 0;
	char *start = haystack;
	while((start = strchr(start, needle)) != NULL) {
		start[0] = replace;
		num_replaced++;
	}

	return num_replaced;
}

char *
get_log_uniqueness(char *buffer)
{
	int offset = sprintf(buffer, "%d", getpid());
	struct timeval tv;
	struct tm timeinfo;

	gettimeofday(&tv, NULL);
	localtime_r(&tv.tv_sec, &timeinfo);
	strftime(buffer + offset, 15, "%Y%m%d%H%M%S", &timeinfo);
	sprintf(buffer + offset + 14, "%ld", tv.tv_usec);

	return buffer;
}

void
millis_setup(struct timeval *start_tv)
{

	gettimeofday(start_tv, NULL);
}

double
millis_interval(struct timeval *start_tv)
{
	struct timeval end_tv;

	gettimeofday(&end_tv, NULL);
	float interval = millis_since(start_tv, &end_tv);
	*start_tv = end_tv;

	return interval;
}

double
millis_since(const struct timeval *start_tv, const struct timeval *end_tv)
{
	struct timeval tv;

	if (end_tv == NULL) {
		//event_gettime(NULL, &tv);
		gettimeofday(&tv, NULL);
		end_tv = &tv;
	}

	return (end_tv->tv_sec * 1000ULL + (double)end_tv->tv_usec / 1000) -
	    (start_tv->tv_sec * 1000ULL + (double)start_tv->tv_usec / 1000);
}

uint64_t
micros_since(const struct timeval *start_tv)
{
	struct timeval end_tv;
	uint64_t start_us, end_us;

	an_gettimeofday(&end_tv, true);
	start_us = start_tv->tv_sec * MICROS_PER_SEC + start_tv->tv_usec;
	end_us = end_tv.tv_sec * MICROS_PER_SEC + end_tv.tv_usec;

	return end_us - start_us;
}


uint64_t
micros_now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t)ts.tv_sec * MICROS_PER_SEC + (uint64_t)ts.tv_nsec / 1000LL;
}

uint64_t
micros_since_epoch(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	return (uint64_t)ts.tv_sec * MICROS_PER_SEC + (uint64_t)ts.tv_nsec / 1000LL;
}

const char *
url_scheme_get(const char *url, size_t *scheme_len)
{
	const char *s;

	s = strstr(url, "://");
	if (s == NULL) {
		*scheme_len = 0;
		return NULL;
	}

	*scheme_len = s - url;
	return url;
}

bool
url_is_secure(const char *url)
{
	const char *scheme;
	size_t scheme_len;

	scheme = url_scheme_get(url, &scheme_len);
	if (scheme == NULL) {
		/**
		 * Relative paths will typically use the same protocol as the
		 * parent site, but this is not guaranteed, thus allowing insecure
		 * content to be requested on a secure page.
		 */
		return false;
	}

	if (scheme_len == 5 && strncmp(scheme, "https", 5) == 0) {
		return true;
	}

	return false;
}

/* Skip http and https URL schemes. */
char *
url_skip_scheme(const char *url)
{
	const char *cp;

	if (strncmp(url, "http", 4) == 0) {
		cp = url + 4;

		if (*cp == 's')
			cp++;
		if (strncmp(cp, "://", 3) == 0)
			url = cp + 3;
	}

	return (char *)url;
}

bool
url_has_invalid_uri_scheme(const char *url)
{
	size_t len;
	const char *s;

	s = strstr(url, "://");
	if (s == NULL) {
		/** No scheme present */
		return false;
	}

	len = s - url;
	if (len == 4) {
		return strncmp(url, "http", 4) != 0;
	} else if (len == 5) {
		return strncmp(url, "https", 5) != 0;
	}

	/** Scheme length is something other than 4 or 5, must be invalid. */
	return true;

}

bool
url_is_single_encoded(const char *url)
{
	char *http_encoded = "http%3A%2F%2F";
	char *https_encoded = "https%3A%2F%2F";

	if (url == NULL) {
		return false;
	}

	return strncasecmp(url, http_encoded, strlen(http_encoded)) == 0 || strncasecmp(url, https_encoded, strlen(https_encoded)) == 0;
}

int
parse_url(url_t *url, const char *value)
{
	char *cp = url_skip_scheme(value);
	if (cp == value) {
		//initialize to NULL since failure is followed by
		//free_url
		memset(url, 0, sizeof(url_t));
		return 1;
	}

	value = cp;
	char *colon = strchr(value, ':');
	int len = strcspn(value, "/?");
	if (colon) {
		str2int(colon + 1, &url->port, 0);
		url->host = an_string_strndup(value, colon - value);
	} else {
		url->host = an_string_strndup(value, len);
		url->port = 80;
	}

	value += len;
	if (strlen(value) > 0) {
		url->uri = an_string_dup(value);
	} else {
		url->uri = an_string_dup("/");
	}

	return 0;
}

void
free_url(url_t *url)
{

	an_string_free(url->host);
	an_string_free(url->uri);
}

int
count_characters(const char *string, char character)
{
	int chars = 0;
	const char *p;

	if (string == NULL) {
		return 0;
	}

	p = string;
	while ((p = strchr(p, character)) != '\0') {
		chars++;
		p++;
	}

	return chars;
}

void
safe_strncpy(char *dest, const char *src, size_t num)
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

int
safe_strcmp(const char *s1, const char *s2)
{

	if (s1 == NULL && s2 == NULL) {
		return 0;
	}

	if (s1 == NULL && s2 != NULL) {
		return -1;
	}

	if (s1 != NULL && s2 == NULL) {
		return 1;
	}

	return strcmp(s1, s2);
}

bool
is_numeric_string(const char *string)
{
	assert(string != NULL);

	// stops on non [0-9] and on '\0'
	while (isdigit(*string++));

	return (*(string-1) == '\0' ? true : false);
}

ck_bitmap_t *
an_ck_bitmap_create(unsigned int n_entries, bool ones)
{
	ck_bitmap_t *bitmap = an_malloc_region(ck_bitmap_token, ck_bitmap_size(n_entries));

	if (bitmap == NULL) {
		return NULL;
	}

	ck_bitmap_init(bitmap, n_entries, ones);
	return bitmap;
}

void
an_ck_bitmap_destroy(ck_bitmap_t *bitmap)
{

	an_free(ck_bitmap_token, bitmap);
	return;
}

static void
an_ck_bitmap_defer_fn(void *b)
{

	an_ck_bitmap_destroy(b);
	return;
}

void
an_ck_bitmap_defer(ck_bitmap_t *bitmap)
{

	if (bitmap != NULL)
		an_thread_defer(bitmap, an_ck_bitmap_defer_fn);

	return;
}

void
make_id_code_key(id_code_key_t *key, unsigned id, const char *code)
{

    memset(key, 0, sizeof(id_code_key_t));
    key->id = id;
    safe_strncpy(key->code, code, CODE_LENGTH);
    key->code[CODE_LENGTH] = '\0';
    tolower_str(key->code);
}

const char *
an_strerror(int error)
{
	static __thread char an_strerror_buf[AN_STRERROR_BUFFER_LEN];

#ifdef _GNU_SOURCE
	return strerror_r(error, an_strerror_buf, sizeof(an_strerror_buf));
#else
	int r = strerror_r(error, an_strerror_buf, sizeof(an_strerror_buf));
	if (r != 0) {
		/*
		 * Should only happen if our buffer is too small, or if
		 * we pass an invalid error number.
		 */
		return "an_strerror: strerror_r() failed";
	}
	return an_strerror_buf;
#endif
}

const char *
bool_to_str(bool b)
{

	return b ? "true" : "false";
}

int
str2bool(const char *s, int default_value)
{

	if (s == NULL) {
		return default_value;
	}

	if (*s == 't' || *s == 'T' || *s == '1' || *s == 'y' || *s == 'Y') {
		return true;
	}

	if (*s == 'f' || *s == 'F' || *s == '0' || *s == 'n' || *s == 'N') {
		return false;
	}

	return default_value;
}

const char *
bool_to_yes_no(bool b)
{

	return b ? "yes" : "no";
}

int
gcd(int a, int b)
{
	int c;

	while (a != 0) {
		c = a;
		a = b % a;
		b = c;
	}

	return b;
}

#define METRIC_SUBST_CHAR '_'
#define METRIC_SUBDIR_CHAR '.'

void
metrics_sanitize_str(char *str, int len)
{

	if (str == NULL) {
		return;
	}

	for (int i = 0; str[i] != '\0' && i != len; ++i) {
		switch (str[i]) {
		case ' ':
		case '\t':
		case METRIC_SUBDIR_CHAR:
		case '\'':
		case '"':
			str[i] = METRIC_SUBST_CHAR;
			break;
		case '/':
			str[i] = METRIC_SUBDIR_CHAR;
			break;
		default:
			break;
		}
	}

	return;
}

bool
domain_valid(const char *domain)
{
	size_t domain_len = 0;
	const char *domain_end = NULL;
	const char *period = NULL;

	if (str_empty(domain) == true) {
		/* An empty domain is not valid */
		return false;
	}

	domain_len = strlen(domain);
	domain_end = domain + domain_len;

	period = strchr(domain, '.');
	if (period == NULL || period == domain || period[1] == '\0') {
		/* A period ('.') must be present, but may not be the
		 * first or last character of the domain.
		 */
		return false;
	}

	for (size_t i = 0; i < domain_len; i++) {
		/* Should only have alphanumerics, UTF-8 characters, or periods. */
		if (an_is_utf8(&domain[i], domain_end) <= 0) {
			return false;
		}
	}

	return true;
}

bool
domain_is_ip_address(const char *url)
{
	struct in_addr addr;
	int result = 0;

	/* url scheme should already be skipped */

	if (str_empty(url) == true) {
		return false;
	}

	/* terminate string at '/' or ':' to get domain */
	char *domain_end_position = strpbrk(url, "/:");
	char domain_end_char;
	if (domain_end_position != NULL) {
		domain_end_char = *domain_end_position;
		*domain_end_position = '\0';
	}

	result = inet_pton(AF_INET, url, &(addr));

	/* restore '/' or ':' on url string */
	if (domain_end_position!= NULL) {
		*domain_end_position = domain_end_char;
	}

	return result;
}

bool
url_is_invalid(const char *url, bool debug_or_test)
{
	const char *p;

	if (str_empty(url) == true) {
		return false;
	}

	if (url_has_invalid_uri_scheme(url) == true) {
		/* blacklist invalid url scheme */
		return true;
	}

	p = url_skip_scheme(url);

	/* skip whitespaces */
	while (isspace(*p)) {
		p++;
	}

	if (strncmp(p, "localhost", 9) == 0 || strncmp(p, "127.0.0.1", 9) == 0) {
		if (debug_or_test == true) {
			return false;
		} else {
			return true;
		}
	}

	/* blacklist if url domain is an IP address */
	return domain_is_ip_address(p);
}

char *
domain_valid_decode(const char *encoded_url)
{
	char *url;

	if (encoded_url == NULL) {
		return NULL;
	}

	url = an_decode_uri(encoded_url);
	if (domain_valid(url) == true) {
		return url;
	}

	debug(5, "Invalid \"referrer\" in querystring: '%s', decoded: '%s'\n", encoded_url, url);

	an_string_free(url);
	return NULL;
}

char *
domain_valid_from_kv(const struct evkeyvalq *kv, const char *param)
{
	char *url = NULL;

	if (kv != NULL && param != NULL) {
		url = domain_valid_decode(evhttp_find_header(kv, param));
	}

	return url;
}

bool
evbuffer_append_ck_hs_int(struct evbuffer *buf, ck_hs_t *set, bool pre_comma, unsigned limit)
{
	ck_hs_iterator_t iterator = CK_HS_ITERATOR_INITIALIZER;
	void *entry;
	bool first = (pre_comma == false);
	unsigned count = 0;

	while (ck_hs_next(set, &iterator, &entry) == true) {
		check_and_print_comma(first, buf);
		evbuffer_add_printf(buf, "%lu", (uintptr_t)entry);
		++count;
		if (limit > 0 && count >= limit) {
			break;
		}
	}

	return first == false;
}

bool
evbuffer_append_ck_hs_string(struct evbuffer *buf, ck_hs_t *set, bool pre_comma, unsigned limit)
{
	ck_hs_iterator_t iterator = CK_HS_ITERATOR_INITIALIZER;
	void *entry;
	bool first = (pre_comma == false);
	unsigned count = 0;

	while (ck_hs_next(set, &iterator, &entry) == true) {
		check_and_print_comma(first, buf);
		evbuffer_add_printf(buf, "\"%s\"", (const char *)entry);
		++count;
		if (limit > 0 && count >= limit) {
			break;
		}
	}

	return first == false;
}

bool
content_type_is_anm(const char *content_type)
{

	if (content_type == NULL) {
		return false;
	}

	if (strcmp(content_type, "application/x-an-message") == 0 ||
	    strcmp(content_type, "application/x-snappy-an-message") == 0) {
		return true;
	}

	return false;
}

void
an_sha1_encode(char dest[static AN_SHA1_ENCODED_LEN], const char *src, size_t len)
{
	unsigned char hash[SHA_DIGEST_LENGTH];
	int enc_len;

	SHA1((const unsigned char *)src, len, hash);
	enc_len = modp_b64w_encode(dest, (char *)hash, SHA_DIGEST_LENGTH);
	if (enc_len == -1) {
		char buffer[SHA_DIGEST_LENGTH * 2];

		for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
			sprintf(buffer + (i * 2), "%02x", hash[i]);
		}

		an_syslog(LOG_INFO, "Failed to encode SHA1 hash: %s\n", buffer);
	}

	return;
}

static int
str2index_internal(const char *str, const char *const strings[], size_t strings_len, int default_value, bool case_sensitive)
{
	int (*comparator)(const char *, const char *) = (case_sensitive) ? strcmp : strcasecmp;

	if (str == NULL) {
		return default_value;
	}

	for (size_t i = 0; i < strings_len; i++) {
		if (strings[i] == NULL) {
			continue;
		}

		if ((*comparator)(strings[i], str) == 0) {
			return (int)i;
		}
	}

	return default_value;
}

int
str2index(const char *str, const char *const strings[], size_t strings_len, int default_value)
{

	return str2index_internal(str, strings, strings_len, default_value, true);
}

int
stri2index(const char *str, const char *const strings[], size_t strings_len, int default_value)
{

	return str2index_internal(str, strings, strings_len, default_value, false);
}

int
btree_strcasecmp(const void *a, const void *b)
{
	const char *l = *(const char **)a;
	const char *r = *(const char **)b;
	return strcasecmp(l, r);
}

const char *
uri_skip_scheme(const char *uri)
{
	const char *ret = NULL;

	/* No URI */
	if (uri == NULL || uri[0] == '\0') {
		return NULL;
	}

	ret = strstr(uri, "://");

	/* No scheme present */
	if (ret == NULL) {
		return NULL;
	}

	/* Skip length of "://" */
	ret += 3;

	return ret;
}

char *
an_decode_uri_len(const char *uri, size_t len, size_t *out_len)
{
	char *decode;
	size_t temp_out_len = 0;

	decode = an_string_malloc(modp_burl_decode_len(len));
	temp_out_len = modp_burl_decode(decode, uri, len);

	if (out_len != NULL) {
		*out_len = temp_out_len;
	}

	return decode;
}

char *
an_decode_uri(const char *uri)
{
	size_t uri_len;

	if (uri == NULL) {
		return NULL;
	}

	uri_len = strlen(uri);
	if (uri_len == 0) {
		return NULL;
	}

	return an_decode_uri_len(uri, uri_len, NULL);
}

char *
an_decode_uri_utf8(const char *uri)
{
	char *tmp = an_decode_uri(uri);
	if (tmp == NULL) {
		return NULL;
	}

	char *r = an_idna_to_utf8(tmp);
	an_string_free(tmp);
	return r;
}

char *
an_encode_uri_len(const char *uri, size_t len, size_t *out_len)
{
	char *encode;
	size_t temp_out_len = 0;

	encode = an_string_malloc(modp_burl_encode_len(len));
	temp_out_len = modp_burl_encode(encode, uri, len);

	if (out_len != NULL) {
		*out_len = temp_out_len;
	}

	return encode;
}

char *
an_encode_uri(const char *uri)
{
	size_t uri_len;

	if (uri == NULL) {
		return NULL;
	}

	uri_len = strlen(uri);
	if (uri_len == 0) {
		return NULL;
	}

	return an_encode_uri_len(uri, uri_len, NULL);
}

int
an_mkdirhier(const char *path, mode_t mode)
{
	int r;
	const char *slash;

	if (path == NULL || strlen(path) == 0) {
		return EINVAL;
	}

	/*
	 * Create each parent directory found in the path.
	 * This algorithm is as slow and safe as possible.
	 */
	for (const char *p = path; (slash = strchr(p, '/')) != NULL; p = slash + 1) {
		char *const parent = an_string_strndup(path, (slash + 1) - path);

		debug(2, "%s: mkdir... %s", __func__, parent);

		r = mkdir(parent, mode);
		if (r < 0 && errno != EEXIST) {
			r = errno;
			an_syslog(LOG_ERR, "%s: failed to create parent %s with r = %d (%s)",
			    __func__, parent, r, an_strerror(r));
			an_string_free(parent);
			return r;
		}

		/* chown the directory if it's newly created */
		if (r == 0 && getuid() == 0 && server_config_chown_unprivileged(parent) != 0) {
			r = errno;
			an_syslog(LOG_ERR, "chown %s failed: %s", parent, an_strerror(r));
			an_string_free(parent);
			return r;
		}

		an_string_free(parent);
	}

	/* Should now be able to create the original path. */
	r = mkdir(path, mode);
	if (r < 0 && errno != EEXIST) {
		return errno;
	}

	/* chown the directory if it's newly created */
	if (r == 0 && getuid() == 0 && server_config_chown_unprivileged(path) != 0) {
		r = errno;
		an_syslog(LOG_ERR, "chown %s failed: %s", path, an_strerror(r));
		return r;
	}

	return 0;
}

/*
 * The number of leading consecutive bits set is the number of bytes a UTF-8 char occupies
 * If most significant bit is 0, then it's 1 byte, just like ASCII
 */
bool
str_utruncate(char *const s, size_t n_of_char)
{
	if (s == NULL) {
		return true;
	}

	if (an_utf8_validate(s, strlen(s)) == false) {
		return false;
	}

	unsigned char *p = (unsigned char *)s;
	size_t n = 0;
	while (*p != '\0' && n < n_of_char) {
		n++;

		if (is_non_ascii(*p) == false) {
			p++;
			continue;
		}

		for (unsigned int i = 6; i > 1; i--) {
			unsigned char bitmask = ((0x1 << i) - 1) << (8 - i);
			if ((*p & bitmask) == bitmask) {
				p += i;
				break;
			}
		}
	}

	*p = '\0';
	return true;
}
int
an_make_socket_nonblocking(int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL, NULL)) < 0) {
		an_syslog(LOG_WARNING, "fcntl(%d, F_GETFL)", fd);
		return -1;
	}

	if ((flags & O_NONBLOCK) == 0) {
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
			an_syslog(LOG_WARNING, "fcntl(%d, F_SETFL)", fd);
			return -1;
		}
	}

	return 0;
}

static inline bool
domain_valid_char(char c)
{

	/*
	 * Allow UTF-8 character
	 */
	if (is_non_ascii(c)) {
		return true;
	}

	switch (c) {
	case '-':
	case '.':
	case '+':
		return true;
	default:
		return isalnum(c) != 0;
	}

	return false;
}

bool
domain_extract(char domain[DOMAIN_BUFLEN], size_t *domain_len, const char *url, size_t in_len)
{

	if (url == NULL) {
		return false;
	}

	if (url[0] == '\0') {
		return false;
	}

	char *url2 = an_string_strndup(url, in_len);
	for (int i = 0; i < 10; i++) {

		size_t len = strlen(url2);
		size_t dec = modp_burl_decode(url2, url2, len);

		if (dec == len) {
			break;
		}
	}

	/* I am searching for "//" because this is now a valid url prefix */
	const char *p = url2;
	while (isspace(*p)) {
		p++;
	}

	const char *scheme = strstr(p, "//");
	if (scheme != NULL) {
		p = scheme + 2;
	}

	const char *host_start = p;
	while (*p != '\0') {

		if (domain_valid_char(*p) == false) {
			break;
		}

		p++;
	}

	*domain_len = p - host_start;
	if (*domain_len > DOMAIN_MAX_LEN) {
		*domain_len = 0;
		an_string_free(url2);
		return false;
	}

	memmove(domain, host_start, *domain_len);
	domain[*domain_len] = '\0';

	modp_tolower(domain, *domain_len);
	an_string_free(url2);

	return true;
}

/*
 * Read the entire contents of the file descriptor into the given buffer.
 * On success, the number of bytes that were successfully read is returned,
 * and a NUL character is appended to the buffer. On error, -1 is returned
 * and errno is set appropriately. We reuse the EFBIG errno constant for
 * buffer overflows.
 */
ssize_t
an_readall(int fd, char *buf, size_t size)
{
	ssize_t n;
	size_t off, left;

	off = 0;
	left = size;
	for (;;) {
		n = read(fd, buf + off, left);
		if (n == -1 && errno == EINTR) {
			continue;
		}
		if (n == -1) {
			return -1;
		}
		if (n == 0) {
			break;
		}
		assert((size_t)n <= left);
		off += n;
		left -= n;
		if (left == 0) {
			errno = EFBIG;	/* Not used by read() */
			return -1;
		}
	}

	assert(left >= 1);
	buf[off] = '\0';
	return (ssize_t)off;
}

void
an_vm_drop_caches(void)
{
	static const char *const path = "/proc/sys/vm/drop_caches";
	static const char *const content = "3\n";
	FILE *f;

	/*
	 * Sync dirty data to the filesystem, since we want all clean
	 * pages in the cache before dropping them.
	 */
	sync();

	f = fopen(path, "w");
	if (f == NULL) {
		an_syslog(LOG_ERR, "%s: failed to open %s", __func__, path);
		return;
	}

	if (fwrite(content, sizeof(char), strlen(content), f) == strlen(content)) {
		an_syslog(LOG_INFO, "%s: successfully dropped caches", __func__);
	} else {
		an_syslog(LOG_ERR, "%s: did not successfully write to %s", __func__, path);
	}

	fclose(f);
	return;
}

/**
 * Refer to https://stackoverflow.com/questions/3911536/utf-8-unicode-whats-with-0xc0-and-0x80
 */
size_t
strlen_utf8(const char *s)
{
	size_t len = 0;

	if (s == NULL) {
		return 0;;
	}

	for (uint32_t idx = 0; s[idx] != '\0'; idx++) {
		if ((s[idx] & 0xc0) != 0x80) {
			len++;
		}
	}

	return len;
}
