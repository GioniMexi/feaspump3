// Formatting library for C++ - formatting library implementation tests
//
// Copyright (c) 2012 - present, Victor Zverovich
// All rights reserved.
//
// For the license information refer to format.h.

#define FMT_NOEXCEPT
#undef FMT_SHARED
#include "test-assert.h"

// Include format.cc instead of format.h to test implementation.
#include "../src/format.cc"
#include "fmt/printf.h"

#include <algorithm>
#include <cstring>

#include "gmock.h"
#include "gtest-extra.h"
#include "util.h"

#undef max

using fmt::internal::bigint;
using fmt::internal::fp;
using fmt::internal::max_value;

static_assert(!std::is_copy_constructible<bigint>::value, "");
static_assert(!std::is_copy_assignable<bigint>::value, "");

TEST(BigIntTest, Construct) {
  EXPECT_EQ("", fmt::format("{}", bigint()));
  EXPECT_EQ("42", fmt::format("{}", bigint(0x42)));
  EXPECT_EQ("123456789abcedf0", fmt::format("{}", bigint(0x123456789abcedf0)));
}

TEST(BigIntTest, ShiftLeft) {
  bigint n(0x42);
  n <<= 0;
  EXPECT_EQ("42", fmt::format("{}", n));
  n <<= 1;
  EXPECT_EQ("84", fmt::format("{}", n));
  n <<= 25;
  EXPECT_EQ("108000000", fmt::format("{}", n));
}

TEST(BigIntTest, Multiply) {
  bigint n(0x42);
  n *= 1;
  EXPECT_EQ("42", fmt::format("{}", n));
  n *= 2;
  EXPECT_EQ("84", fmt::format("{}", n));
  n *= 0x12345678;
  EXPECT_EQ("962fc95e0", fmt::format("{}", n));
  auto max = max_value<uint32_t>();
  bigint bigmax(max);
  bigmax *= max;
  EXPECT_EQ("fffffffe00000001", fmt::format("{}", bigmax));
}

template <bool is_iec559> void test_construct_from_double() {
  fmt::print("warning: double is not IEC559, skipping FP tests\n");
}

template <> void test_construct_from_double<true>() {
  auto v = fp(1.23);
  EXPECT_EQ(v.f, 0x13ae147ae147aeu);
  EXPECT_EQ(v.e, -52);
}

TEST(FPTest, ConstructFromDouble) {
  test_construct_from_double<std::numeric_limits<double>::is_iec559>();
}

TEST(FPTest, Normalize) {
  const auto v = fp(0xbeef, 42);
  auto normalized = normalize(v);
  EXPECT_EQ(0xbeef000000000000, normalized.f);
  EXPECT_EQ(-6, normalized.e);
}

TEST(FPTest, ComputeBoundariesSubnormal) {
  auto v = fp(0xbeef, 42);
  fp lower, upper;
  v.compute_boundaries(lower, upper);
  EXPECT_EQ(0xbeee800000000000, lower.f);
  EXPECT_EQ(-6, lower.e);
  EXPECT_EQ(0xbeef800000000000, upper.f);
  EXPECT_EQ(-6, upper.e);
}

TEST(FPTest, ComputeBoundaries) {
  auto v = fp(0x10000000000000, 42);
  fp lower, upper;
  v.compute_boundaries(lower, upper);
  EXPECT_EQ(0x7ffffffffffffe00, lower.f);
  EXPECT_EQ(31, lower.e);
  EXPECT_EQ(0x8000000000000400, upper.f);
  EXPECT_EQ(31, upper.e);
}

TEST(FPTest, Subtract) {
  auto v = fp(123, 1) - fp(102, 1);
  EXPECT_EQ(v.f, 21u);
  EXPECT_EQ(v.e, 1);
}

TEST(FPTest, Multiply) {
  auto v = fp(123ULL << 32, 4) * fp(56ULL << 32, 7);
  EXPECT_EQ(v.f, 123u * 56u);
  EXPECT_EQ(v.e, 4 + 7 + 64);
  v = fp(123ULL << 32, 4) * fp(567ULL << 31, 8);
  EXPECT_EQ(v.f, (123 * 567 + 1u) / 2);
  EXPECT_EQ(v.e, 4 + 8 + 64);
}

TEST(FPTest, GetCachedPower) {
  typedef std::numeric_limits<double> limits;
  for (auto exp = limits::min_exponent; exp <= limits::max_exponent; ++exp) {
    int dec_exp = 0;
    auto fp = fmt::internal::get_cached_power(exp, dec_exp);
    EXPECT_LE(exp, fp.e);
    int dec_exp_step = 8;
    EXPECT_LE(fp.e, exp + dec_exp_step * log2(10));
    EXPECT_DOUBLE_EQ(pow(10, dec_exp), ldexp(static_cast<double>(fp.f), fp.e));
  }
}

TEST(FPTest, GetRoundDirection) {
  using fmt::internal::get_round_direction;
  EXPECT_EQ(fmt::internal::down, get_round_direction(100, 50, 0));
  EXPECT_EQ(fmt::internal::up, get_round_direction(100, 51, 0));
  EXPECT_EQ(fmt::internal::down, get_round_direction(100, 40, 10));
  EXPECT_EQ(fmt::internal::up, get_round_direction(100, 60, 10));
  for (int i = 41; i < 60; ++i)
    EXPECT_EQ(fmt::internal::unknown, get_round_direction(100, i, 10));
  uint64_t max = max_value<uint64_t>();
  EXPECT_THROW(get_round_direction(100, 100, 0), assertion_failure);
  EXPECT_THROW(get_round_direction(100, 0, 100), assertion_failure);
  EXPECT_THROW(get_round_direction(100, 0, 50), assertion_failure);
  // Check that remainder + error doesn't overflow.
  EXPECT_EQ(fmt::internal::up, get_round_direction(max, max - 1, 2));
  // Check that 2 * (remainder + error) doesn't overflow.
  EXPECT_EQ(fmt::internal::unknown,
            get_round_direction(max, max / 2 + 1, max / 2));
  // Check that remainder - error doesn't overflow.
  EXPECT_EQ(fmt::internal::unknown, get_round_direction(100, 40, 41));
  // Check that 2 * (remainder - error) doesn't overflow.
  EXPECT_EQ(fmt::internal::up, get_round_direction(max, max - 1, 1));
}

TEST(FPTest, FixedHandler) {
  struct handler : fmt::internal::fixed_handler {
    char buffer[10];
    handler(int prec = 0) : fmt::internal::fixed_handler() {
      buf = buffer;
      precision = prec;
    }
  };
  int exp = 0;
  handler().on_digit('0', 100, 99, 0, exp, false);
  EXPECT_THROW(handler().on_digit('0', 100, 100, 0, exp, false),
               assertion_failure);
  namespace digits = fmt::internal::digits;
  EXPECT_EQ(handler(1).on_digit('0', 100, 10, 10, exp, false), digits::done);
  // Check that divisor - error doesn't overflow.
  EXPECT_EQ(handler(1).on_digit('0', 100, 10, 101, exp, false), digits::error);
  // Check that 2 * error doesn't overflow.
  uint64_t max = max_value<uint64_t>();
  EXPECT_EQ(handler(1).on_digit('0', max, 10, max - 1, exp, false),
            digits::error);
}

TEST(FPTest, GrisuFormatCompilesWithNonIEEEDouble) {
  fmt::memory_buffer buf;
  int exp = 0;
  grisu_format(4.2f, buf, -1, false, exp);
}

template <typename T> struct value_extractor {
  T operator()(T value) { return value; }

  template <typename U> FMT_NORETURN T operator()(U) {
    throw std::runtime_error(fmt::format("invalid type {}", typeid(U).name()));
  }

#ifdef __apple_build_version__
  // Apple Clang does not define typeid for __int128_t and __uint128_t.
  FMT_NORETURN T operator()(__int128_t) {
    throw std::runtime_error(fmt::format("invalid type {}", "__int128_t"));
  }

  FMT_NORETURN T operator()(__uint128_t) {
    throw std::runtime_error(fmt::format("invalid type {}", "__uint128_t"));
  }
#endif
};

TEST(FormatTest, ArgConverter) {
  long long value = max_value<long long>();
  auto arg = fmt::internal::make_arg<fmt::format_context>(value);
  fmt::visit_format_arg(
      fmt::internal::arg_converter<long long, fmt::format_context>(arg, 'd'),
      arg);
  EXPECT_EQ(value, fmt::visit_format_arg(value_extractor<long long>(), arg));
}

TEST(FormatTest, FormatNegativeNaN) {
  double nan = std::numeric_limits<double>::quiet_NaN();
  if (std::signbit(-nan))
    EXPECT_EQ("-nan", fmt::format("{}", -nan));
  else
    fmt::print("Warning: compiler doesn't handle negative NaN correctly");
}

TEST(FormatTest, StrError) {
  char* message = nullptr;
  char buffer[BUFFER_SIZE];
  EXPECT_ASSERT(fmt::internal::safe_strerror(EDOM, message = nullptr, 0),
                "invalid buffer");
  EXPECT_ASSERT(fmt::internal::safe_strerror(EDOM, message = buffer, 0),
                "invalid buffer");
  buffer[0] = 'x';
#if defined(_GNU_SOURCE) && !defined(__COVERITY__)
  // Use invalid error code to make sure that safe_strerror returns an error
  // message in the buffer rather than a pointer to a static string.
  int error_code = -1;
#else
  int error_code = EDOM;
#endif

  int result =
      fmt::internal::safe_strerror(error_code, message = buffer, BUFFER_SIZE);
  EXPECT_EQ(result, 0);
  std::size_t message_size = std::strlen(message);
  EXPECT_GE(BUFFER_SIZE - 1u, message_size);
  EXPECT_EQ(get_system_error(error_code), message);

  // safe_strerror never uses buffer on MinGW.
#if !defined(__MINGW32__) && !defined(__sun)
  result =
      fmt::internal::safe_strerror(error_code, message = buffer, message_size);
  EXPECT_EQ(ERANGE, result);
  result = fmt::internal::safe_strerror(error_code, message = buffer, 1);
  EXPECT_EQ(buffer, message);  // Message should point to buffer.
  EXPECT_EQ(ERANGE, result);
  EXPECT_STREQ("", message);
#endif
}

TEST(FormatTest, FormatErrorCode) {
  std::string msg = "error 42", sep = ": ";
  {
    fmt::memory_buffer buffer;
    format_to(buffer, "garbage");
    fmt::internal::format_error_code(buffer, 42, "test");
    EXPECT_EQ("test: " + msg, to_string(buffer));
  }
  {
    fmt::memory_buffer buffer;
    std::string prefix(fmt::inline_buffer_size - msg.size() - sep.size() + 1,
                       'x');
    fmt::internal::format_error_code(buffer, 42, prefix);
    EXPECT_EQ(msg, to_string(buffer));
  }
  int codes[] = {42, -1};
  for (std::size_t i = 0, n = sizeof(codes) / sizeof(*codes); i < n; ++i) {
    // Test maximum buffer size.
    msg = fmt::format("error {}", codes[i]);
    fmt::memory_buffer buffer;
    std::string prefix(fmt::inline_buffer_size - msg.size() - sep.size(), 'x');
    fmt::internal::format_error_code(buffer, codes[i], prefix);
    EXPECT_EQ(prefix + sep + msg, to_string(buffer));
    std::size_t size = fmt::inline_buffer_size;
    EXPECT_EQ(size, buffer.size());
    buffer.resize(0);
    // Test with a message that doesn't fit into the buffer.
    prefix += 'x';
    fmt::internal::format_error_code(buffer, codes[i], prefix);
    EXPECT_EQ(msg, to_string(buffer));
  }
}

TEST(FormatTest, CountCodePoints) {
  EXPECT_EQ(4, fmt::internal::count_code_points(fmt::u8string_view("????????")));
}

// Tests fmt::internal::count_digits for integer type Int.
template <typename Int> void test_count_digits() {
  for (Int i = 0; i < 10; ++i) EXPECT_EQ(1u, fmt::internal::count_digits(i));
  for (Int i = 1, n = 1, end = max_value<Int>() / 10; n <= end;
       ++i) {
    n *= 10;
    EXPECT_EQ(i, fmt::internal::count_digits(n - 1));
    EXPECT_EQ(i + 1, fmt::internal::count_digits(n));
  }
}

TEST(UtilTest, CountDigits) {
  test_count_digits<uint32_t>();
  test_count_digits<uint64_t>();
}

TEST(UtilTest, WriteUIntPtr) {
  fmt::memory_buffer buf;
  fmt::internal::writer writer(buf);
  writer.write_pointer(fmt::internal::bit_cast<fmt::internal::fallback_uintptr>(
                           reinterpret_cast<void*>(0xface)),
                       nullptr);
  EXPECT_EQ("0xface", to_string(buf));
}
