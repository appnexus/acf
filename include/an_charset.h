/*
  Copyright (C) 2011 Joseph A. Adams (joeyadams3.14159@gmail.com)
  All rights reserved.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

/*
 * NOTE: This file has been modified from its original form to remove
 * functions that we (AppNexus) do not use.
 */

#ifndef AN_CHARSET_H
#define AN_CHARSET_H

#include "an_cc.h"

AN_EXTERN_C_BEGIN

#include <stdbool.h>
#include <stddef.h>

#include "acf_export.h"

/**
 * @brief Validate that the given char is valid UTF-8
 *
 * @param s Start of the UTF-8 character
 * @param e End of the string containing the UTF-8 character
 *
 * @return The byte-length of the UTF-8 character, 0 if invalid
 */
ACF_EXPORT int an_is_utf8(const char *s, const char *e);

/** Utf8 string statistics */
struct an_utf8_stats
{
	bool is_valid;					/**< True if the string has valid utf-8 content */
	size_t total_code_point_count;	/**< Total number of code points in the string.
										 If @a is_valid is false, number of code points before error was detected */
	size_t wide_code_point_count;	/**< Number of wide (>= bytes in size) code points encountered */
	size_t parsed_length;			/**< Number of characters, successfully parsed from the input string */
};

/**
 * @brief Calculates the stats on the utf-8 encoded string pointed to by
 *		  parameter @a str. See @a utf8_stats
 * @param str Pointer to the utf-8 string to calculate stats
 * @param byte_length Byte length of the utf-8 string
 * @return @a utf8_stats structure with stats collected
*/
ACF_EXPORT struct an_utf8_stats an_utf8_stats_get(const char *str, size_t byte_length);

/**
 * @brief Validate the given UTF-8 string. If it contains '\0' characters, it is still valid.
 *
 * @param str String to validate
 * @param byte_length Byte-length of the string
 *
 * @return True if valid UTF-8 string, false otherwise
 */
ACF_EXPORT bool an_utf8_validate(const char *str, size_t byte_length);

AN_EXTERN_C_END

#endif
