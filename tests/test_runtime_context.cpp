
/**
 * @file test_falcon_runtime.cpp
 * @brief Tests for FalconRuntime.hpp and the C99 FalconRuntimeAPI ABI:
 *        verifies host callback registration, context forwarding, comparison
 *        operators, error handling, and deep-copy round-trips across the FFI.
 */

#include <stdexcept>
#define FALCON_IMPLEMENT_RUNTIME_HOOK
#include "falcon-typing/FFIHelpers.hpp"
#include "falcon-typing/FalconRuntime.hpp"
#include "falcon-typing/PrimitiveTypes.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace falcon::typing;
using namespace falcon::typing::ffi;

// ─────────────────────────────────────────────────────────────────────────────
// Mock Host Runtime: simulates the Falcon engine side of FalconRuntimeAPI
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct MockHostContext {
  int call_count = 0;
  bool force_error = false;
};

RuntimeValue unpack_single(const FalconParamEntry *entry) {
  return unpack_value(*entry);
}

int32_t mock_equals(FalconRuntimeContext *ctx, const FalconParamEntry *lhs,
                    const FalconParamEntry *rhs) {
  auto *host = reinterpret_cast<MockHostContext *>(ctx);
  host->call_count++;
  if (host->force_error)
    return -1;

  RuntimeValue a = unpack_single(lhs);
  RuntimeValue b = unpack_single(rhs);

  if (a.index() != b.index())
    return 0;
  if (std::holds_alternative<int64_t>(a))
    return std::get<int64_t>(a) == std::get<int64_t>(b) ? 1 : 0;
  if (std::holds_alternative<double>(a))
    return std::get<double>(a) == std::get<double>(b) ? 1 : 0;
  if (std::holds_alternative<bool>(a))
    return std::get<bool>(a) == std::get<bool>(b) ? 1 : 0;
  if (std::holds_alternative<std::string>(a))
    return std::get<std::string>(a) == std::get<std::string>(b) ? 1 : 0;
  if (std::holds_alternative<std::nullptr_t>(a))
    return 1;
  if (std::holds_alternative<std::shared_ptr<StructInstance>>(a)) {
    auto sa = std::get<std::shared_ptr<StructInstance>>(a);
    auto sb = std::get<std::shared_ptr<StructInstance>>(b);
    return (sa->type_name == sb->type_name &&
            sa->get_field("id") == sb->get_field("id"))
               ? 1
               : 0;
  }
  return 0;
}

int32_t mock_not_equals(FalconRuntimeContext *ctx, const FalconParamEntry *lhs,
                        const FalconParamEntry *rhs) {
  int32_t eq = mock_equals(ctx, lhs, rhs);
  return (eq < 0) ? eq : (eq == 0 ? 1 : 0);
}

int32_t mock_less_than(FalconRuntimeContext *ctx, const FalconParamEntry *lhs,
                       const FalconParamEntry *rhs) {
  auto *host = reinterpret_cast<MockHostContext *>(ctx);
  host->call_count++;
  if (host->force_error)
    return -1;

  RuntimeValue a = unpack_single(lhs);
  RuntimeValue b = unpack_single(rhs);

  if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b))
    return std::get<int64_t>(a) < std::get<int64_t>(b) ? 1 : 0;
  if (std::holds_alternative<std::string>(a) &&
      std::holds_alternative<std::string>(b))
    return std::get<std::string>(a) < std::get<std::string>(b) ? 1 : 0;
  return 0;
}

int32_t mock_greater_than(FalconRuntimeContext *ctx,
                          const FalconParamEntry *lhs,
                          const FalconParamEntry *rhs) {
  auto *host = reinterpret_cast<MockHostContext *>(ctx);
  host->call_count++;
  if (host->force_error)
    return -1;

  RuntimeValue a = unpack_single(lhs);
  RuntimeValue b = unpack_single(rhs);

  if (std::holds_alternative<int64_t>(a) && std::holds_alternative<int64_t>(b))
    return std::get<int64_t>(a) > std::get<int64_t>(b) ? 1 : 0;
  if (std::holds_alternative<std::string>(a) &&
      std::holds_alternative<std::string>(b))
    return std::get<std::string>(a) > std::get<std::string>(b) ? 1 : 0;
  return 0;
}

void mock_deep_copy(FalconRuntimeContext *ctx, const FalconParamEntry *input,
                    FalconResultSlot *output) {
  auto *host = reinterpret_cast<MockHostContext *>(ctx);
  host->call_count++;

  RuntimeValue val = unpack_single(input);

  if (std::holds_alternative<std::shared_ptr<StructInstance>>(val)) {
    auto orig = std::get<std::shared_ptr<StructInstance>>(val);

    auto clone = std::make_shared<StructInstance>(orig->type_name);
    clone->set_field("id", orig->get_field("id"));

    val = clone;
  }

  pack_value(val, *output);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

class FalconRuntimeTest : public ::testing::Test {
protected:
  MockHostContext host_ctx_;
  FalconRuntimeAPI api_{};

  void SetUp() override {
    api_.context = reinterpret_cast<FalconRuntimeContext *>(&host_ctx_);
    api_.equals = &mock_equals;
    api_.not_equals = &mock_not_equals;
    api_.less_than = &mock_less_than;
    api_.greater_than = &mock_greater_than;
    api_.deep_copy = &mock_deep_copy;

    // Simulate dlsym("FalconRuntimeInit") invoked by the engine on load
    FalconRuntimeInit(&api_);
  }

  void TearDown() override {
    falcon::typing::ffi::wrapper::Runtime::init(nullptr);
  }

  /// Helper to keep PackedParams storage alive while inspecting a single entry
  struct SingleParam {
    engine::PackedParams storage;
    const FalconParamEntry &entry() const { return storage.entries[0]; }
  };

  SingleParam make_param(const std::string &key, RuntimeValue val) {
    ParameterMap pm;
    pm[key] = std::move(val);
    return SingleParam{engine::pack_params(pm)};
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Initialization & Error Guards
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FalconRuntimeTest, UninitializedRuntimeThrows) {
  falcon::typing::ffi::wrapper::Runtime::init(nullptr);

  EXPECT_FALSE(falcon::typing::ffi::wrapper::Runtime::is_initialized());

  RuntimeValue a = int64_t{1};
  RuntimeValue b = int64_t{1};

  EXPECT_THROW(falcon::typing::ffi::wrapper::Runtime::equals(a, b),
               std::runtime_error);
  EXPECT_THROW((void)(a == b), std::runtime_error);
  EXPECT_THROW(falcon::typing::ffi::wrapper::Runtime::deep_copy(a),
               std::runtime_error);
}

TEST_F(FalconRuntimeTest, NullFunctionPointerInApiThrows) {
  api_.less_than = nullptr;
  FalconRuntimeInit(&api_);

  RuntimeValue a = int64_t{1};
  RuntimeValue b = int64_t{2};

  EXPECT_THROW(falcon::typing::ffi::wrapper::Runtime::less_than(a, b),
               std::runtime_error);
}

TEST_F(FalconRuntimeTest, NegativeHostReturnCodeThrows) {
  host_ctx_.force_error = true;

  RuntimeValue a = int64_t{1};
  RuntimeValue b = int64_t{2};

  EXPECT_THROW(falcon::typing::ffi::wrapper::Runtime::equals(a, b),
               std::runtime_error);

  EXPECT_THROW(falcon::typing::ffi::wrapper::Runtime::less_than(a, b),
               std::runtime_error);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Context Threading & Direct API Calls
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FalconRuntimeTest, ContextIsForwardedToHostCallbacks) {
  EXPECT_TRUE(falcon::typing::ffi::wrapper::Runtime::is_initialized());

  EXPECT_EQ(host_ctx_.call_count, 0);

  RuntimeValue a = int64_t{42};
  RuntimeValue b = int64_t{42};

  EXPECT_TRUE(falcon::typing::ffi::wrapper::Runtime::equals(a, b));
  EXPECT_EQ(host_ctx_.call_count, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: C++ Operator Overloads (ADL) & STL Compatibility
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FalconRuntimeTest, ScalarComparisonOperators) {
  RuntimeValue ten = int64_t{10};
  RuntimeValue ten_copy = int64_t{10};
  RuntimeValue twenty = int64_t{20};

  EXPECT_TRUE(ten == ten_copy);
  EXPECT_FALSE(ten != ten_copy);

  EXPECT_TRUE(ten != twenty);

  EXPECT_TRUE(ten < twenty);
  EXPECT_FALSE(twenty < ten);

  EXPECT_TRUE(twenty > ten);
  EXPECT_FALSE(ten > twenty);
}

TEST_F(FalconRuntimeTest, OpaqueStructSemanticEquality) {
  auto s1 = std::make_shared<StructInstance>("Instrument");
  s1->set_field("id", int64_t{101});

  auto s2 = std::make_shared<StructInstance>("Instrument");
  s2->set_field("id", int64_t{101});

  auto s3 = std::make_shared<StructInstance>("Instrument");
  s3->set_field("id", int64_t{202});

  RuntimeValue v1 = s1;
  RuntimeValue v2 = s2;
  RuntimeValue v3 = s3;

  EXPECT_TRUE(v1 == v2);
  EXPECT_TRUE(v1 != v3);
}

TEST_F(FalconRuntimeTest, WorksWithStlAlgorithms) {
  std::vector<RuntimeValue> items = {
      std::string{"charlie"}, std::string{"alpha"}, std::string{"bravo"}};

  RuntimeValue target = std::string{"bravo"};

  auto it = std::find(items.begin(), items.end(), target);

  ASSERT_NE(it, items.end());

  EXPECT_EQ(std::get<std::string>(*it), "bravo");

  std::sort(items.begin(), items.end());

  EXPECT_EQ(std::get<std::string>(items[0]), "alpha");
  EXPECT_EQ(std::get<std::string>(items[1]), "bravo");
  EXPECT_EQ(std::get<std::string>(items[2]), "charlie");
}
// ─────────────────────────────────────────────────────────────────────────────
// Tests: Deep Copy Round-Trip
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FalconRuntimeTest, DeepCopyClonesOpaqueStructAcrossAbi) {
  auto orig = std::make_shared<StructInstance>("Measurement");

  orig->set_field("id", int64_t{55});

  RuntimeValue copied = wrapper::Runtime::deep_copy(orig);

  ASSERT_TRUE(std::holds_alternative<std::shared_ptr<StructInstance>>(copied));

  auto cloned = std::get<std::shared_ptr<StructInstance>>(copied);

  ASSERT_NE(cloned, nullptr);
  EXPECT_NE(cloned.get(), orig.get());
  EXPECT_EQ(cloned->type_name, "Measurement");
  EXPECT_EQ(std::get<int64_t>(cloned->get_field("id")), 55);

  orig->set_field("id", int64_t{999});

  EXPECT_EQ(std::get<int64_t>(orig->get_field("id")), 999);
  EXPECT_EQ(std::get<int64_t>(cloned->get_field("id")), 55);
}
