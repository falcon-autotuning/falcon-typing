/**
 * @file falcon_ffi.h
 * @brief Pure-C ABI types for crossing the FFI boundary.
 *
 * This header is intentionally plain C so it can be included by wrapper
 * translation units compiled as C or C++ without triggering C-linkage
 * warnings about C++ types.
 *
 * Wrappers export symbols with the signature:
 *   void MyFunc(const FalconParamEntry*, int32_t,
 *               FalconResultSlot*, int32_t*);
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

/* -------------------------------------------------------------------------
 * Type tags
 * -----------------------------------------------------------------------*/
using FalconTypeTag = enum FalconTypeTag : uint8_t {
  FALCON_TYPE_NIL = 0,
  FALCON_TYPE_INT = 1,
  FALCON_TYPE_FLOAT = 2,
  FALCON_TYPE_BOOL = 3,
  FALCON_TYPE_STRING = 4, /* value.str_val / str_len */
  FALCON_TYPE_OPAQUE = 5, /* value.opaque_val  — heap-allocated shared_ptr<T>*
                             together with a deleter fn;  type_name carries
                             the C++ RTTI name for reconstruction */
  FALCON_TYPE_ERROR = 6,  /* value.str_val carries the message */
};

/* -------------------------------------------------------------------------
 * A single parameter entry (for ParameterMap)
 * -----------------------------------------------------------------------*/
using FalconParamEntry = struct FalconParamEntry {
  const char *key; /* NUL-terminated parameter name */
  FalconTypeTag tag;
  union {
    int64_t int_val;
    double float_val;
    int32_t bool_val; /* 0 = false, 1 = true */
    struct {
      const char *ptr; /* NUL-terminated; owned by the *caller* side */
      int32_t len;     /* NOT including NUL */
    } str;
    struct {
      void *ptr;               /* actually shared_ptr<T>* on the heap */
      const char *type_name;   /* e.g. "ConnectionSP", "QuantitySP", or
                                  user struct type name */
      void (*deleter)(void *); /* called by the engine after it has read the
                                  value and wrapped it in its own shared_ptr */
    } opaque;
  } value;
};

/* -------------------------------------------------------------------------
 * A single result slot (for FunctionResult)
 * -----------------------------------------------------------------------*/
using FalconResultSlot = struct FalconResultSlot {
  FalconTypeTag tag;
  union {
    int64_t int_val;
    double float_val;
    int32_t bool_val;
    struct {
      char *ptr; /* heap-allocated by the wrapper; freed by engine */
      int32_t len;
    } str;
    struct {
      void *ptr; /* heap-allocated shared_ptr<T>* by the wrapper */
      const char *type_name;
      void (*deleter)(void *);
    } opaque;
    struct {
      char *message; /* heap-allocated; freed by engine */
      int32_t is_fatal;
    } error;
  } value;
};

/* -------------------------------------------------------------------------
 * The C-ABI function pointer type that every exported wrapper function
 * must conform to.
 * -----------------------------------------------------------------------*/
using FalconFFIFunc = void (*)(
    const FalconParamEntry *params, int32_t param_count,
    FalconResultSlot *out_slots,
    int32_t *out_count /* in: capacity; out: written count */
);

/* -------------------------------------------------------------------------
 * Runtime semantics & context (optional host callbacks)
 * -------------------------------------------------------------------------
 * FalconRuntimeContext represents the language semantics of a specific
 * Falcon execution universe (equality, ordering, deep-copy, type/function
 * dispatch).
 *
 * If your library needs to perform runtime operations on opaque or dynamic
 * Falcon values (such as checking element equality inside a container):
 *   1. Store a static pointer:
 *        static const FalconRuntimeAPI* g_runtime = nullptr;
 *   2. Export the optional initialization hook:
 *        extern "C" void FalconRuntimeInit(const FalconRuntimeAPI* api) {
 *          g_runtime = api;
 *        }
 *      The engine will automatically look up and invoke this symbol via
 *      dlsym immediately after loading the library.
 *   3. Always pass `g_runtime->context` as the first argument when invoking
 *      any callback (e.g., `g_runtime->equals(g_runtime->context, &a, &b)`).
 *   4. Guard against `g_runtime == nullptr` if your library might be loaded
 *      in standalone or test environments where the hook is not invoked.
 * -----------------------------------------------------------------------*/
typedef struct FalconRuntimeContext FalconRuntimeContext;

typedef struct FalconRuntimeAPI {
  FalconRuntimeContext *context;

  int32_t (*equals)(FalconRuntimeContext *context, const FalconParamEntry *lhs,
                    const FalconParamEntry *rhs);

  int32_t (*not_equals)(FalconRuntimeContext *context,
                        const FalconParamEntry *lhs,
                        const FalconParamEntry *rhs);

  int32_t (*less_than)(FalconRuntimeContext *context,
                       const FalconParamEntry *lhs,
                       const FalconParamEntry *rhs);

  int32_t (*greater_than)(FalconRuntimeContext *context,
                          const FalconParamEntry *lhs,
                          const FalconParamEntry *rhs);

  void (*deep_copy)(FalconRuntimeContext *context,
                    const FalconParamEntry *input, FalconResultSlot *output,
                    int32_t *output_count);
} FalconRuntimeAPI;

/* Optional initialization hook exported by wrapper libraries */
void FalconRuntimeInit(const FalconRuntimeAPI *api);

#ifdef __cplusplus
} /* extern "C" */
#endif
