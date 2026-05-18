/**
 * @file Status.h
 * @brief Error handling types for StatusLed.
 *
 * Provides a lightweight, zero-allocation error model. All error messages
 * are static string literals.
 */

#pragma once

#include <stdint.h>

namespace StatusLed {

/**
 * @brief Error code enumeration.
 *
 * Covers common embedded error scenarios. When wrapping third-party libraries,
 * translate their errors to the most appropriate Err code and store the
 * original error in Status::detail.
 */
enum class Err : uint16_t {
  OK = 0,              ///< Success, no error
  INVALID_CONFIG,      ///< Invalid argument or configuration parameter
  TIMEOUT,             ///< Operation timed out
  RESOURCE_BUSY,       ///< Resource is busy, try again later
  NOT_INITIALIZED,     ///< Library not initialized or begin() not called
  OUT_OF_MEMORY,       ///< Memory allocation failed
  HARDWARE_FAULT,      ///< Hardware peripheral returned error
  EXTERNAL_LIB_ERROR,  ///< Error from external library (see detail field)
  UNSUPPORTED,         ///< Feature not supported by current backend
  INTERNAL_ERROR       ///< Internal logic error (bug in library code)
};

/**
 * @brief Operation result with error details.
 *
 * Returned by fallible operations. Check with ok() or inspect code/msg.
 *
 * @note The msg field MUST point to a static string literal. Never assign
 *       dynamically allocated strings. This ensures zero heap allocation
 *       in error paths and safe usage across function boundaries.
 */
struct Status {
  Err code = Err::OK;       ///< Error category
  int32_t detail = 0;       ///< Vendor/library-specific error code (optional)
  const char* msg = "";     ///< Human-readable message (STATIC STRING ONLY)

  /// @brief Default constructor creates OK status.
  constexpr Status() : code(Err::OK), detail(0), msg("") {}

  /// @brief Constructor with all fields.
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}

  /// @brief Create a success Status.
  /// @return Status with Err::OK.
  static constexpr Status Ok() { return Status(Err::OK, 0, ""); }

  /// @brief Create an error Status.
  /// @param c Error category.
  /// @param d Vendor/library-specific detail value.
  /// @param m Static message string.
  /// @return Status with supplied error fields.
  static constexpr Status Error(Err c, int32_t d = 0, const char* m = "") {
    return Status(c, d, m);
  }

  /// @brief Check if operation succeeded.
  /// @return true if code == Err::OK
  constexpr bool ok() const { return code == Err::OK; }

  /// @brief Check whether the operation should be retried later.
  /// @return true for transient RESOURCE_BUSY results.
  constexpr bool inProgress() const { return code == Err::RESOURCE_BUSY; }
};

/// @brief Create a success Status.
/// @return Status with Err::OK
constexpr Status Ok() { return Status::Ok(); }

/// @brief Create an error Status.
/// @param code Error category.
/// @param detail Vendor/library-specific detail value.
/// @param msg Static message string.
/// @return Status with supplied error fields.
constexpr Status Error(Err code, int32_t detail = 0, const char* msg = "") {
  return Status::Error(code, detail, msg);
}

}  // namespace StatusLed
