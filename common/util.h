#ifndef _UTIL_H_
#define _UTIL_H_

#include <assert.h>
#include <ck_bitmap.h>
#include <ctype.h>
#include <errno.h>
#include <event.h>
#include <json/json.h>
#include <limits.h>
#include <math.h>
#include <modp_b64w.h>
#include <modp_numtoa.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <acf/an_cc.h>
#include <acf/an_array.h>

#include "common/an_cc.h"
#include "common/an_poison.h"
#include "common/an_rand.h"
#include "common/assert_dev.h"
#include "common/bitmap.h"
#include "common/common_types.h"
#include "common/debug.h"

#define MIN_SECS (60U)
#define HOUR_SECS (3600U)
#define DAY_SECS (24U * HOUR_SECS)
#define DAY_MINS (24U * 60U)
#define YEAR_SECS (52U * 7U * DAY_SECS)

#define CODE_LENGTH 100
#define TIE_MARGIN 0.000005
#define AN_STRERROR_BUFFER_LEN 256
#define AN_SHA1_ENCODED_LEN modp_b64w_encode_len(SHA_DIGEST_LENGTH)

#define DOMAIN_MAX_LEN 253
#define DOMAIN_BUFLEN  256

#define AN_BLOCK_EXECUTE_ONCE_GUARD						\
	static bool _local_an_block_execute_once_guard;				\
	assert(_local_an_block_execute_once_guard == false);			\
	_local_an_block_execute_once_guard = true;

/**
 * @brief Callback to allow chaining ops on the buffer content.
 * @param src_buf Pointer to the source buffer
 * @param src_buf_size Size of the source buffer in bytes
 * @ctx Generic context
 * @return True on success, false on failure
 */
typedef bool (buffer_cb_t)(const char *src_buf, size_t src_buf_size, void *ctx);

typedef struct url {
	char *host;
	char *uri;
	int port;
} url_t;

int int64_val_comparator(int64_t one, int64_t two);

typedef struct id_code_key {
	unsigned id;
	char code[CODE_LENGTH + 1];
} id_code_key_t;

/**
 * Create a new ck_bitmap
 *
 * @param n_entries Number of entries to create in bitmap
 * @param ones initialize the bits to one
 * @return newly allocated ck_bitmap_t
 */
ck_bitmap_t *an_ck_bitmap_create(unsigned int n_entries, bool ones);

/**
 * @param bitmap destroy ck_bitmap
 */
void an_ck_bitmap_destroy(ck_bitmap_t *bitmap);

/**
 * @param bitmap defer the destruction of bitmap
 */
void an_ck_bitmap_defer(ck_bitmap_t *bitmap);

/**
 * @brief Return the scheme used for @a url, defined as string of characters
 * preceding `://`.
 *
 * @param url Input url
 * @param[out] scheme_len Length of scheme, if present
 * @return Pointer to scheme, or NULL if not present.
 */
const char *url_scheme_get(const char *url, size_t *scheme_len);

/**
 * @brief Determine if the url uses the https scheme.
 *
 * @param url Input url
 * @return True if the url uses the https scheme; false, otherwise.
 */
bool url_is_secure(const char *url);

/**
 * Skip returns pointer passed url scheme. This would be the character right
 * after 'http://' or 'https://'
 *
 * @param url the url you want to parse
 * @return the first character after the scheme
 */
char *url_skip_scheme(const char *url);

/**
 * Determine if the given url has an invalid uri scheme.
 *
 * We reject any scheme that is not 'http://' or 'https://'
 *
 * @param url the url to check
 * @return true if the url hs an invalid uri scheme as described above.
 */
bool url_has_invalid_uri_scheme(const char *url);

/**
 * Determine if the given url is single-encoded
 *
 * @param url the url to check
 * @return true if the url starts with 'http%3A%2F%2F' or 'https%3A%2F%2F'
 */
bool url_is_single_encoded(const char *url);

int parse_url(url_t *url, const char *value);
void free_url(url_t *url);


/**
 * Check whether a string is a prefix of another.
 *
 * @param str The string to test
 * @param prefix The prefix
 * @param len The number of characters from "prefix" to consider
 *            in the comparison
 * @return NULL if "prefix" is not a prefix of "str", or a pointer to
 *         the string immediately after the prefix otherwise.
 */
const char *strnprefix(const char *str, const char *prefix, size_t len);

/**
 * Equivalent to calling strnprefix() with strlen(prefix) for the length.
 */
static inline const char *
strprefix(const char *str, const char *prefix)
{

	return strnprefix(str, prefix, strlen(prefix));
}

/**
 * Search for the index within an array of strings
 *
 * @param str the string you are searching for
 * @param strings the array of search strings
 * @param strings_len the length of the strings array
 * @param default_value default value if string is not found
 * @return index of offset within the strings array
 */
int str2index(const char *str, const char *const strings[], size_t strings_len,
    int default_value);

/**
 * Case insensitive search for the index within an array of strings
 *
 * @param str the string you are searching for
 * @param strings the array of search strings
 * @param strings_len the length of the strings array
 * @param default_value default value if string is not found
 * @return index of offset within the strings array
 */
int stri2index(const char *str, const char *const strings[], size_t strings_len,
    int default_value);

/**
 * Convert a string to float
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2float(const char *str, float *result, float default_value);

/**
 * Convert a string to double
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2double(const char *str, double *result, double default_value);

/**
 * convert a string to int
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2int(const char *str, int *result, int default_value);

/**
 * convert a string to uint8_t
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2uint8(const char *str, uint8_t *result, uint8_t default_value);

/**
 * convert a string to uint16_t
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2uint16(const char *str, uint16_t *result, uint16_t default_value);

/**
 * convert a string to uint32_t
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2uint32(const char *str, uint32_t *result, uint32_t default_value);

/**
 * convert a string to uint64_t
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2uint64(const char *str, uint64_t *result, uint64_t default_value);

/**
 * convert a string to int32_t
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2int32(const char *str, int32_t *result, int32_t default_value);

/**
 * convert a string to int64_t
 *
 * @param str incoming string
 * @param[out] result the result of the conversion
 * @param default_value the value to use if the string is invalid or fails to convert
 * @return 1 on failure 0 on success, on failure default value is used
 */
int str2int64(const char *str, int64_t *result, int64_t default_value);

/**
 * check if a string is empty or null
 *
 * @param str incoming string
 * @return true if str is empty or null, false otherwise
 */
bool str_empty(const char *str);

/**
 * calculate output length of binary2hex function based on input
 *
 * @param binary_len length of string to base calculation on
 *
 * @return number of characters to allocate
 */
size_t binary2hex_output_len(size_t binary_len);

/**
 * calculate output length of hex2binary function based on input
 *
 * @param hex_len length of string to base calculation on
 *
 * @return number of characters to allocate
 */
size_t hex2binary_output_len(size_t hex_len);

/**
 * convert a hex string to binary
 *
 * @param[out] binary destination buffer of binary
 * @param hex string to convert
 * @param hex_len the length of the hex string
 *
 * @return number of characters written
 */
size_t hex2binary(unsigned char *binary, char *hex, size_t hex_len);

/**
 * convert a binary byte string to a hex string
 *
 * @param[out] hex destination buffer of of hex
 * @param binary string to convert
 * @param binary_len the length of the binary string
 *
 * @return size of destination string
 */
size_t binary2hex(char *hex, const unsigned char *binary, size_t binary_len);

/**
 * convert a bool to a true/false string
 *
 * @param b boolean value
 * @return 'true' or 'false' depending on boolean value
 */
const char *bool_to_str(bool b);

/**
 * convert a bool to a yes/no string
 *
 * @param b boolean value
 * @return 'yes' or 'no' depending on boolean value
 */
const char *bool_to_yes_no(bool b);

/**
 * convert a bool from a string
 *
 * @param s string to convert to boolean value
 * @param default_value default value to return if parsing fails
 * @return 1/true on "T,t,1" or 0/false on "F,f,0". If none of these values were present or the string was null, returns default_value.
 */
int str2bool(const char *s, int default_value);

/**
 *
 * determine whether a string represents a string value of false
 *
 * @param s string to check for falseness
 * @return true on "F,f,0", otherwise, false.
 */
static inline bool
str_is_false(const char *s)
{

	return (str2bool(s, true) == false);
}

/**
  * @brief truncate and round float to @a precision decimal
  * @param value float value to truncate
  * @param precision number of decimal to truncate to.
  * @return value after truncation
  */
static inline float
round_float(float value, int precision)
{

	if (precision <= 0) {
		return value;
	}

	double dec = pow(10, precision);

	return (roundf(value * dec) / dec);
}

/**
 *
 * determine whether a string represents a string value of true
 *
 * @param s string to check for trueness
 * @return true on "T,t,1", otherwise, false.
 */
static inline bool
str_is_true(const char *s)
{

	return (str2bool(s, false) == true);
}

void get_timestamp(char log_time[], int len);
void time_to_str(time_t time, char log_time[], int len);
uint8_t get_current_hour_utc(void);

/**
 * Check if string has only white space characters
 *
 * @param str string to check
 * @param len length of str
 * @return true if str has only white space characters or is NULL, false otherwise.
 */
bool is_whitespace_str(const char *str, size_t len);

/**
 * Zero copy left string trim.  This will return the offset to the first
 * non-space character in the middle of the string buffer or pointer to NULL
 * if string contains only spaces.
 *
 * Careful - this function does not check for pointer validity!
 *
 * @param str string to trim
 * @return offset in str that contains the first non space character
 */
const char *ltrim(const char *str);

/**
 * Zero copy string trim.  This will return the offset to the first character
 * in the middle of the string buffer
 *
 * @param str string to trim
 * @return offset in str that contains the first non space character
 */
char *trim(char *str);

/**
 * Trim a string in place.   This modifies the string itself such that the
 * first character in str[0] will now contain the first non-space value.
 *
 * @param str string to trim
 */
void trim_inplace(char *str);

/**
 * Convert a string to lower case
 *
 * @param str string to convert
 */
void tolower_str(char *str);

/**
 * Convert a string to uppercase
 *
 * @param str string to convert
 */
void toupper_str(char *str);

void safe_fill(char dest[], const char *source, size_t max_len);
size_t an_str_replace_char(char *haystack, char needle, char replace);
uint64_t next_power_of_2(uint64_t x);
int gcd(int a, int b);

// strncpy() that checks for nulls and always adds a null terminator
void safe_strncpy(char *dest, const char *src, size_t num);

/**
 * @brief Compare @a s1 and @a s2. Accepts NULL.
 * @param s1 String to be compared
 * @param s2 String to be compared
 * @return An integer less than, equal to, or greater than zero if s1 is found,
 * respectively, to be less than, to match, or be greater than s2.
 */
int safe_strcmp(const char *s1, const char *s2);

/**
 * @brief Copy escaped version of src string into dest string. Dest must have enough space allocated for the expansion of escapable characters, @see an_string_escaped_len().
 *
 * @param dest destination string for escaped src string
 * @param src source string that is copied into dest string
 */
void an_string_escaped(char *dest, const char *src, size_t len);

/**
 * @brief Calculates required length of destination string for an_string_escaped()
 *
 * @param src source string which will be used as input for an_string_escaped() and
 * from which length of destination string will be calculated
 *
 * @return required length of destination string
 */
size_t an_string_escaped_len(const char *src);

/**
 * @brief init the id_code_key with the id and a deep copy
 * of the code, store the code as lower case to make lookups
 * case insensitive
 *
 * @param[out] key the id_code_key struct being filled
 * @param[in] id id
 * @param[in] code code
 */
void make_id_code_key(id_code_key_t *key, unsigned id, const char *code);

/**
 * Thread safe version of strerror
 *
 * @param error the errno to convert
 * @return constant string representation of the errno
 */
const char *an_strerror(int error);

/**
 * Count the number of characters that a appear in a string
 *
 * @param string the string to search in
 * @param character the character to count occurrences of
 * @return the number of times the character appears
 */
int count_characters(const char *string, char character);

/**
 * Print a unique value to the buffer
 *
 * @param buffer the dest buffer to store this unique string
 * @return pointer to buffer
 */
char *get_log_uniqueness(char *buffer);

/**
 * initialize timeval
 *
 * @param start_tv timeval to initialize
 */
void millis_setup(struct timeval *start_tv);

/**
 * time in ms between end and start.  If end is NULL the current
 * time will be used.
 *
 * @param start start time
 * @param end end time or NULL
 * @return end - start in milliseconds
 */
double millis_since(const struct timeval *start, const struct timeval *end);

/**
 * The current time in milliseconds since the initial timeval.  After calling
 * this function interval_tv will be updated to the current time.
 *
 * @param interval_tv start time
 * @return time in ms since start time denoted by interval_tv
 */
double millis_interval(struct timeval *interval_tv);

/**
 * @brief micro seconds since start_tv
 * @param start_tv start time
 * @return time in us since start time
 */
uint64_t micros_since(const struct timeval *start_tv);

/**
 * the current time in microseconds
 *
 * @return the current time in microseconds
 */
uint64_t micros_now(void);

/**
 * the current time in microseconds, measured against the UNIX epoch
 *
 * @return the current time in microseconds
 */
uint64_t micros_since_epoch(void);

/**
 * checks if a string only contains numeric characters
 *
 * @return true if string is composed only of characters [0-9]
 */
bool is_numeric_string(const char *string);

/**
 * @brief Replace metrics reserved characters with '_' in member's short name
 * Note: Please DON'T call this function on string which is in static readonly
 * memory, like global string
 *
 * @param str Metrics identifier string to be sanitized
 * @param len As a safety measure, user can pass a maximum length of the str
 * to check. User can pass -1 to trust that str is null terminated
 */
void metrics_sanitize_str(char *str, int len);

/**
 * @brief ceil(log_2(x)). Special case: log2_ceiling(0) = 0.
 */
static inline uint32_t
log2_ceiling(uint64_t x)
{

	if (x <= 1) {
		return 0;
	}

	return (CHAR_BIT * sizeof(long long)) - __builtin_clzll(x - 1);
}

/**
 * @brief floor(log_2(x)). log2_floor(0) is undefined.
 */
static inline uint32_t
log2_floor(uint64_t x)
{

	return (CHAR_BIT * sizeof(long long) - 1) - __builtin_clzll(x);
}

/**
 * Create a custom bsearch function named bsearch_{{name}}.  The code
 * assumes that the comparator is inlined and simple (a handful of
 * instructions).
 *
 * @param name the name of the btree
 * @param type the struct type
 * @param compare the comare function
 */
#define BSEARCH_DEFINE(name, type, compare)				\
									\
AN_CC_UNUSED static inline struct type *				\
bsearch_##name(struct type *array, size_t array_length, struct type *key) \
{									\
	size_t half, n = array_length;					\
	const struct type *lo = &array[0];				\
									\
	{								\
		int r = -1;						\
									\
		/* Only search if non-empty and least elt < key */	\
		if (array_length == 0 || (r = compare(lo, key)) >= 0) {	\
			return (r == 0) ? (struct type *)lo : NULL;	\
		}							\
	}								\
									\
	/*								\
	 * Invariants:							\
	 * *lo <= key							\
	 * lo[n...) > key						\
	 */								\
	half = n / 2;							\
	while (half > 0) {						\
		const struct type *mid = lo + half;			\
		lo = (compare(mid, key) <= 0) ? mid : lo;		\
		n -= half;						\
		half = n / 2;						\
	}								\
									\
	if (compare(lo, key) != 0) {					\
		return NULL;						\
	}								\
									\
	return (struct type *)lo;					\
}									\
									\
AN_CC_UNUSED static inline const struct type *							\
bsearch_const_##name(const struct type *array, size_t array_length, struct type *key) 		\
{												\
	return (const struct type *)bsearch_##name((struct type *)array, array_length, key); 	\
}

/**
 * @brief Lower bound: finds lower bound index for the @a key using @a cmp comparator. Lower bound is
 *		  defined as an earliest index i in the ordered sequence (s) for which s[i] >= key (so for all
 *		  indexes j<i, s[j] < key). In the case key is greater then all the elements in the sequence s,
 *		  the lower bound index is set to sequense s size (index greater then all sequence indexes,
 *		  however, it points to a non-existing element.
 *
 * 		  Upper bound: finds upper bound index for the @a key using @a cmp comparator. Upper bound is
 *		  defined as an earliest index i in the ordered sequence (s) for which s[i] > key (so for all
 *		  indexes j<i, s[j] <= key). In the case key is greater or equal to all the elements in the
 *		  sequence s, the upper bound index is set to sequense s size (index greater then all sequence
 *		  indexes, however, it points to a non-existing element.
 *
 *		  Equal range: interval essentially equal to [lower_bound, upper_bound]; s[i] = @a key,
 *		  lower_bound <= i < upper_bound
 *
 * @param name Function name suffix
 * @param array Pointer to array being searched
 * @param array_length Size of an array being searched
 * @param key Key to search for
 * @return Pointer to lower bound element
 */

#define BSEARCH_BOUNDS_DEFINE(name, type, compare)														\
AN_CC_UNUSED static const struct type *																	\
bsearch_lower_bound_const_##name(const struct type *array, size_t array_length, const struct type *key)	\
{																										\
																										\
	if (array_length == 0) {																			\
			return array;																				\
	}																									\
																										\
	size_t half = array_length / 2;																		\
	while (half > 0)																					\
	{																									\
		const struct type *mid = array + half;															\
		array = (compare(mid, key) < 0) ? mid : array;													\
		array_length -= half;																			\
		half = array_length / 2;																		\
	}																									\
																										\
	array = (compare(array, key) < 0) ? array + 1 : array;												\
	return array;																						\
}																										\
																										\
AN_CC_UNUSED static inline struct type *																\
bsearch_lower_bound_##name(struct type *array, size_t array_length, const struct type *key)				\
{																										\
	return (struct type *)bsearch_lower_bound_const_##name((struct type *)array, array_length, key);	\
}																										\
																										\
AN_CC_UNUSED static const struct type *																	\
bsearch_upper_bound_const_##name(const struct type *array, size_t array_length, const struct type *key)	\
{																										\
																										\
	if (array_length == 0) {																			\
			return array;																				\
	}																									\
																										\
	size_t half = array_length / 2;																		\
	while (half > 0)																					\
	{																									\
		const struct type *mid = array + half;															\
		array = (compare(mid, key) <= 0) ? mid : array;													\
		array_length -= half;																			\
		half = array_length / 2;																		\
	}																									\
																										\
	array = (compare(array, key) <= 0) ? array + 1 : array;												\
	return array;																						\
}																										\
																										\
AN_CC_UNUSED static inline struct type *																\
bsearch_upper_bound_##name(struct type *array, size_t array_length, const struct type *key)				\
{																										\
	return (struct type *)bsearch_upper_bound_const_##name((struct type *)array, array_length, key);	\
}																										\
																										\
struct bsearch_equal_range_rc_const_##name																\
{																										\
	const struct type *lower_bound, *upper_bound;														\
};																										\
																										\
struct bsearch_equal_range_rc_##name																	\
{																										\
	struct type *lower_bound, *upper_bound;																\
};																										\
																										\
AN_CC_UNUSED static struct bsearch_equal_range_rc_const_##name											\
bsearch_equal_range_const_##name(const struct type *array, size_t array_length, const struct type *key)	\
{																										\
	if (array_length == 0) {																			\
		return (struct bsearch_equal_range_rc_const_##name){											\
			.lower_bound = array,																		\
			.upper_bound = array																		\
		};																								\
	}																									\
	size_t half = array_length / 2;																		\
	const struct type *lb = array;																		\
	const struct type *ub = array;																		\
	while (half > 0)																					\
	{																									\
		const struct type *lb_mid = lb + half;															\
		const struct type *ub_mid = ub + half;															\
		lb = (compare(lb_mid, key) < 0) ? lb_mid : lb;													\
		ub = (compare(ub_mid, key) <= 0) ? ub_mid : ub;													\
		array_length -= half;																			\
		half = array_length / 2;																		\
	}																									\
																										\
	return (struct bsearch_equal_range_rc_const_##name){												\
		.lower_bound = (compare(lb, key) < 0) ? lb + 1 : lb,											\
		.upper_bound = (compare(ub, key) <= 0) ? ub + 1 : ub											\
	};																									\
}																										\
																										\
AN_CC_UNUSED static struct bsearch_equal_range_rc_##name												\
bsearch_equal_range_##name(struct type *array, size_t array_length, const struct type *key)				\
{																										\
	struct bsearch_equal_range_rc_const_##name rc = 													\
		bsearch_equal_range_const_##name(array, array_length, key);										\
																										\
	return (struct bsearch_equal_range_rc_##name){														\
		.lower_bound = (struct type *)rc.lower_bound,													\
		.upper_bound = (struct type *)rc.upper_bound 													\
	};																									\
}

/**
 * calls function created with BSEARCH_DEFINE
 */
#define BSEARCH(name, array, array_length, key) \
	bsearch_##name(array, array_length, key)

#define BSEARCH_CONST(name, array, array_length, key) \
	bsearch_const_##name(array, array_length, key)

/**
 * calls function created with BSEARCH_BOUNDS_DEFINE
 */
#define BSEARCH_LOWER_BOUND(name, array, array_length, key) 		\
	bsearch_lower_bound_##name(array, array_length, key)

#define BSEARCH_LOWER_BOUND_CONST(name, array, array_length, key)	\
	bsearch_lower_bound_const_##name(array, array_length, key)

#define BSEARCH_UPPER_BOUND(name, array, array_length, key) 		\
	bsearch_upper_bound_##name(array, array_length, key)

#define BSEARCH_UPPER_BOUND_CONST(name, array, array_length, key) 	\
	bsearch_upper_bound_const_##name(array, array_length, key)

#define BSEARCH_EQUAL_RANGE_RC_CONST_TYPE(name)						\
	struct bsearch_equal_range_rc_const_##name

#define BSEARCH_EQUAL_RANGE_RC_TYPE(name)							\
	struct bsearch_equal_range_rc_##name

#define BSEARCH_EQUAL_RANGE_CONST(name, array, array_length, key)	\
	bsearch_equal_range_const_##name(array, array_length, key)

#define BSEARCH_EQUAL_RANGE(name, array, array_length, key)			\
	bsearch_equal_range_##name(array, array_length, key)

/**
 * Create a custom uniq procedure named uniq_{{name}} that uses
 * a combiner to merge equal elements.
 *
 * @param name the name of the btree
 * @param type the element type
 * @param compare the compare function
 * @param combine the combine procedure
 */
#define UNIQ_DEFINE(name, type, compare, combine)			\
static inline size_t							\
uniq_##name(type *array, size_t array_length)				\
{									\
	size_t first, next;						\
									\
	if (array_length == 0) {					\
		return 0;						\
	}								\
									\
	for (first = 0, next = 1; next < array_length; next++) {	\
		if (compare(&array[first], &array[next]) == 0) {	\
			combine(&array[first], &array[next]);		\
		} else if (++first < next) {				\
			array[first] = array[next];			\
		}							\
	}								\
									\
	return first + 1;						\
}

#define UNIQ_NOOP_COMBINE(first, next)

#define UNIQ(name, array, array_length) \
	uniq_##name((array), (array_length))

#ifdef assert
#undef assert
#endif

#define assert(X) do {							\
		AN_HOOK_ON(assert, assert) {				\
			if (AN_CC_UNLIKELY(!(X))) {			\
				int level = 1;				\
									\
				AN_HOOK_UNSAFE(assert_die, assert) {	\
					level = 3;			\
				}					\
									\
				syslog_assert(level, #X, __FILE__,	\
				    __LINE__, __PRETTY_FUNCTION__);	\
			}						\
		}							\
	} while (0)

/* clamp a value between a min and max */
#define CLAMP(x, low, high)                 \
    ({                                      \
        typeof(x) _x = (x);                 \
        typeof(low) _low = (low);           \
        typeof(high) _high = (high);        \
        ((_x > _high) ? _high : ((_x < _low) ? _low : _x)); \
     })

#undef strdup
#define strdup(str)						\
	({							\
		const char *_str = (str);			\
		_str == NULL ? NULL : strdup(_str);		\
	})

#define offset_of(type, member)					\
	((size_t)(&((type *)0)->member))
#define container_of(derived_ptr, type, field)			\
	((type *)((char *)(derived_ptr) - offset_of(type, field)))

#define LIST_LINKED(x, y) \
	((x)->y.le_prev != NULL)

#define CK_LIST_REMOVE_SAFE(x, y)				\
	({							\
		bool r = false;					\
		if ((x)->y.le_prev != NULL) {			\
			CK_LIST_REMOVE((x), y);			\
			(x)->y.le_prev = NULL;			\
			(x)->y.le_next = NULL;			\
			r = true;				\
		}						\
		r;						\
	})

#define LIST_REMOVE_SAFE(x, y)					\
	({							\
		bool r = false;					\
		if ((x)->y.le_prev != NULL) {			\
			LIST_REMOVE((x), y);			\
			(x)->y.le_prev = NULL;			\
			(x)->y.le_next = NULL;			\
			r = true;				\
		}						\
		r;						\
	})

#define AN_QSORT(ARRAY, COUNT, COMPARATOR)				\
	qsort((ARRAY), (COUNT), sizeof(*(ARRAY)),			\
	    AN_CC_CAST_COMPARATOR((COMPARATOR), __typeof__(*(ARRAY))))

static inline int
strtol_range(char **end_ptr, int base, long int min, long int max)
{
	const char *str = *end_ptr;
	errno = 0;
	long int result = strtol(str, end_ptr, base);
	if (errno == ERANGE || (result > max) || (result < min)) {
		errno = ERANGE;
		return 0;
	}
	return result;
}

static inline int
modular_cmp_32(uint32_t a, uint32_t b)
{
	int32_t delta;

	delta = (int32_t)(a - b);
	if (delta == 0) {
		return 0;
	}

	return (delta < 0) ? -1 : 1;
}

static inline bool
modular_lt_32(uint32_t a, uint32_t b)
{

	return (int32_t)(a - b) < 0;
}

static inline bool
modular_leq_32(uint32_t a, uint32_t b)
{

	return (int32_t)(a - b) <= 0;
}

static inline bool
modular_geq_32(uint32_t a, uint32_t b)
{

	return (int32_t)(a - b) >= 0;
}

static inline bool
modular_gt_32(uint32_t a, uint32_t b)
{

	return (int32_t)(a - b) > 0;
}

/*
 * Check if the url is valid. Right now we only check if there is a dot in the middle of it
 * As time goes by and we found more invalid urls, we'll make this function more complicated
 *
 * @param domain url to be validated
 * @return true if valid, false otherwise
 */
bool domain_valid(const char *domain);

/*
 * Check if the url starts with an IP address.
 *
 * @param url url to be checked
 * @return true if url starts with an IP address, false otherwise
 */
bool domain_is_ip_address(const char *url);

/*
 * Check if the url has invalid url scheme or hostname is an ip address.
 *
 * @param url url to be checked
 * @param debug_or_test boolean that indicates whether impbus request is debug/test
 * @return true if invalid, false otherwise
 */
bool url_is_invalid(const char *url, bool debug_or_test);

/*
 * Decode encoded_url and return it if it's valid, NULL if invalid
 *
 * @param encoded_url Encoded url to be decode and validate
 * @return Decoded url if valid, NULL if invalid
 */
char *domain_valid_decode(const char *encoded_url);

struct evkeyvalq;

/*
 * Find "referrer" in kv and decode it. Set it to be *v if it's valid url.
 * If it's invalid, leave *v unchanged
 *
 * @param kv a pointer a a key value pair queue to search for "referrer"
 * @param param the querystring parameter to search
 * @return decoded url from kv, NULL if invalid
 */
char *domain_valid_from_kv(const struct evkeyvalq *kv, const char *param);


/**
 * @brief Gets the current year. This can be used to get year of birth from age
 * @return current year in 4 digits format
 */
static inline uint16_t
current_year(void)
{
	time_t timeval;
	struct tm tp;

	time(&timeval);
	gmtime_r(&timeval, &tp);

	return tp.tm_year + 1900;
}


/**
 *  Returns time_t representation of a date string meant for log storage.
 *
 *  @param[in] timestamp string to convert to time_t
 *  @return time value convert from timestamp
 */
time_t an_logtime(const char *timestamp);

/**
 * append all integers in ck_hs data structure in json format
 *
 * @param buf the buffer to store the array
 * @param set the ck_hs_t to convert
 * @param pre_comma if true it will append a leading comma
 * @param limit the maximum number of elements to append
 */
bool evbuffer_append_ck_hs_int(struct evbuffer *buf, ck_hs_t *set, bool pre_comma, unsigned limit);

/**
 * append all strings in ck_hs data structure in json format
 *
 * @param buf the buffer to store the array
 * @param set the ck_hs_t to convert
 * @param pre_comma if true it will append a leading comma
 * @param limit the maximum number of elements to append
 */
bool evbuffer_append_ck_hs_string(struct evbuffer *buf, ck_hs_t *set, bool pre_comma, unsigned limit);

/**
 * @brief Determine if @a content_type is an_message.
 * @param content_type Content-Type read off of request header
 * @return true if content_type is an_message.
 */
bool content_type_is_anm(const char *content_type);

/**
 * @brief Compute SHA1 hash of @a src and encode using b64w.
 * @param[out] dest Output buffer with width at least AN_SHA1_ENCODED_LEN
 * @param[in] src Input buffer to be hashed and encoded
 * @param[in] len Length of input buffer to hash and encode
 */
void an_sha1_encode(char dest[static AN_SHA1_ENCODED_LEN], const char *src, size_t len);

#define an_asprintf(p, fmt,...) do {		\
	int nop = asprintf(p, fmt,__VA_ARGS__); \
	(void) nop;				\
} while (0)

/**
 * basic intset comparator
 *
 * @param a integer value a
 * @param b integer value b
 * @return a - b
 */
static inline int
int_comparator_asc(const void *a, const void *b)
{
	int *ia = (int *)a;
	int *ib = (int *)b;
	return *ia - *ib;
}

/**
 * basic intset comparator
 *
 * @param a integer value a
 * @param b integer value b
 * @return a - b
 */
static inline int
int_comparator_desc(const void *a, const void *b)
{
	int *ia = (int *)a;
	int *ib = (int *)b;
	return *ib - *ia;
}

/*
 * @brief Comparator function used for storing string in a btree
 * The arguments are actually pointer to char *
 */
int btree_strcasecmp(const void *, const void *);

/**
 * Deprecated BS we only keep around for compatibility with old code.
 *
 * Print an AN_ARRAY containing pointers as a JSON array of values,
 * where the value is the field element of the object
 */
#define AN_ARRAY_PTR_TO_EVBUFFER(buf, array, field)			\
	do {								\
		char buffer[32];					\
									\
		EVBUFFER_ADD_STRING((buf), "[");			\
		for (unsigned int _an_i = 0; _an_i < (array)->n_entries; _an_i++) { \
			if (_an_i != 0) {				\
				EVBUFFER_ADD_STRING((buf), ",");	\
			}						\
									\
			modp_itoa10((array)->values[_an_i]->field, buffer); \
			evbuffer_add_printf((buf), "%s", buffer);	\
		}							\
									\
		EVBUFFER_ADD_STRING((buf), "]");			\
	} while (0)

/**
 * @brief Skip the scheme portion of any URL
 *
 * @param uri URI to skip the scheme of, if present
 *
 * @return A pointer to the location just past the scheme in the URI
 */
const char *uri_skip_scheme(const char *uri);

/**
 * @brief Wrapper for the modp variant of URI decode. It is up to the caller to free
 * the returned string.
 *
 * @param uri String to decode
 * @param len Length of the string to decode
 * @param[out] out_len Length of the decoded string
 *
 * @returns an_string_malloc'd string
 */
char *an_decode_uri_len(const char *uri, size_t len, size_t *out_len);

/**
 * @brief Wrapper for the modp variant of URI encode. It is up to the caller to free
 * the returned string.
 *
 * @param uri String to encode
 * @param len Length of the string to encode
 * @param[out] out_len Length of the encoded string
 *
 * @returns an_string_malloc'd string
 */
char *an_encode_uri_len(const char *uri, size_t len, size_t *out_len);

/**
 * @brief Wrapper for the modp variant of URI decode. It is up to the caller to free
 * the returned string.
 *
 * @param uri String to decode
 *
 * @returns an_string_malloc'd string
 */
char *an_decode_uri(const char *uri);

char *an_decode_uri_utf8(const char *uri);

/**
 * @brief Wrapper for the modp variant of URI encode. It is up to the caller to free
 * the returned string.
 *
 * @param uri String to encode
 *
 * @returns an_string_malloc'd string
 */
char *an_encode_uri(const char *uri);

/**
 * @brief Ensure that a directory hierarchy exists.
 * @param path the path of the bottommost directory in the hierarchy
 * @param mode the mode bits to use if any directories are created
 * @returns 0 on success, otherwise an errno value
 */
int an_mkdirhier(const char *path, mode_t mode);

/*
 * @brief Truncate a UTF-8 string
 * Note: if @a s is not valid UTF-8 string, nothing is done. The expected behavior in this case is debatable though
 * @s string to truncate. Note this string must be modifiable, caller is responsible to make sure it doesn't reside on read-only section memory
 * @n_of_char Number of UTF-8 character, NOT number of bytes
 * @return true if s is NULL or valid utf-8 string, false otherwise
 */
bool str_utruncate(char *const s, size_t n_of_char);

static inline bool
is_non_ascii(char c)
{

	return (c & (0x1 << 7)) != 0;
}
/**
 * @brief Make a socket nonblocking.
 * @param file descriptor which has already had a socket mapped to it.
 * @return 0 on success, -1 otherwise.
 */
int an_make_socket_nonblocking(int fd);

bool domain_extract(char domain[DOMAIN_BUFLEN], size_t *domain_len, const char *url, size_t in_len);

ssize_t an_readall(int fd, char *buf, size_t size);

/* Drop operating system caches via echo 3 > /proc/sys/vm/drop_caches */
void an_vm_drop_caches(void);

/**
 * @biref Given a utf8 string, count its length before encoded. When we validate
 * length of creative content, we want to validate its original length instead of
 * the bytes of the utf8 encoded string.
 * @param s utf8 string
 * @return length of characters for utf8 string
 */
size_t strlen_utf8(const char *s);

/**
 * @brief Compare two unsigned integers without using conditionals.
 * @return > 0 if a > b, 0 if a == b, < 0 if a < b
 */
#define INTEGER_COMPARE(a, b) ({						\
	_Static_assert(__builtin_types_compatible_p(typeof(a), typeof(b)),	\
		"Comparing integers with mismatched types");			\
	int tmp;								\
	if (a == b) {								\
		tmp = 0;							\
	} else {								\
		tmp = (a < b) ? -1 : 1;						\
	}									\
	tmp;									\
})

#endif
