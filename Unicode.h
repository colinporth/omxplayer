#pragma once

#include <boost/config.hpp>
#include <stdexcept>

/*******************************
 * Test if c is a valid UTF-32 character.
 *
 * \uFFFE and \uFFFF are considered valid by this function,
 * as they are permitted for internal use by an application,
 * but they are not allowed for interchange by the Unicode standard.
 *
 * Returns: true if it is, false if not.
 */

inline bool isValidChar32(char32_t c) BOOST_NOEXCEPT
{
  /* Note: FFFE and FFFF are specifically permitted by the
   * Unicode standard for application internal use, but are not
   * allowed for interchange.
   * (thanks to Arcane Jill)
   */

  return c < 0xD800 ||
    (c > 0xDFFF && c <= 0x10FFFF /*&& c != 0xFFFE && c != 0xFFFF*/);
}

/***************
 * Decodes and returns character starting at s[idx]. idx is advanced past the
 * decoded character. If the character is not well formed, an exception is
 * thrown and idx remains unchanged.
 */

char32_t decodeUtf8(const char* s, size_t len, size_t& idx);
