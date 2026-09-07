/**
 * @file CliParse.h
 * @brief Shared strict decimal parsing for the Arduino and ESP-IDF examples.
 */

#pragma once

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

namespace cli {

// Reject signs, whitespace, overflow and trailing text; leave output unchanged
// on failure. strtoul alone accepts negative values as unsigned integers.
inline bool parseU32(const char* text, uint32_t* out) {
  if (text == nullptr || out == nullptr || text[0] < '0' || text[0] > '9') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long value = strtoul(text, &end, 10);
  if (errno != 0 || *end != '\0') {
    return false;
  }
#if ULONG_MAX > UINT32_MAX
  // strtoul can succeed above the output range on hosts with 64-bit long.
  if (value > UINT32_MAX) {
    return false;
  }
#endif
  *out = static_cast<uint32_t>(value);
  return true;
}

inline bool parseU8(const char* text, uint8_t* out, uint8_t maxValue) {
  uint32_t value = 0;
  if (out == nullptr || !parseU32(text, &value) || value > maxValue) {
    return false;
  }
  *out = static_cast<uint8_t>(value);
  return true;
}

inline bool parseU16(const char* text, uint16_t* out, uint16_t minValue, uint16_t maxValue) {
  uint32_t value = 0;
  if (out == nullptr || !parseU32(text, &value) || value < minValue || value > maxValue) {
    return false;
  }
  *out = static_cast<uint16_t>(value);
  return true;
}

}  // namespace cli
