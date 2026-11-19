#ifndef APPSTORAGE_RTCREFLECT
#define APPSTORAGE_RTCREFLECT

#include <stddef.h>
#include <stdint.h>
#include <tuple>
#include <type_traits>
#include <utility>

namespace zw::esp8266::app::storage::detail {

// Expected behaviour:
// `type_salt<T>()` hashes T's recursive structure (and, under C++20+, member names) at
// compile time. Any change of compiler, -std (e.g. the planned gnu++20 upgrade), or type
// layout changes the salt and resets RTC memory ONCE on the next boot, losing only the
// pre-SNTP wall-clock checkpoint.

inline constexpr size_t kMaxReflFields = 8;

constexpr uint32_t fnv1a_32(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s != '\0'; ++s) {
    h ^= static_cast<uint8_t>(*s);
    h *= 16777619u;
  }
  return h;
}

constexpr uint32_t mix_u32(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

// Universal non-array probe used alongside empty-brace slot probing to avoid
// array brace elision and GCC scalar-brace errors.
struct ubiq_field {
  template <typename U, std::enable_if_t<!std::is_array_v<U>, int> = 0>
  constexpr operator U&() const;
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

// Empty-brace slot probing (0..kMaxReflFields+1): `{}` never brace-elides into
// array elements or nested aggregates, giving the exact direct field count for
// default-initializable aggregates.
template <typename T, size_t N, typename = void>
struct is_empty_brace_constructible : std::false_type {};

template <typename T>
struct is_empty_brace_constructible<T, 0, std::void_t<decltype(T{})>> : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 1, std::void_t<decltype(T{{}})>> : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 2, std::void_t<decltype(T{{}, {}})>> : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 3, std::void_t<decltype(T{{}, {}, {}})>>
    : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 4, std::void_t<decltype(T{{}, {}, {}, {}})>>
    : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 5, std::void_t<decltype(T{{}, {}, {}, {}, {}})>>
    : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 6, std::void_t<decltype(T{{}, {}, {}, {}, {}, {}})>>
    : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 7, std::void_t<decltype(T{{}, {}, {}, {}, {}, {}, {}})>>
    : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<T, 8, std::void_t<decltype(T{{}, {}, {}, {}, {}, {}, {}, {}})>>
    : std::true_type {};
template <typename T>
struct is_empty_brace_constructible<
    T, 9, std::void_t<decltype(T{{}, {}, {}, {}, {}, {}, {}, {}, {}})>> : std::true_type {};

// Flat `ubiq_field` probing fallback for aggregates whose members are not
// empty-brace initializable (e.g., trivially-copyable non-default-constructible types).
template <typename T, typename Seq, typename = void>
struct is_ubiq_constructible : std::false_type {};

template <typename T, size_t... Is>
struct is_ubiq_constructible<
    T, std::index_sequence<Is...>,
    std::void_t<decltype(T{(static_cast<void>(Is), ubiq_field{})...})>> : std::true_type {};

template <typename T, size_t N>
constexpr size_t field_count_empty() {
  if constexpr (is_empty_brace_constructible<T, N>::value) {
    return N;
  } else if constexpr (N > 0) {
    return field_count_empty<T, N - 1>();
  } else {
    return 0;
  }
}

template <typename T, size_t N>
constexpr size_t field_count_ubiq() {
  if constexpr (is_ubiq_constructible<T, std::make_index_sequence<N>>::value) {
    return N;
  } else if constexpr (N > 0) {
    return field_count_ubiq<T, N - 1>();
  } else {
    return 0;
  }
}

template <typename T>
constexpr size_t field_count() {
  static_assert(std::is_aggregate_v<T> && !std::is_array_v<T> && !std::is_union_v<T>,
                "field_count<T>() requires a non-array, non-union aggregate");
  if constexpr (is_empty_brace_constructible<T, 0>::value) {
    return field_count_empty<T, kMaxReflFields + 1>();
  } else {
    return field_count_ubiq<T, kMaxReflFields + 1>();
  }
}

#pragma GCC diagnostic pop

// Unevaluated structured-binding decomposition into tuple of decayed field pointers.
template <typename T>
auto field_ptr_tuple_decl(T&, std::integral_constant<size_t, 0>) -> std::tuple<>;

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 1>) {
  auto& [f0] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*>{};
}

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 2>) {
  auto& [f0, f1] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*, remove_cvref_t<decltype(f1)>*>{};
}

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 3>) {
  auto& [f0, f1, f2] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*, remove_cvref_t<decltype(f1)>*,
                    remove_cvref_t<decltype(f2)>*>{};
}

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 4>) {
  auto& [f0, f1, f2, f3] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*, remove_cvref_t<decltype(f1)>*,
                    remove_cvref_t<decltype(f2)>*, remove_cvref_t<decltype(f3)>*>{};
}

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 5>) {
  auto& [f0, f1, f2, f3, f4] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*, remove_cvref_t<decltype(f1)>*,
                    remove_cvref_t<decltype(f2)>*, remove_cvref_t<decltype(f3)>*,
                    remove_cvref_t<decltype(f4)>*>{};
}

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 6>) {
  auto& [f0, f1, f2, f3, f4, f5] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*, remove_cvref_t<decltype(f1)>*,
                    remove_cvref_t<decltype(f2)>*, remove_cvref_t<decltype(f3)>*,
                    remove_cvref_t<decltype(f4)>*, remove_cvref_t<decltype(f5)>*>{};
}

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 7>) {
  auto& [f0, f1, f2, f3, f4, f5, f6] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*, remove_cvref_t<decltype(f1)>*,
                    remove_cvref_t<decltype(f2)>*, remove_cvref_t<decltype(f3)>*,
                    remove_cvref_t<decltype(f4)>*, remove_cvref_t<decltype(f5)>*,
                    remove_cvref_t<decltype(f6)>*>{};
}

template <typename T>
auto field_ptr_tuple_decl(T& t, std::integral_constant<size_t, 8>) {
  auto& [f0, f1, f2, f3, f4, f5, f6, f7] = t;
  return std::tuple<remove_cvref_t<decltype(f0)>*, remove_cvref_t<decltype(f1)>*,
                    remove_cvref_t<decltype(f2)>*, remove_cvref_t<decltype(f3)>*,
                    remove_cvref_t<decltype(f4)>*, remove_cvref_t<decltype(f5)>*,
                    remove_cvref_t<decltype(f6)>*, remove_cvref_t<decltype(f7)>*>{};
}

#if defined(__cpp_nontype_template_args) && __cpp_nontype_template_args >= 201911L
template <typename T>
struct fake_wrapper {
  T value;
};

template <typename T>
extern const fake_wrapper<T> fake_instance;

template <auto Ptr>
constexpr uint32_t subobj_name_salt() {
  return fnv1a_32(__PRETTY_FUNCTION__);
}

template <typename T>
constexpr auto field_addr_tuple(const T&, std::integral_constant<size_t, 0>) {
  return std::tuple<>{};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 1>) {
  const auto& [f0] = t;
  return std::tuple{&f0};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 2>) {
  const auto& [f0, f1] = t;
  return std::tuple{&f0, &f1};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 3>) {
  const auto& [f0, f1, f2] = t;
  return std::tuple{&f0, &f1, &f2};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 4>) {
  const auto& [f0, f1, f2, f3] = t;
  return std::tuple{&f0, &f1, &f2, &f3};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 5>) {
  const auto& [f0, f1, f2, f3, f4] = t;
  return std::tuple{&f0, &f1, &f2, &f3, &f4};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 6>) {
  const auto& [f0, f1, f2, f3, f4, f5] = t;
  return std::tuple{&f0, &f1, &f2, &f3, &f4, &f5};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 7>) {
  const auto& [f0, f1, f2, f3, f4, f5, f6] = t;
  return std::tuple{&f0, &f1, &f2, &f3, &f4, &f5, &f6};
}
template <typename T>
constexpr auto field_addr_tuple(const T& t, std::integral_constant<size_t, 8>) {
  const auto& [f0, f1, f2, f3, f4, f5, f6, f7] = t;
  return std::tuple{&f0, &f1, &f2, &f3, &f4, &f5, &f6, &f7};
}

template <typename T, size_t N, size_t I>
constexpr uint32_t member_name_salt() {
  constexpr auto addrs = field_addr_tuple(fake_instance<T>.value, std::integral_constant<size_t, N>{});
  return subobj_name_salt<std::get<I>(addrs)>();
}
#endif

template <typename T>
constexpr uint32_t scalar_type_salt() {
  uint32_t h = 0x53434C32u;  // "SCL2"
  h = mix_u32(h, fnv1a_32(__PRETTY_FUNCTION__));
  h = mix_u32(h, sizeof(T));
  h = mix_u32(h, alignof(T));
  return h;
}

template <typename T>
constexpr uint32_t type_salt();

template <typename T, typename PtrTuple, size_t... Is>
constexpr uint32_t fold_aggregate_fields(uint32_t h, std::index_sequence<Is...>) {
  (
      [&] {
        using Field = std::remove_pointer_t<std::tuple_element_t<Is, PtrTuple>>;
        h = mix_u32(h, static_cast<uint32_t>(Is));
#if defined(__cpp_nontype_template_args) && __cpp_nontype_template_args >= 201911L
        h = mix_u32(h, member_name_salt<T, sizeof...(Is), Is>());
#endif
        h = mix_u32(h, type_salt<Field>());
      }(),
      ...);
  return h;
}

template <typename T>
constexpr uint32_t type_salt() {
  using U = remove_cvref_t<T>;
  static_assert(std::is_trivially_copyable_v<U>,
                "RTCData<T> requires a trivially copyable type");
  if constexpr (std::is_array_v<U>) {
    uint32_t h = 0x41525232u;  // "ARR2"
    h = mix_u32(h, static_cast<uint32_t>(std::extent_v<U>));
    h = mix_u32(h, type_salt<std::remove_extent_t<U>>());
    return h;
  } else if constexpr (std::is_aggregate_v<U> && !std::is_union_v<U>) {
    constexpr size_t N = field_count<U>();
    static_assert(N <= kMaxReflFields, "RTCData aggregate exceeds kMaxReflFields (8)");
    using PtrTuple = decltype(field_ptr_tuple_decl(std::declval<U&>(),
                                                   std::integral_constant<size_t, N>{}));
    uint32_t h = 0x41474732u;  // "AGG2"
    h = mix_u32(h, sizeof(U));
    h = mix_u32(h, alignof(U));
    h = mix_u32(h, static_cast<uint32_t>(N));
    return fold_aggregate_fields<U, PtrTuple>(h, std::make_index_sequence<N>{});
  } else {
    return scalar_type_salt<U>();
  }
}

}  // namespace zw::esp8266::app::storage::detail

#endif  // APPSTORAGE_RTCREFLECT
