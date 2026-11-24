
/**
 * @brief C++ wrapper around the C99 FalconRuntimeAPI host callbacks.
 *
 * Provides idiomatic C++ access to host runtime semantics (equality, ordering,
 * deep-copy) without exposing raw context pointers or null-checks at call
 * sites.
 *
 * Also defines non-member operator overloads (==, !=, <, >) in the
 * global namespace for FalconParamEntry. Because they are free functions,
 * FalconParamEntry retains its pure-C99 ABI layout while supporting natural
 * C++ comparisons and standard algorithms (e.g., std::find, std::sort).
 *
 * Usage in wrapper libraries:
 *   1. Define FALCON_IMPLEMENT_RUNTIME_HOOK in exactly ONE translation unit
 *      before including this header to export the C initialization hook:
 *        #define FALCON_IMPLEMENT_RUNTIME_HOOK
 *        #include "FalconRuntime.hpp"
 *   2. Include this header normally in all other translation units.
 *   3. Call falcon::Runtime::equals(a, b) or use `a == b` / `a < b` directly.
 *
 * Note: All methods throw std::runtime_error if the runtime hook has not been
 * initialized by the host or if a predicate callback reports an error (< 0).
 */

#pragma once

#include "falcon-typing/falcon_ffi.h"
#include <stdexcept>
#include <vector>
namespace falcon::typing::ffi::wrapper {
class Runtime {
public:
  // Called automatically by the C hook below
  static void init(const FalconRuntimeAPI *api) noexcept { instance_ = api; }

  static bool is_initialized() noexcept { return instance_ != nullptr; }

  // -----------------------------------------------------------------------
  // Comparison & Ordering
  // -----------------------------------------------------------------------
  static bool equals(const FalconParamEntry &lhs, const FalconParamEntry &rhs) {
    return invoke_predicate(require().equals, lhs, rhs);
  }

  static bool not_equals(const FalconParamEntry &lhs,
                         const FalconParamEntry &rhs) {
    return invoke_predicate(require().not_equals, lhs, rhs);
  }

  static bool less_than(const FalconParamEntry &lhs,
                        const FalconParamEntry &rhs) {
    return invoke_predicate(require().less_than, lhs, rhs);
  }

  static bool greater_than(const FalconParamEntry &lhs,
                           const FalconParamEntry &rhs) {
    return invoke_predicate(require().greater_than, lhs, rhs);
  }

  // -----------------------------------------------------------------------
  // Deep Copy
  // -----------------------------------------------------------------------
  static std::vector<FalconResultSlot> deep_copy(const FalconParamEntry &input,
                                                 int32_t max_slots = 1) {
    const auto &api = require();
    if (api.deep_copy == nullptr) {
      throw std::runtime_error("FalconRuntimeAPI: deep_copy not supported");
    }

    std::vector<FalconResultSlot> slots(max_slots);
    int32_t count = max_slots;
    api.deep_copy(api.context, &input, slots.data(), &count);
    slots.resize(count);
    return slots;
  }

private:
  using PredicateFn = int32_t (*)(FalconRuntimeContext *,
                                  const FalconParamEntry *,
                                  const FalconParamEntry *);

  static inline const FalconRuntimeAPI *instance_ = nullptr;

  static const FalconRuntimeAPI &require() {
    if (instance_ == nullptr) {
      throw std::runtime_error("FalconRuntime has not been initialized");
    }
    return *instance_;
  }

  static bool invoke_predicate(PredicateFn fn, const FalconParamEntry &lhs,
                               const FalconParamEntry &rhs) {
    const auto &api = require();
    if (fn == nullptr) {
      throw std::runtime_error("FalconRuntimeAPI: requested predicate is null");
    }
    const int32_t res = fn(api.context, &lhs, &rhs);
    if (res < 0) {
      throw std::runtime_error("FalconRuntimeAPI: predicate evaluation failed");
    }
    return res != 0;
  }
};

} // namespace falcon::typing::ffi::wrapper

// -------------------------------------------------------------------------
// C++ Operator Overloads for FalconParamEntry
// -------------------------------------------------------------------------
// Kept as free functions in the global namespace (where FalconParamEntry lives)
// so Argument-Dependent Lookup (ADL) finds them automatically without altering
// the C99 struct layout.

inline bool operator==(const FalconParamEntry &lhs,
                       const FalconParamEntry &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::equals(lhs, rhs);
}

inline bool operator!=(const FalconParamEntry &lhs,
                       const FalconParamEntry &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::not_equals(lhs, rhs);
}

inline bool operator<(const FalconParamEntry &lhs,
                      const FalconParamEntry &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::less_than(lhs, rhs);
}

inline bool operator>(const FalconParamEntry &lhs,
                      const FalconParamEntry &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::greater_than(lhs, rhs);
}

extern "C" inline void FalconRuntimeInit(const FalconRuntimeAPI *api) {
  falcon::typing::ffi::wrapper::Runtime::init(api);
}
