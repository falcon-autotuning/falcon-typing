/**
 * @file FFIHelpers.hpp
 * @brief C++ helpers for converting between FalconFFI C types and
 *        RuntimeValue / ParameterMap / FunctionResult.
 *
 * Two directions:
 *   - engine_to_c  : ParameterMap  → FalconParamEntry[]   (before call)
 *   - c_to_engine  : FalconResultSlot[] → FunctionResult  (after call)
 *   - wrapper_from_c : FalconParamEntry[] → ParameterMap  (in wrapper)
 *   - wrapper_to_c   : FunctionResult → FalconResultSlot[]  (in wrapper)
 */
#pragma once

#include "falcon-typing/PrimitiveTypes.hpp"
#include "falcon-typing/falcon_ffi.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace falcon::typing::ffi {

inline RuntimeValue unpack_value(const FalconParamEntry &e) {
  switch (e.tag) {
  case FALCON_TYPE_NIL:
    return nullptr;

  case FALCON_TYPE_INT:
    return e.value.int_val;

  case FALCON_TYPE_FLOAT:
    return e.value.float_val;

  case FALCON_TYPE_BOOL:
    return e.value.bool_val != 0;

  case FALCON_TYPE_STRING:
    return std::string(e.value.str.ptr, static_cast<size_t>(e.value.str.len));

  case FALCON_TYPE_OPAQUE: {
    std::string tn = e.value.opaque.type_name ? e.value.opaque.type_name : "";

    void *ptr = e.value.opaque.ptr;

    if (tn == "StructInstance") {
      using SP = std::shared_ptr<StructInstance>;
      return *static_cast<SP *>(ptr);
    }
    if (tn == "TupleValue") {
      using SP = std::shared_ptr<TupleValue>;
      return *static_cast<SP *>(ptr);
    }
    if (tn == "ArrayValue") {
      using SP = std::shared_ptr<ArrayValue>;
      return *static_cast<SP *>(ptr);
    }
    if (tn == "ErrorObject") {
      using SP = std::shared_ptr<ErrorObject>;
      return **static_cast<SP *>(ptr);
    }
    auto inner = *reinterpret_cast<std::shared_ptr<void> *>(ptr);

    auto inst = std::make_shared<StructInstance>(tn);

    inst->native_handle = inner;

    return inst;
  }

  default:
    return nullptr;
  }
}
inline RuntimeValue unpack_value(FalconResultSlot &s) {
  switch (s.tag) {
  case FALCON_TYPE_NIL:
    return nullptr;

  case FALCON_TYPE_INT:
    return s.value.int_val;

  case FALCON_TYPE_FLOAT:
    return s.value.float_val;

  case FALCON_TYPE_BOOL:
    return s.value.bool_val != 0;

  case FALCON_TYPE_STRING: {
    std::string str(s.value.str.ptr, static_cast<size_t>(s.value.str.len));

    ::free(s.value.str.ptr);

    return str;
  }

  case FALCON_TYPE_OPAQUE: {
    std::string tn = s.value.opaque.type_name ? s.value.opaque.type_name : "";

    void *ptr = s.value.opaque.ptr;
    auto del = s.value.opaque.deleter;

    if (tn == "TupleValue") {
      using SP = std::shared_ptr<TupleValue>;

      auto value = *static_cast<SP *>(ptr);

      if (del)
        del(ptr);

      return value;
    }

    if (tn == "ArrayValue") {
      using SP = std::shared_ptr<ArrayValue>;

      auto value = *static_cast<SP *>(ptr);

      if (del)
        del(ptr);

      return value;
    }

    if (tn == "StructInstance") {
      using SP = std::shared_ptr<StructInstance>;

      auto value = *static_cast<SP *>(ptr);

      if (del)
        del(ptr);

      return value;
    }

    if (tn == "ErrorObject") {
      using SP = std::shared_ptr<ErrorObject>;

      auto value = **static_cast<SP *>(ptr);

      if (del)
        del(ptr);

      return value;
    }

    auto inner_sp = *reinterpret_cast<std::shared_ptr<void> *>(ptr);

    auto inst = std::make_shared<StructInstance>(tn);

    inst->native_handle = inner_sp;

    if (del)
      del(ptr);

    return inst;
  }

  default:
    return nullptr;
  }
}

inline void pack_value(const RuntimeValue &value, FalconResultSlot &s) {
  std::visit(
      [&](auto &&val) {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, int64_t>) {
          s.tag = FALCON_TYPE_INT;
          s.value.int_val = val;

        } else if constexpr (std::is_same_v<T, double>) {
          s.tag = FALCON_TYPE_FLOAT;
          s.value.float_val = val;

        } else if constexpr (std::is_same_v<T, bool>) {
          s.tag = FALCON_TYPE_BOOL;
          s.value.bool_val = val ? 1 : 0;

        } else if constexpr (std::is_same_v<T, std::string>) {
          s.tag = FALCON_TYPE_STRING;
          s.value.str.len = static_cast<int32_t>(val.size());
          s.value.str.ptr = static_cast<char *>(::malloc(val.size() + 1));
          std::memcpy(s.value.str.ptr, val.c_str(), val.size() + 1);

        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
          s.tag = FALCON_TYPE_NIL;

        } else if constexpr (std::is_same_v<T, ErrorObject>) {
          using SP = std::shared_ptr<ErrorObject>;

          auto *heap_sp = new SP(std::make_shared<ErrorObject>(val));

          s.tag = FALCON_TYPE_OPAQUE;
          s.value.opaque.ptr = heap_sp;
          s.value.opaque.type_name = "ErrorObject";
          s.value.opaque.deleter = [](void *p) { delete static_cast<SP *>(p); };

        } else {
          using SP = T;

          auto *heap_sp = new SP(val);

          s.tag = FALCON_TYPE_OPAQUE;
          s.value.opaque.ptr = heap_sp;

          if constexpr (std::is_same_v<SP, std::shared_ptr<TupleValue>>) {

            s.value.opaque.type_name = "TupleValue";

          } else if constexpr (std::is_same_v<SP,
                                              std::shared_ptr<ArrayValue>>) {

            s.value.opaque.type_name = "ArrayValue";

          } else if constexpr (std::is_same_v<
                                   SP, std::shared_ptr<StructInstance>>) {

            if (val && val->is_native()) {

              auto *svp = new std::shared_ptr<void>(
                  std::static_pointer_cast<void>(val->native_handle.value()));

              delete heap_sp;

              s.value.opaque.ptr = svp;
              s.value.opaque.type_name = val->type_name.c_str();

              s.value.opaque.deleter = [](void *p) {
                delete static_cast<std::shared_ptr<void> *>(p);
              };

              return;
            }

            s.value.opaque.type_name = "StructInstance";

          } else {
            s.value.opaque.type_name = "unknown_opaque";
          }

          s.value.opaque.deleter = [](void *p) { delete static_cast<SP *>(p); };
        }
      },
      value);
}

inline void pack_value(const RuntimeValue &value, FalconParamEntry &e,
                       std::deque<std::string> &string_storage) {
  std::visit(
      [&](auto &&val) {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, int64_t>) {
          e.tag = FALCON_TYPE_INT;
          e.value.int_val = val;

        } else if constexpr (std::is_same_v<T, double>) {
          e.tag = FALCON_TYPE_FLOAT;
          e.value.float_val = val;

        } else if constexpr (std::is_same_v<T, bool>) {
          e.tag = FALCON_TYPE_BOOL;
          e.value.bool_val = val ? 1 : 0;

        } else if constexpr (std::is_same_v<T, std::string>) {
          string_storage.push_back(val);

          e.tag = FALCON_TYPE_STRING;
          e.value.str.ptr = string_storage.back().c_str();
          e.value.str.len = static_cast<int32_t>(string_storage.back().size());

        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
          e.tag = FALCON_TYPE_NIL;

        } else if constexpr (std::is_same_v<T, ErrorObject>) {
          using SP = std::shared_ptr<ErrorObject>;

          auto *heap_sp = new SP(std::make_shared<ErrorObject>(val));

          e.tag = FALCON_TYPE_OPAQUE;
          e.value.opaque.ptr = heap_sp;
          e.value.opaque.type_name = "ErrorObject";
          e.value.opaque.deleter = [](void *p) { delete static_cast<SP *>(p); };

        } else {
          using SP = T;

          if constexpr (std::is_same_v<SP, std::shared_ptr<StructInstance>>) {
            if (val && val->is_native()) {
              auto *svp = new std::shared_ptr<void>(
                  std::static_pointer_cast<void>(val->native_handle.value()));

              e.tag = FALCON_TYPE_OPAQUE;
              e.value.opaque.ptr = svp;
              e.value.opaque.type_name = val->type_name.c_str();
              e.value.opaque.deleter = [](void *p) {
                delete static_cast<std::shared_ptr<void> *>(p);
              };
              return;
            }
          }

          auto *heap_sp = new SP(val);

          e.tag = FALCON_TYPE_OPAQUE;
          e.value.opaque.ptr = heap_sp;

          if constexpr (std::is_same_v<SP, std::shared_ptr<TupleValue>>) {
            e.value.opaque.type_name = "TupleValue";
          } else if constexpr (std::is_same_v<SP,
                                              std::shared_ptr<ArrayValue>>) {
            e.value.opaque.type_name = "ArrayValue";
          } else if constexpr (std::is_same_v<
                                   SP, std::shared_ptr<StructInstance>>) {
            e.value.opaque.type_name = "StructInstance";
          } else {
            e.value.opaque.type_name = "unknown_opaque";
          }

          e.value.opaque.deleter = [](void *p) { delete static_cast<SP *>(p); };
        }
      },
      value);
}

// ============================================================================
// Helpers used by the ENGINE side (packing params, unpacking results)
// ============================================================================
namespace engine {

struct PackedParams {
  std::vector<FalconParamEntry> entries;
  std::deque<std::string> key_storage;
  std::deque<std::string> str_storage;
};

inline PackedParams pack_params(const ParameterMap &params) {
  PackedParams packed;

  packed.entries.reserve(params.size());

  for (const auto &[k, v] : params) {
    packed.key_storage.push_back(k);

    FalconParamEntry e{};
    e.key = packed.key_storage.back().c_str();

    pack_value(v, e, packed.str_storage);

    packed.entries.push_back(e);
  }

  return packed;
}

inline FunctionResult unpack_results(FalconResultSlot *slots, int32_t count) {
  FunctionResult result;
  result.reserve(static_cast<size_t>(count));

  for (int32_t i = 0; i < count; ++i) {
    result.push_back(unpack_value(slots[i]));
  }

  return result;
}

} // namespace engine

// ============================================================================
// Helpers used by the WRAPPER side (unpacking params, packing results)
// ============================================================================
namespace wrapper {

inline ParameterMap unpack_params(const FalconParamEntry *entries,
                                  int32_t count) {
  ParameterMap result;

  for (int32_t i = 0; i < count; ++i) {
    result[entries[i].key] = unpack_value(entries[i]);
  }

  return result;
}

/**
 * Extract an opaque (user-defined) shared_ptr from a FalconParamEntry array.
 *
 * Convention: for native structs, ptr is a heap-allocated shared_ptr<void>*
 * aliasing T.  We static_pointer_cast back to T.
 */
template <typename T>
inline std::shared_ptr<T> get_opaque(const FalconParamEntry *entries,
                                     int32_t count, const char *key) {
  for (int32_t i = 0; i < count; ++i) {
    if (std::strcmp(entries[i].key, key) == 0 &&
        entries[i].tag == FALCON_TYPE_OPAQUE) {
      // ptr is a heap-allocated shared_ptr<void>* (alias to T).
      auto sv =
          *static_cast<std::shared_ptr<void> *>(entries[i].value.opaque.ptr);
      return std::static_pointer_cast<T>(sv);
    }
  }
  throw std::runtime_error(std::string("FFI: opaque param not found: ") + key);
}

inline void pack_results(const FunctionResult &result, FalconResultSlot *slots,
                         int32_t capacity, int32_t *out_count) {

  int32_t n = std::min(static_cast<int32_t>(result.size()), capacity);

  *out_count = n;

  for (int32_t i = 0; i < n; ++i) {
    pack_value(result.at(i), slots[i]);
  }
}

inline void pack_single(RuntimeValue val, FalconResultSlot *slots,
                        int32_t *out_count) {
  *out_count = 1;
  pack_value(val, slots[0]);
}

} // namespace wrapper
} // namespace falcon::typing::ffi
