
/**
 * @file test_falcon_runtime.cpp
 * @brief Tests for FalconRuntime.hpp and the C99 FalconRuntimeAPI ABI:
 *        verifies host callback registration, context forwarding, comparison
 *        operators, error handling, and deep-copy round-trips across the FFI.
 */

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
  ParameterMap pm = wrapper::unpack_params(entry, 1);
  return pm.begin()->second;
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
                    FalconResultSlot *output, int32_t *output_count) {
  auto *host = reinterpret_cast<MockHostContext *>(ctx);
  host->call_count++;

  RuntimeValue val = unpack_single(input);

  // Clone StructInstance to prove true deep copy rather than pointer aliasing
  if (std::holds_alternative<std::shared_ptr<StructInstance>>(val)) {
    auto orig = std::get<std::shared_ptr<StructInstance>>(val);
    auto clone = std::make_shared<StructInstance>(orig->type_name);
    clone->set_field("id", orig->get_field("id"));
    val = clone;
  }

  FunctionResult fr{val};
  wrapper::pack_results(fr, output, *output_count, output_count);
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

  auto a = make_param("a", int64_t{1});
  auto b = make_param("b", int64_t{1});

  EXPECT_THROW(
      falcon::typing::ffi::wrapper::Runtime::equals(a.entry(), b.entry()),
      std::runtime_error);
  EXPECT_THROW((void)(a.entry() == b.entry()), std::runtime_error);
  EXPECT_THROW(falcon::typing::ffi::wrapper::Runtime::deep_copy(a.entry()),
               std::runtime_error);
}

TEST_F(FalconRuntimeTest, NullFunctionPointerInApiThrows) {
  api_.less_than = nullptr;
  FalconRuntimeInit(&api_);

  auto a = make_param("a", int64_t{1});
  auto b = make_param("b", int64_t{2});

  EXPECT_THROW(
      falcon::typing::ffi::wrapper::Runtime::less_than(a.entry(), b.entry()),
      std::runtime_error);
}

TEST_F(FalconRuntimeTest, NegativeHostReturnCodeThrows) {
  host_ctx_.force_error = true;

  auto a = make_param("a", int64_t{1});
  auto b = make_param("b", int64_t{2});

  EXPECT_THROW(
      falcon::typing::ffi::wrapper::Runtime::equals(a.entry(), b.entry()),
      std::runtime_error);
  EXPECT_THROW(
      falcon::typing::ffi::wrapper::Runtime::less_than(a.entry(), b.entry()),
      std::runtime_error);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Context Threading & Direct API Calls
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FalconRuntimeTest, ContextIsForwardedToHostCallbacks) {
  EXPECT_TRUE(falcon::typing::ffi::wrapper::Runtime::is_initialized());
  EXPECT_EQ(host_ctx_.call_count, 0);

  auto a = make_param("a", int64_t{42});
  auto b = make_param("b", int64_t{42});

  EXPECT_TRUE(
      falcon::typing::ffi::wrapper::Runtime::equals(a.entry(), b.entry()));
  EXPECT_EQ(host_ctx_.call_count, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: C++ Operator Overloads (ADL) & STL Compatibility
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FalconRuntimeTest, ScalarComparisonOperators) {
  auto ten = make_param("x", int64_t{10});
  auto ten_copy = make_param("y", int64_t{10});
  auto twenty = make_param("z", int64_t{20});

  EXPECT_TRUE(ten.entry() == ten_copy.entry());
  EXPECT_FALSE(ten.entry() != ten_copy.entry());
  EXPECT_TRUE(ten.entry() != twenty.entry());

  EXPECT_TRUE(ten.entry() < twenty.entry());
  EXPECT_FALSE(twenty.entry() < ten.entry());

  EXPECT_TRUE(twenty.entry() > ten.entry());
  EXPECT_FALSE(ten.entry() > twenty.entry());
}

TEST_F(FalconRuntimeTest, OpaqueStructSemanticEquality) {
  // Two distinct shared_ptr instances with identical semantic fields
  auto s1 = std::make_shared<StructInstance>("Instrument");
  s1->set_field("id", int64_t{101});

  auto s2 = std::make_shared<StructInstance>("Instrument");
  s2->set_field("id", int64_t{101});

  auto s3 = std::make_shared<StructInstance>("Instrument");
  s3->set_field("id", int64_t{202});

  auto p1 = make_param("p1", s1);
  auto p2 = make_param("p2", s2);
  auto p3 = make_param("p3", s3);

  // Wrapper sees opaque pointers, but delegates semantic equality to host
  EXPECT_TRUE(p1.entry() == p2.entry());
  EXPECT_TRUE(p1.entry() != p3.entry());
}

TEST_F(FalconRuntimeTest, WorksWithStlAlgorithms) {
  ParameterMap pm;
  pm["c"] = std::string{"charlie"};
  pm["a"] = std::string{"alpha"};
  pm["b"] = std::string{"bravo"};
  auto packed = engine::pack_params(pm);

  auto target = make_param("target", std::string{"bravo"});

  // std::find uses operator== via ADL
  auto it =
      std::find(packed.entries.begin(), packed.entries.end(), target.entry());
  ASSERT_NE(it, packed.entries.end());
  EXPECT_STREQ(it->value.str.ptr, "bravo");

  // std::sort uses operator< via ADL
  std::vector<FalconParamEntry> items = {packed.entries[2], packed.entries[0],
                                         packed.entries[1]};
  std::sort(items.begin(), items.end());
  EXPECT_STREQ(items[0].value.str.ptr, "alpha");
  EXPECT_STREQ(items[1].value.str.ptr, "bravo");
  EXPECT_STREQ(items[2].value.str.ptr, "charlie");
}
// ─────────────────────────────────────────────────────────────────────────────
// Tests: Deep Copy Round-Trip
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(FalconRuntimeTest, DeepCopyClonesOpaqueStructAcrossAbi) {
  auto orig = std::make_shared<StructInstance>("Measurement");
  orig->set_field("id", int64_t{55});

  auto input = make_param("src", orig);

  // Wrapper invokes host deep_copy
  std::vector<FalconResultSlot> slots =
      wrapper::Runtime::deep_copy(input.entry());
  ASSERT_EQ(slots.size(), 1u);

  // Unpack slots back into RuntimeValue (also cleans up allocated slot buffers)
  FunctionResult unpacked =
      engine::unpack_results(slots.data(), static_cast<int32_t>(slots.size()));
  ASSERT_EQ(unpacked.size(), 1u);
  ASSERT_TRUE(
      std::holds_alternative<std::shared_ptr<StructInstance>>(unpacked[0]));

  auto cloned = std::get<std::shared_ptr<StructInstance>>(unpacked[0]);
  ASSERT_NE(cloned, nullptr);
  EXPECT_NE(cloned.get(), orig.get()); // Must be a distinct heap allocation
  EXPECT_EQ(cloned->type_name, "Measurement");
  EXPECT_EQ(std::get<int64_t>(cloned->get_field("id")), 55);

  // Mutate the original and verify changes do NOT propagate to the copy
  orig->set_field("id", int64_t{999});
  EXPECT_EQ(std::get<int64_t>(orig->get_field("id")), 999);
  EXPECT_EQ(std::get<int64_t>(cloned->get_field("id")), 55);
}
