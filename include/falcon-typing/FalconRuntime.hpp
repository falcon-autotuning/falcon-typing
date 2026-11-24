
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

#include "falcon-typing/FFIHelpers.hpp"
#include "falcon-typing/PrimitiveTypes.hpp"
#include "falcon-typing/falcon_ffi.h"
#include <stdexcept>
namespace falcon::typing {
namespace ffi::wrapper {
class Runtime {
public:
  // Called automatically by the C hook below
  static void init(const FalconRuntimeAPI *api) noexcept { instance_ = api; }

  static bool is_initialized() noexcept { return instance_ != nullptr; }

  // -----------------------------------------------------------------------
  // Comparison & Ordering
  // -----------------------------------------------------------------------
  static bool equals(const RuntimeValue &lhs, const RuntimeValue &rhs) {
    FalconParamEntry lhs_entry{};
    FalconParamEntry rhs_entry{};

    std::deque<std::string> storage;

    ffi::pack_value(lhs, lhs_entry, storage);
    ffi::pack_value(rhs, rhs_entry, storage);

    return invoke_predicate(require().equals, lhs_entry, rhs_entry);
  }

  static bool not_equals(const RuntimeValue &lhs, const RuntimeValue &rhs) {
    FalconParamEntry lhs_entry{};
    FalconParamEntry rhs_entry{};

    std::deque<std::string> storage;

    ffi::pack_value(lhs, lhs_entry, storage);
    ffi::pack_value(rhs, rhs_entry, storage);

    return invoke_predicate(require().not_equals, lhs_entry, rhs_entry);
  }

  static bool less_than(const RuntimeValue &lhs, const RuntimeValue &rhs) {
    FalconParamEntry lhs_entry{};
    FalconParamEntry rhs_entry{};

    std::deque<std::string> storage;

    ffi::pack_value(lhs, lhs_entry, storage);
    ffi::pack_value(rhs, rhs_entry, storage);

    return invoke_predicate(require().less_than, lhs_entry, rhs_entry);
  }

  static bool greater_than(const RuntimeValue &lhs, const RuntimeValue &rhs) {
    FalconParamEntry lhs_entry{};
    FalconParamEntry rhs_entry{};

    std::deque<std::string> storage;

    ffi::pack_value(lhs, lhs_entry, storage);
    ffi::pack_value(rhs, rhs_entry, storage);

    return invoke_predicate(require().greater_than, lhs_entry, rhs_entry);
  }

  // -----------------------------------------------------------------------
  // Deep Copy
  // -----------------------------------------------------------------------
  static RuntimeValue deep_copy(const RuntimeValue &value) {

    FalconParamEntry entry{};
    std::deque<std::string> storage;

    ffi::pack_value(value, entry, storage);

    FalconResultSlot slot{};
    require().deep_copy(require().context, &entry, &slot);
    return ffi::unpack_value(slot);
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

} // namespace ffi::wrapper

// -------------------------------------------------------------------------
// C++ Operator Overloads for FalconParamEntry
// -------------------------------------------------------------------------
inline bool operator==(const falcon::typing::RuntimeValue &lhs,
                       const falcon::typing::RuntimeValue &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::equals(lhs, rhs);
}

inline bool operator!=(const falcon::typing::RuntimeValue &lhs,
                       const falcon::typing::RuntimeValue &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::not_equals(lhs, rhs);
}

inline bool operator<(const falcon::typing::RuntimeValue &lhs,
                      const falcon::typing::RuntimeValue &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::less_than(lhs, rhs);
}

inline bool operator>(const falcon::typing::RuntimeValue &lhs,
                      const falcon::typing::RuntimeValue &rhs) {
  return falcon::typing::ffi::wrapper::Runtime::greater_than(lhs, rhs);
}

} // namespace falcon::typing

#ifdef FALCON_IMPLEMENT_RUNTIME_HOOK

extern "C" void FalconRuntimeInit(const FalconRuntimeAPI *api) {
  falcon::typing::ffi::wrapper::Runtime::init(api);
}

#else

#endif
