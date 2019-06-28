/*
 Formatting library for C++

 Copyright (c) 2012 - present, Victor Zverovich
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 1. Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef FMT_FORMAT_H_
#define FMT_FORMAT_H_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>

#include "core.h"

#ifdef __clang__
#  define FMT_CLANG_VERSION (__clang_major__ * 100 + __clang_minor__)
#else
#  define FMT_CLANG_VERSION 0
#endif

#ifdef __INTEL_COMPILER
#  define FMT_ICC_VERSION __INTEL_COMPILER
#elif defined(__ICL)
#  define FMT_ICC_VERSION __ICL
#else
#  define FMT_ICC_VERSION 0
#endif

#ifdef __NVCC__
#  define FMT_CUDA_VERSION (__CUDACC_VER_MAJOR__ * 100 + __CUDACC_VER_MINOR__)
#else
#  define FMT_CUDA_VERSION 0
#endif

#ifdef __has_builtin
#  define FMT_HAS_BUILTIN(x) __has_builtin(x)
#else
#  define FMT_HAS_BUILTIN(x) 0
#endif

#ifndef FMT_THROW
#  if FMT_EXCEPTIONS
#    if FMT_MSC_VER
FMT_BEGIN_NAMESPACE
namespace internal {
template <typename Exception> inline void do_throw(const Exception& x) {
  // Silence unreachable code warnings in MSVC because these are nearly
  // impossible to fix in a generic code.
  volatile bool b = true;
  if (b) throw x;
}
}  // namespace internal
FMT_END_NAMESPACE
#      define FMT_THROW(x) fmt::internal::do_throw(x)
#    else
#      define FMT_THROW(x) throw x
#    endif
#  else
#    define FMT_THROW(x)              \
      do {                            \
        static_cast<void>(sizeof(x)); \
        assert(false);                \
      } while (false)
#  endif
#endif

#ifndef FMT_USE_USER_DEFINED_LITERALS
// For Intel and NVIDIA compilers both they and the system gcc/msc support UDLs.
#  if (FMT_HAS_FEATURE(cxx_user_literals) || FMT_GCC_VERSION >= 407 ||      \
       FMT_MSC_VER >= 1900) &&                                              \
      (!(FMT_ICC_VERSION || FMT_CUDA_VERSION) || FMT_ICC_VERSION >= 1500 || \
       FMT_CUDA_VERSION >= 700)
#    define FMT_USE_USER_DEFINED_LITERALS 1
#  else
#    define FMT_USE_USER_DEFINED_LITERALS 0
#  endif
#endif

#ifndef FMT_USE_UDL_TEMPLATE
// EDG front end based compilers (icc, nvcc) do not support UDL templates yet
// and GCC 9 warns about them.
#  if FMT_USE_USER_DEFINED_LITERALS && FMT_ICC_VERSION == 0 && \
      FMT_CUDA_VERSION == 0 &&                                 \
      ((FMT_GCC_VERSION >= 600 && FMT_GCC_VERSION <= 900 &&    \
        __cplusplus >= 201402L) ||                             \
       FMT_CLANG_VERSION >= 304)
#    define FMT_USE_UDL_TEMPLATE 1
#  else
#    define FMT_USE_UDL_TEMPLATE 0
#  endif
#endif

#ifdef FMT_USE_INT128
// Do nothing.
#elif defined(__SIZEOF_INT128__)
#  define FMT_USE_INT128 1
#else
#  define FMT_USE_INT128 0
#endif

// __builtin_clz is broken in clang with Microsoft CodeGen:
// https://github.com/fmtlib/fmt/issues/519
#if (FMT_GCC_VERSION || FMT_HAS_BUILTIN(__builtin_clz)) && !FMT_MSC_VER
#  define FMT_BUILTIN_CLZ(n) __builtin_clz(n)
#endif
#if (FMT_GCC_VERSION || FMT_HAS_BUILTIN(__builtin_clzll)) && !FMT_MSC_VER
#  define FMT_BUILTIN_CLZLL(n) __builtin_clzll(n)
#endif

// Some compilers masquerade as both MSVC and GCC-likes or otherwise support
// __builtin_clz and __builtin_clzll, so only define FMT_BUILTIN_CLZ using the
// MSVC intrinsics if the clz and clzll builtins are not available.
#if FMT_MSC_VER && !defined(FMT_BUILTIN_CLZLL) && !defined(_MANAGED)
#  include <intrin.h>  // _BitScanReverse, _BitScanReverse64

FMT_BEGIN_NAMESPACE
namespace internal {
// Avoid Clang with Microsoft CodeGen's -Wunknown-pragmas warning.
#  ifndef __clang__
#    pragma intrinsic(_BitScanReverse)
#  endif
inline uint32_t clz(uint32_t x) {
  unsigned long r = 0;
  _BitScanReverse(&r, x);

  assert(x != 0);
  // Static analysis complains about using uninitialized data
  // "r", but the only way that can happen is if "x" is 0,
  // which the callers guarantee to not happen.
#  pragma warning(suppress : 6102)
  return 31 - r;
}
#  define FMT_BUILTIN_CLZ(n) fmt::internal::clz(n)

#  if defined(_WIN64) && !defined(__clang__)
#    pragma intrinsic(_BitScanReverse64)
#  endif

inline uint32_t clzll(uint64_t x) {
  unsigned long r = 0;
#  ifdef _WIN64
  _BitScanReverse64(&r, x);
#  else
  // Scan the high 32 bits.
  if (_BitScanReverse(&r, static_cast<uint32_t>(x >> 32))) return 63 - (r + 32);

  // Scan the low 32 bits.
  _BitScanReverse(&r, static_cast<uint32_t>(x));
#  endif

  assert(x != 0);
  // Static analysis complains about using uninitialized data
  // "r", but the only way that can happen is if "x" is 0,
  // which the callers guarantee to not happen.
#  pragma warning(suppress : 6102)
  return 63 - r;
}
#  define FMT_BUILTIN_CLZLL(n) fmt::internal::clzll(n)
}  // namespace internal
FMT_END_NAMESPACE
#endif

FMT_BEGIN_NAMESPACE
namespace internal {

// A fallback implementation of uintptr_t for systems that lack it.
struct fallback_uintptr {
  unsigned char value[sizeof(void*)];
};
#ifdef UINTPTR_MAX
using uintptr_t = ::uintptr_t;
#else
using uintptr_t = fallback_uintptr;
#endif

// An equivalent of `*reinterpret_cast<Dest*>(&source)` that doesn't produce
// undefined behavior (e.g. due to type aliasing).
// Example: uint64_t d = bit_cast<uint64_t>(2.718);
template <typename Dest, typename Source>
inline Dest bit_cast(const Source& source) {
  static_assert(sizeof(Dest) == sizeof(Source), "size mismatch");
  Dest dest;
  std::memcpy(&dest, &source, sizeof(dest));
  return dest;
}

// An implementation of iterator_t for pre-C++20 systems.
template <typename T>
using iterator_t = decltype(std::begin(std::declval<T&>()));

#ifndef FMT_USE_GRISU
#  define FMT_USE_GRISU 1
#endif

template <typename T> inline bool use_grisu() {
  return FMT_USE_GRISU && std::numeric_limits<double>::is_iec559 &&
         sizeof(T) <= sizeof(double);
}

// A range with an iterator appending to a buffer.
template <typename T> class buffer_range {
 public:
  using value_type = T;
  using iterator = std::back_insert_iterator<buffer<T>>;

 private:
  iterator begin_;

 public:
  buffer_range(buffer<T>& buf) : begin_(std::back_inserter(buf)) {}
  explicit buffer_range(iterator it) : begin_(it) {}
  iterator begin() const { return begin_; }
};

// A range with the specified output iterator and value type.
template <typename OutputIt, typename T = typename OutputIt::value_type>
class output_range {
 private:
  OutputIt it_;

 public:
  using value_type = T;
  using iterator = OutputIt;

  explicit output_range(OutputIt it) : it_(it) {}
  OutputIt begin() const { return it_; }
};

#ifdef _SECURE_SCL
// Make a checked iterator to avoid MSVC warnings.
template <typename T> using checked_ptr = stdext::checked_array_iterator<T*>;
template <typename T> checked_ptr<T> make_checked(T* p, std::size_t size) {
  return {p, size};
}
#else
template <typename T> using checked_ptr = T*;
template <typename T> inline T* make_checked(T* p, std::size_t) { return p; }
#endif

template <typename T>
template <typename U>
void buffer<T>::append(const U* begin, const U* end) {
  std::size_t new_size = size_ + to_unsigned(end - begin);
  reserve(new_size);
  std::uninitialized_copy(begin, end, make_checked(ptr_, capacity_) + size_);
  size_ = new_size;
}
}  // namespace internal

// A UTF-8 string view.
class u8string_view : public basic_string_view<char8_t> {
 public:
  u8string_view(const char* s)
      : basic_string_view<char8_t>(reinterpret_cast<const char8_t*>(s)) {}
  u8string_view(const char* s, size_t count) FMT_NOEXCEPT
      : basic_string_view<char8_t>(reinterpret_cast<const char8_t*>(s), count) {
  }
};

#if FMT_USE_USER_DEFINED_LITERALS
inline namespace literals {
inline u8string_view operator"" _u(const char* s, std::size_t n) {
  return {s, n};
}
}  // namespace literals
#endif

// The number of characters to store in the basic_memory_buffer object itself
// to avoid dynamic memory allocation.
enum { inline_buffer_size = 500 };

/**
  \rst
  A dynamically growing memory buffer for trivially copyable/constructible types
  with the first ``SIZE`` elements stored in the object itself.

  You can use one of the following typedefs for common character types:

  +----------------+------------------------------+
  | Type           | Definition                   |
  +================+==============================+
  | memory_buffer  | basic_memory_buffer<char>    |
  +----------------+------------------------------+
  | wmemory_buffer | basic_memory_buffer<wchar_t> |
  +----------------+------------------------------+

  **Example**::

     fmt::memory_buffer out;
     format_to(out, "The answer is {}.", 42);

  This will append the following output to the ``out`` object:

  .. code-block:: none

     The answer is 42.

  The output can be converted to an ``std::string`` with ``to_string(out)``.
  \endrst
 */
template <typename T, std::size_t SIZE = inline_buffer_size,
          typename Allocator = std::allocator<T>>
class basic_memory_buffer : private Allocator, public internal::buffer<T> {
 private:
  T store_[SIZE];

  // Deallocate memory allocated by the buffer.
  void deallocate() {
    T* data = this->data();
    if (data != store_) Allocator::deallocate(data, this->capacity());
  }

 protected:
  void grow(std::size_t size) FMT_OVERRIDE;

 public:
  typedef T value_type;
  typedef const T& const_reference;

  explicit basic_memory_buffer(const Allocator& alloc = Allocator())
      : Allocator(alloc) {
    this->set(store_, SIZE);
  }
  ~basic_memory_buffer() { deallocate(); }

 private:
  // Move data from other to this buffer.
  void move(basic_memory_buffer& other) {
    Allocator &this_alloc = *this, &other_alloc = other;
    this_alloc = std::move(other_alloc);
    T* data = other.data();
    std::size_t size = other.size(), capacity = other.capacity();
    if (data == other.store_) {
      this->set(store_, capacity);
      std::uninitialized_copy(other.store_, other.store_ + size,
                              internal::make_checked(store_, capacity));
    } else {
      this->set(data, capacity);
      // Set pointer to the inline array so that delete is not called
      // when deallocating.
      other.set(other.store_, 0);
    }
    this->resize(size);
  }

 public:
  /**
    \rst
    Constructs a :class:`fmt::basic_memory_buffer` object moving the content
    of the other object to it.
    \endrst
   */
  basic_memory_buffer(basic_memory_buffer&& other) { move(other); }

  /**
    \rst
    Moves the content of the other ``basic_memory_buffer`` object to this one.
    \endrst
   */
  basic_memory_buffer& operator=(basic_memory_buffer&& other) {
    assert(this != &other);
    deallocate();
    move(other);
    return *this;
  }

  // Returns a copy of the allocator associated with this buffer.
  Allocator get_allocator() const { return *this; }
};

template <typename T, std::size_t SIZE, typename Allocator>
void basic_memory_buffer<T, SIZE, Allocator>::grow(std::size_t size) {
  std::size_t old_capacity = this->capacity();
  std::size_t new_capacity = old_capacity + old_capacity / 2;
  if (size > new_capacity) new_capacity = size;
  T* old_data = this->data();
  T* new_data = std::allocator_traits<Allocator>::allocate(*this, new_capacity);
  // The following code doesn't throw, so the raw pointer above doesn't leak.
  std::uninitialized_copy(old_data, old_data + this->size(),
                          internal::make_checked(new_data, new_capacity));
  this->set(new_data, new_capacity);
  // deallocate must not throw according to the standard, but even if it does,
  // the buffer already uses the new storage and will deallocate it in
  // destructor.
  if (old_data != store_) Allocator::deallocate(old_data, old_capacity);
}

typedef basic_memory_buffer<char> memory_buffer;
typedef basic_memory_buffer<wchar_t> wmemory_buffer;

/** A formatting error such as invalid format string. */
class FMT_API format_error : public std::runtime_error {
 public:
  explicit format_error(const char* message) : std::runtime_error(message) {}
  explicit format_error(const std::string& message)
      : std::runtime_error(message) {}
  ~format_error() FMT_NOEXCEPT;
};

template <typename Range> class basic_writer;
using writer = basic_writer<internal::buffer_range<char>>;
using wwriter = basic_writer<internal::buffer_range<wchar_t>>;

namespace internal {

// A workaround for std::string not having mutable data() until C++17.
template <typename Char> inline Char* get_data(std::basic_string<Char>& s) {
  return &s[0];
}
template <typename Container>
inline typename Container::value_type* get_data(Container& c) {
  return c.data();
}

template <typename Container, FMT_ENABLE_IF(is_contiguous<Container>::value)>
inline checked_ptr<typename Container::value_type> reserve(
    std::back_insert_iterator<Container>& it, std::size_t n) {
  Container& c = internal::get_container(it);
  std::size_t size = c.size();
  c.resize(size + n);
  return make_checked(get_data(c) + size, n);
}

template <typename Iterator>
inline Iterator& reserve(Iterator& it, std::size_t) {
  return it;
}

template <typename Char> class null_terminating_iterator;

template <typename Char>
FMT_CONSTEXPR_DECL const Char* pointer_from(null_terminating_iterator<Char> it);

// An output iterator that counts the number of objects written to it and
// discards them.
template <typename T> class counting_iterator {
 private:
  std::size_t count_;
  mutable T blackhole_;

 public:
  typedef std::output_iterator_tag iterator_category;
  typedef T value_type;
  typedef std::ptrdiff_t difference_type;
  typedef T* pointer;
  typedef T& reference;
  typedef counting_iterator _Unchecked_type;  // Mark iterator as checked.

  counting_iterator() : count_(0) {}

  std::size_t count() const { return count_; }

  counting_iterator& operator++() {
    ++count_;
    return *this;
  }

  counting_iterator operator++(int) {
    auto it = *this;
    ++*this;
    return it;
  }

  T& operator*() const { return blackhole_; }
};

template <typename OutputIt> class truncating_iterator_base {
 protected:
  OutputIt out_;
  std::size_t limit_;
  std::size_t count_;

  truncating_iterator_base(OutputIt out, std::size_t limit)
      : out_(out), limit_(limit), count_(0) {}

 public:
  typedef std::output_iterator_tag iterator_category;
  typedef void difference_type;
  typedef void pointer;
  typedef void reference;
  typedef truncating_iterator_base
      _Unchecked_type;  // Mark iterator as checked.

  OutputIt base() const { return out_; }
  std::size_t count() const { return count_; }
};

// An output iterator that truncates the output and counts the number of objects
// written to it.
template <typename OutputIt,
          typename Enable = typename std::is_void<
              typename std::iterator_traits<OutputIt>::value_type>::type>
class truncating_iterator;

template <typename OutputIt>
class truncating_iterator<OutputIt, std::false_type>
    : public truncating_iterator_base<OutputIt> {
  typedef std::iterator_traits<OutputIt> traits;

  mutable typename traits::value_type blackhole_;

 public:
  typedef typename traits::value_type value_type;

  truncating_iterator(OutputIt out, std::size_t limit)
      : truncating_iterator_base<OutputIt>(out, limit) {}

  truncating_iterator& operator++() {
    if (this->count_++ < this->limit_) ++this->out_;
    return *this;
  }

  truncating_iterator operator++(int) {
    auto it = *this;
    ++*this;
    return it;
  }

  value_type& operator*() const {
    return this->count_ < this->limit_ ? *this->out_ : blackhole_;
  }
};

template <typename OutputIt>
class truncating_iterator<OutputIt, std::true_type>
    : public truncating_iterator_base<OutputIt> {
 public:
  typedef typename OutputIt::container_type::value_type value_type;

  truncating_iterator(OutputIt out, std::size_t limit)
      : truncating_iterator_base<OutputIt>(out, limit) {}

  truncating_iterator& operator=(value_type val) {
    if (this->count_++ < this->limit_) this->out_ = val;
    return *this;
  }

  truncating_iterator& operator++() { return *this; }
  truncating_iterator& operator++(int) { return *this; }
  truncating_iterator& operator*() { return *this; }
};

// Returns true if value is negative, false otherwise.
// Same as (value < 0) but doesn't produce warnings if T is an unsigned type.
template <typename T, FMT_ENABLE_IF(std::numeric_limits<T>::is_signed)>
FMT_CONSTEXPR bool is_negative(T value) {
  return value < 0;
}
template <typename T, FMT_ENABLE_IF(!std::numeric_limits<T>::is_signed)>
FMT_CONSTEXPR bool is_negative(T) {
  return false;
}

template <typename T> struct int_traits {
  // Smallest of uint32_t and uint64_t that is large enough to represent
  // all values of T.
  using main_type =
      conditional_t<std::numeric_limits<T>::digits <= 32, uint32_t, uint64_t>;
};

// Static data is placed in this class template to allow header-only
// configuration.
template <typename T = void> struct FMT_EXTERN_TEMPLATE_API basic_data {
  static const uint64_t POWERS_OF_10_64[];
  static const uint32_t ZERO_OR_POWERS_OF_10_32[];
  static const uint64_t ZERO_OR_POWERS_OF_10_64[];
  static const uint64_t POW10_SIGNIFICANDS[];
  static const int16_t POW10_EXPONENTS[];
  static const char DIGITS[];
  static const char HEX_DIGITS[];
  static const char FOREGROUND_COLOR[];
  static const char BACKGROUND_COLOR[];
  static const char RESET_COLOR[5];
  static const wchar_t WRESET_COLOR[5];
};

FMT_EXTERN template struct basic_data<void>;

// This is a struct rather than a typedef to avoid shadowing warnings in gcc.
struct data : basic_data<> {};

#ifdef FMT_BUILTIN_CLZLL
// Returns the number of decimal digits in n. Leading zeros are not counted
// except for n == 0 in which case count_digits returns 1.
inline int count_digits(uint64_t n) {
  // Based on http://graphics.stanford.edu/~seander/bithacks.html#IntegerLog10
  // and the benchmark https://github.com/localvoid/cxx-benchmark-count-digits.
  int t = (64 - FMT_BUILTIN_CLZLL(n | 1)) * 1233 >> 12;
  return t - (n < data::ZERO_OR_POWERS_OF_10_64[t]) + 1;
}
#else
// Fallback version of count_digits used when __builtin_clz is not available.
inline int count_digits(uint64_t n) {
  int count = 1;
  for (;;) {
    // Integer division is slow so do it for a group of four digits instead
    // of for every digit. The idea comes from the talk by Alexandrescu
    // "Three Optimization Tips for C++". See speed-test for a comparison.
    if (n < 10) return count;
    if (n < 100) return count + 1;
    if (n < 1000) return count + 2;
    if (n < 10000) return count + 3;
    n /= 10000u;
    count += 4;
  }
}
#endif

// Counts the number of digits in n. BITS = log2(radix).
template <unsigned BITS, typename UInt> inline int count_digits(UInt n) {
  int num_digits = 0;
  do {
    ++num_digits;
  } while ((n >>= BITS) != 0);
  return num_digits;
}

template <> int count_digits<4>(internal::fallback_uintptr n);

template <typename Char>
inline size_t count_code_points(basic_string_view<Char> s) {
  return s.size();
}

// Counts the number of code points in a UTF-8 string.
inline size_t count_code_points(basic_string_view<char8_t> s) {
  const char8_t* data = s.data();
  size_t num_code_points = 0;
  for (size_t i = 0, size = s.size(); i != size; ++i) {
    if ((data[i] & 0xc0) != 0x80) ++num_code_points;
  }
  return num_code_points;
}

inline char8_t to_char8_t(char c) { return static_cast<char8_t>(c); }

template <typename InputIt, typename OutChar>
struct needs_conversion
    : bool_constant<
          std::is_same<typename std::iterator_traits<InputIt>::value_type,
                       char>::value &&
          std::is_same<OutChar, char8_t>::value> {};

template <typename OutChar, typename InputIt, typename OutputIt,
          FMT_ENABLE_IF(!needs_conversion<InputIt, OutChar>::value)>
OutputIt copy_str(InputIt begin, InputIt end, OutputIt it) {
  return std::copy(begin, end, it);
}

template <typename OutChar, typename InputIt, typename OutputIt,
          FMT_ENABLE_IF(needs_conversion<InputIt, OutChar>::value)>
OutputIt copy_str(InputIt begin, InputIt end, OutputIt it) {
  return std::transform(begin, end, it, to_char8_t);
}

#if FMT_HAS_CPP_ATTRIBUTE(always_inline)
#  define FMT_ALWAYS_INLINE __attribute__((always_inline))
#else
#  define FMT_ALWAYS_INLINE
#endif

template <typename Handler>
inline char* lg(uint32_t n, Handler h) FMT_ALWAYS_INLINE;

// Computes g = floor(log10(n)) and calls h.on<g>(n);
template <typename Handler> inline char* lg(uint32_t n, Handler h) {
  return n < 100 ? n < 10 ? h.template on<0>(n) : h.template on<1>(n)
                 : n < 1000000
                       ? n < 10000 ? n < 1000 ? h.template on<2>(n)
                                              : h.template on<3>(n)
                                   : n < 100000 ? h.template on<4>(n)
                                                : h.template on<5>(n)
                       : n < 100000000 ? n < 10000000 ? h.template on<6>(n)
                                                      : h.template on<7>(n)
                                       : n < 1000000000 ? h.template on<8>(n)
                                                        : h.template on<9>(n);
}

// An lg handler that formats a decimal number.
// Usage: lg(n, decimal_formatter(buffer));
class decimal_formatter {
 private:
  char* buffer_;

  void write_pair(unsigned N, uint32_t index) {
    std::memcpy(buffer_ + N, data::DIGITS + index * 2, 2);
  }

 public:
  explicit decimal_formatter(char* buf) : buffer_(buf) {}

  template <unsigned N> char* on(uint32_t u) {
    if (N == 0) {
      *buffer_ = static_cast<char>(u) + '0';
    } else if (N == 1) {
      write_pair(0, u);
    } else {
      // The idea of using 4.32 fixed-point numbers is based on
      // https://github.com/jeaiii/itoa
      unsigned n = N - 1;
      unsigned a = n / 5 * n * 53 / 16;
      uint64_t t =
          ((1ULL << (32 + a)) / data::ZERO_OR_POWERS_OF_10_32[n] + 1 - n / 9);
      t = ((t * u) >> a) + n / 5 * 4;
      write_pair(0, t >> 32);
      for (unsigned i = 2; i < N; i += 2) {
        t = 100ULL * static_cast<uint32_t>(t);
        write_pair(i, t >> 32);
      }
      if (N % 2 == 0) {
        buffer_[N] =
            static_cast<char>((10ULL * static_cast<uint32_t>(t)) >> 32) + '0';
      }
    }
    return buffer_ += N + 1;
  }
};

// An lg handler that formats a decimal number with a terminating null.
class decimal_formatter_null : public decimal_formatter {
 public:
  explicit decimal_formatter_null(char* buf) : decimal_formatter(buf) {}

  template <unsigned N> char* on(uint32_t u) {
    char* buf = decimal_formatter::on<N>(u);
    *buf = '\0';
    return buf;
  }
};

#ifdef FMT_BUILTIN_CLZ
// Optional version of count_digits for better performance on 32-bit platforms.
inline int count_digits(uint32_t n) {
  int t = (32 - FMT_BUILTIN_CLZ(n | 1)) * 1233 >> 12;
  return t - (n < data::ZERO_OR_POWERS_OF_10_32[t]) + 1;
}
#endif

// A functor that doesn't add a thousands separator.
struct no_thousands_sep {
  typedef char char_type;

  template <typename Char> void operator()(Char*) {}

  enum { size = 0 };
};

// A functor that adds a thousands separator.
template <typename Char> class add_thousands_sep {
 private:
  basic_string_view<Char> sep_;

  // Index of a decimal digit with the least significant digit having index 0.
  unsigned digit_index_;

 public:
  typedef Char char_type;

  explicit add_thousands_sep(basic_string_view<Char> sep)
      : sep_(sep), digit_index_(0) {}

  void operator()(Char*& buffer) {
    if (++digit_index_ % 3 != 0) return;
    buffer -= sep_.size();
    std::uninitialized_copy(sep_.data(), sep_.data() + sep_.size(),
                            internal::make_checked(buffer, sep_.size()));
  }

  enum { size = 1 };
};

template <typename Char> FMT_API Char thousands_sep_impl(locale_ref loc);

template <typename Char> inline Char thousands_sep(locale_ref loc) {
  return Char(thousands_sep_impl<char>(loc));
}

template <> inline wchar_t thousands_sep(locale_ref loc) {
  return thousands_sep_impl<wchar_t>(loc);
}

// Formats a decimal unsigned integer value writing into buffer.
// thousands_sep is a functor that is called after writing each char to
// add a thousands separator if necessary.
template <typename UInt, typename Char, typename ThousandsSep>
inline Char* format_decimal(Char* buffer, UInt value, int num_digits,
                            ThousandsSep thousands_sep) {
  FMT_ASSERT(num_digits >= 0, "invalid digit count");
  buffer += num_digits;
  Char* end = buffer;
  while (value >= 100) {
    // Integer division is slow so do it for a group of two digits instead
    // of for every digit. The idea comes from the talk by Alexandrescu
    // "Three Optimization Tips for C++". See speed-test for a comparison.
    unsigned index = static_cast<unsigned>((value % 100) * 2);
    value /= 100;
    *--buffer = static_cast<Char>(data::DIGITS[index + 1]);
    thousands_sep(buffer);
    *--buffer = static_cast<Char>(data::DIGITS[index]);
    thousands_sep(buffer);
  }
  if (value < 10) {
    *--buffer = static_cast<Char>('0' + value);
    return end;
  }
  unsigned index = static_cast<unsigned>(value * 2);
  *--buffer = static_cast<Char>(data::DIGITS[index + 1]);
  thousands_sep(buffer);
  *--buffer = static_cast<Char>(data::DIGITS[index]);
  return end;
}

template <typename OutChar, typename UInt, typename Iterator,
          typename ThousandsSep>
inline Iterator format_decimal(Iterator out, UInt value, int num_digits,
                               ThousandsSep sep) {
  FMT_ASSERT(num_digits >= 0, "invalid digit count");
  typedef typename ThousandsSep::char_type char_type;
  // Buffer should be large enough to hold all digits (<= digits10 + 1).
  enum { max_size = std::numeric_limits<UInt>::digits10 + 1 };
  FMT_ASSERT(ThousandsSep::size <= 1, "invalid separator");
  char_type buffer[max_size + max_size / 3];
  auto end = format_decimal(buffer, value, num_digits, sep);
  return internal::copy_str<OutChar>(buffer, end, out);
}

template <typename OutChar, typename It, typename UInt>
inline It format_decimal(It out, UInt value, int num_digits) {
  return format_decimal<OutChar>(out, value, num_digits, no_thousands_sep());
}

template <unsigned BASE_BITS, typename Char, typename UInt>
inline Char* format_uint(Char* buffer, UInt value, int num_digits,
                         bool upper = false) {
  buffer += num_digits;
  Char* end = buffer;
  do {
    const char* digits = upper ? "0123456789ABCDEF" : data::HEX_DIGITS;
    unsigned digit = (value & ((1 << BASE_BITS) - 1));
    *--buffer = static_cast<Char>(BASE_BITS < 4 ? static_cast<char>('0' + digit)
                                                : digits[digit]);
  } while ((value >>= BASE_BITS) != 0);
  return end;
}

template <unsigned BASE_BITS, typename Char>
Char* format_uint(Char* buffer, internal::fallback_uintptr n, int num_digits,
                  bool = false) {
  auto char_digits = std::numeric_limits<unsigned char>::digits / 4;
  int start = (num_digits + char_digits - 1) / char_digits - 1;
  if (int start_digits = num_digits % char_digits) {
    unsigned value = n.value[start--];
    buffer = format_uint<BASE_BITS>(buffer, value, start_digits);
  }
  for (; start >= 0; --start) {
    unsigned value = n.value[start];
    buffer += char_digits;
    auto p = buffer;
    for (int i = 0; i < char_digits; ++i) {
      unsigned digit = (value & ((1 << BASE_BITS) - 1));
      *--p = static_cast<Char>(data::HEX_DIGITS[digit]);
      value >>= BASE_BITS;
    }
  }
  return buffer;
}

template <unsigned BASE_BITS, typename Char, typename It, typename UInt>
inline It format_uint(It out, UInt value, int num_digits, bool upper = false) {
  // Buffer should be large enough to hold all digits (digits / BASE_BITS + 1)
  // and null.
  char buffer[std::numeric_limits<UInt>::digits / BASE_BITS + 2];
  format_uint<BASE_BITS>(buffer, value, num_digits, upper);
  return internal::copy_str<Char>(buffer, buffer + num_digits, out);
}

#ifndef _WIN32
#  define FMT_USE_WINDOWS_H 0
#elif !defined(FMT_USE_WINDOWS_H)
#  define FMT_USE_WINDOWS_H 1
#endif

// Define FMT_USE_WINDOWS_H to 0 to disable use of windows.h.
// All the functionality that relies on it will be disabled too.
#if FMT_USE_WINDOWS_H
// A converter from UTF-8 to UTF-16.
// It is only provided for Windows since other systems support UTF-8 natively.
class utf8_to_utf16 {
 private:
  wmemory_buffer buffer_;

 public:
  FMT_API explicit utf8_to_utf16(string_view s);
  operator wstring_view() const { return wstring_view(&buffer_[0], size()); }
  size_t size() const { return buffer_.size() - 1; }
  const wchar_t* c_str() const { return &buffer_[0]; }
  std::wstring str() const { return std::wstring(&buffer_[0], size()); }
};

// A converter from UTF-16 to UTF-8.
// It is only provided for Windows since other systems support UTF-8 natively.
class utf16_to_utf8 {
 private:
  memory_buffer buffer_;

 public:
  utf16_to_utf8() {}
  FMT_API explicit utf16_to_utf8(wstring_view s);
  operator string_view() const { return string_view(&buffer_[0], size()); }
  size_t size() const { return buffer_.size() - 1; }
  const char* c_str() const { return &buffer_[0]; }
  std::string str() const { return std::string(&buffer_[0], size()); }

  // Performs conversion returning a system error code instead of
  // throwing exception on conversion error. This method may still throw
  // in case of memory allocation error.
  FMT_API int convert(wstring_view s);
};

FMT_API void format_windows_error(fmt::internal::buffer<char>& out,
                                  int error_code,
                                  fmt::string_view message) FMT_NOEXCEPT;
#endif

template <typename T = void> struct null {};
}  // namespace internal

enum alignment {
  ALIGN_DEFAULT,
  ALIGN_LEFT,
  ALIGN_RIGHT,
  ALIGN_CENTER,
  ALIGN_NUMERIC
};

// Flags.
enum { SIGN_FLAG = 1, PLUS_FLAG = 2, MINUS_FLAG = 4, HASH_FLAG = 8 };

// An alignment specifier.
struct align_spec {
  unsigned width_;
  // Fill is always wchar_t and cast to char if necessary to avoid having
  // two specialization of AlignSpec and its subclasses.
  wchar_t fill_;
  alignment align_;

  FMT_CONSTEXPR align_spec() : width_(0), fill_(' '), align_(ALIGN_DEFAULT) {}
  FMT_CONSTEXPR unsigned width() const { return width_; }
  FMT_CONSTEXPR wchar_t fill() const { return fill_; }
  FMT_CONSTEXPR alignment align() const { return align_; }
};

struct core_format_specs {
  int precision;
  uint_least8_t flags;
  char type;

  FMT_CONSTEXPR core_format_specs() : precision(-1), flags(0), type(0) {}
  FMT_CONSTEXPR bool has(unsigned f) const { return (flags & f) != 0; }
  FMT_CONSTEXPR bool has_precision() const { return precision != -1; }
};

// Format specifiers.
template <typename Char>
struct basic_format_specs : align_spec, core_format_specs {
  FMT_CONSTEXPR basic_format_specs() {}
};

typedef basic_format_specs<char> format_specs;

namespace internal {

namespace grisu_options {
enum { fixed = 1, grisu3 = 2 };
}

// Formats value using the Grisu algorithm:
// https://www.cs.tufts.edu/~nr/cs257/archive/florian-loitsch/printf.pdf
template <typename Double, FMT_ENABLE_IF(sizeof(Double) == sizeof(uint64_t))>
FMT_API bool grisu_format(Double, buffer<char>&, int, unsigned, int&);
template <typename Double, FMT_ENABLE_IF(sizeof(Double) != sizeof(uint64_t))>
inline bool grisu_format(Double, buffer<char>&, int, unsigned, int&) {
  return false;
}

struct gen_digits_params {
  int num_digits;
  bool fixed;
  bool upper;
  bool trailing_zeros;
};

// Writes the exponent exp in the form "[+-]d{2,3}" to buffer.
template <typename Char, typename It> It write_exponent(int exp, It it) {
  FMT_ASSERT(-1000 < exp && exp < 1000, "exponent out of range");
  if (exp < 0) {
    *it++ = static_cast<Char>('-');
    exp = -exp;
  } else {
    *it++ = static_cast<Char>('+');
  }
  if (exp >= 100) {
    *it++ = static_cast<Char>(static_cast<char>('0' + exp / 100));
    exp %= 100;
    const char* d = data::DIGITS + exp * 2;
    *it++ = static_cast<Char>(d[0]);
    *it++ = static_cast<Char>(d[1]);
  } else {
    const char* d = data::DIGITS + exp * 2;
    *it++ = static_cast<Char>(d[0]);
    *it++ = static_cast<Char>(d[1]);
  }
  return it;
}

// The number is given as v = digits * pow(10, exp).
template <typename Char, typename It>
It grisu_prettify(const char* digits, int size, int exp, It it,
                  gen_digits_params params) {
  // pow(10, full_exp - 1) <= v <= pow(10, full_exp).
  int full_exp = size + exp;
  if (!params.fixed) {
    // Insert a decimal point after the first digit and add an exponent.
    *it++ = static_cast<Char>(*digits);
    if (size > 1) *it++ = static_cast<Char>('.');
    exp += size - 1;
    it = copy_str<Char>(digits + 1, digits + size, it);
    if (size < params.num_digits)
      it = std::fill_n(it, params.num_digits - size, static_cast<Char>('0'));
    *it++ = static_cast<Char>(params.upper ? 'E' : 'e');
    return write_exponent<Char>(exp, it);
  }
  if (size <= full_exp) {
    // 1234e7 -> 12340000000[.0+]
    it = copy_str<Char>(digits, digits + size, it);
    it = std::fill_n(it, full_exp - size, static_cast<Char>('0'));
    int num_zeros = (std::max)(params.num_digits - full_exp, 1);
    if (params.trailing_zeros) {
      *it++ = static_cast<Char>('.');
      it = std::fill_n(it, num_zeros, static_cast<Char>('0'));
    }
  } else if (full_exp > 0) {
    // 1234e-2 -> 12.34[0+]
    it = copy_str<Char>(digits, digits + full_exp, it);
    if (!params.trailing_zeros) {
      // Remove trailing zeros.
      while (size > full_exp && digits[size - 1] == '0') --size;
      if (size != full_exp) *it++ = static_cast<Char>('.');
      return copy_str<Char>(digits + full_exp, digits + size, it);
    }
    *it++ = static_cast<Char>('.');
    it = copy_str<Char>(digits + full_exp, digits + size, it);
    if (params.num_digits > size) {
      // Add trailing zeros.
      int num_zeros = params.num_digits - size;
      it = std::fill_n(it, num_zeros, static_cast<Char>('0'));
    }
  } else {
    // 1234e-6 -> 0.001234
    *it++ = static_cast<Char>('0');
    *it++ = static_cast<Char>('.');
    int num_zeros = -full_exp;
    if (params.num_digits >= 0 && params.num_digits < num_zeros)
      num_zeros = params.num_digits;
    it = std::fill_n(it, num_zeros, static_cast<Char>('0'));
    if (!params.trailing_zeros)
      while (size > 0 && digits[size - 1] == '0') --size;
    it = copy_str<Char>(digits, digits + size, it);
  }
  return it;
}

template <typename Double>
void sprintf_format(Double, internal::buffer<char>&, core_format_specs);

template <typename Handler>
FMT_CONSTEXPR void handle_int_type_spec(char spec, Handler&& handler) {
  switch (spec) {
  case 0:
  case 'd':
    handler.on_dec();
    break;
  case 'x':
  case 'X':
    handler.on_hex();
    break;
  case 'b':
  case 'B':
    handler.on_bin();
    break;
  case 'o':
    handler.on_oct();
    break;
  case 'n':
    handler.on_num();
    break;
  default:
    handler.on_error();
  }
}

template <typename Handler>
FMT_CONSTEXPR void handle_float_type_spec(char spec, Handler&& handler) {
  switch (spec) {
  case 0:
  case 'g':
  case 'G':
    handler.on_general();
    break;
  case 'e':
  case 'E':
    handler.on_exp();
    break;
  case 'f':
  case 'F':
    handler.on_fixed();
    break;
  case '%':
    handler.on_percent();
    break;
  case 'a':
  case 'A':
    handler.on_hex();
    break;
  default:
    handler.on_error();
    break;
  }
}

template <typename Char, typename Handler>
FMT_CONSTEXPR void handle_char_specs(const basic_format_specs<Char>* specs,
                                     Handler&& handler) {
  if (!specs) return handler.on_char();
  if (specs->type && specs->type != 'c') return handler.on_int();
  if (specs->align() == ALIGN_NUMERIC || specs->flags != 0)
    handler.on_error("invalid format specifier for char");
  handler.on_char();
}

template <typename Char, typename Handler>
FMT_CONSTEXPR void handle_cstring_type_spec(Char spec, Handler&& handler) {
  if (spec == 0 || spec == 's')
    handler.on_string();
  else if (spec == 'p')
    handler.on_pointer();
  else
    handler.on_error("invalid type specifier");
}

template <typename Char, typename ErrorHandler>
FMT_CONSTEXPR void check_string_type_spec(Char spec, ErrorHandler&& eh) {
  if (spec != 0 && spec != 's') eh.on_error("invalid type specifier");
}

template <typename Char, typename ErrorHandler>
FMT_CONSTEXPR void check_pointer_type_spec(Char spec, ErrorHandler&& eh) {
  if (spec != 0 && spec != 'p') eh.on_error("invalid type specifier");
}

template <typename ErrorHandler> class int_type_checker : private ErrorHandler {
 public:
  FMT_CONSTEXPR explicit int_type_checker(ErrorHandler eh) : ErrorHandler(eh) {}

  FMT_CONSTEXPR void on_dec() {}
  FMT_CONSTEXPR void on_hex() {}
  FMT_CONSTEXPR void on_bin() {}
  FMT_CONSTEXPR void on_oct() {}
  FMT_CONSTEXPR void on_num() {}

  FMT_CONSTEXPR void on_error() {
    ErrorHandler::on_error("invalid type specifier");
  }
};

template <typename ErrorHandler>
class float_type_checker : private ErrorHandler {
 public:
  FMT_CONSTEXPR explicit float_type_checker(ErrorHandler eh)
      : ErrorHandler(eh) {}

  FMT_CONSTEXPR void on_general() {}
  FMT_CONSTEXPR void on_exp() {}
  FMT_CONSTEXPR void on_fixed() {}
  FMT_CONSTEXPR void on_percent() {}
  FMT_CONSTEXPR void on_hex() {}

  FMT_CONSTEXPR void on_error() {
    ErrorHandler::on_error("invalid type specifier");
  }
};

template <typename ErrorHandler>
class char_specs_checker : public ErrorHandler {
 private:
  char type_;

 public:
  FMT_CONSTEXPR char_specs_checker(char type, ErrorHandler eh)
      : ErrorHandler(eh), type_(type) {}

  FMT_CONSTEXPR void on_int() {
    handle_int_type_spec(type_, int_type_checker<ErrorHandler>(*this));
  }
  FMT_CONSTEXPR void on_char() {}
};

template <typename ErrorHandler>
class cstring_type_checker : public ErrorHandler {
 public:
  FMT_CONSTEXPR explicit cstring_type_checker(ErrorHandler eh)
      : ErrorHandler(eh) {}

  FMT_CONSTEXPR void on_string() {}
  FMT_CONSTEXPR void on_pointer() {}
};

template <typename Context>
void arg_map<Context>::init(const basic_format_args<Context>& args) {
  if (map_) return;
  map_ = new entry[args.max_size()];
  if (args.is_packed()) {
    for (unsigned i = 0; /*nothing*/; ++i) {
      internal::type arg_type = args.type(i);
      if (arg_type == internal::none_type) return;
      if (arg_type == internal::named_arg_type) push_back(args.values_[i]);
    }
  }
  for (unsigned i = 0;; ++i) {
    auto type = args.args_[i].type_;
    if (type == internal::none_type) return;
    if (type == internal::named_arg_type) push_back(args.args_[i].value_);
  }
}

template <typename Range, typename ErrorHandler = internal::error_handler>
class arg_formatter_base {
 public:
  typedef typename Range::value_type char_type;
  typedef decltype(std::declval<Range>().begin()) iterator;
  typedef basic_format_specs<char_type> format_specs;

 private:
  typedef basic_writer<Range> writer_type;
  writer_type writer_;
  format_specs* specs_;

  struct char_writer {
    char_type value;

    size_t size() const { return 1; }
    size_t width() const { return 1; }

    template <typename It> void operator()(It&& it) const { *it++ = value; }
  };

  void write_char(char_type value) {
    if (specs_)
      writer_.write_padded(*specs_, char_writer{value});
    else
      writer_.write(value);
  }

  void write_pointer(const void* p) {
    writer_.write_pointer(internal::bit_cast<internal::uintptr_t>(p), specs_);
  }

 protected:
  writer_type& writer() { return writer_; }
  format_specs* spec() { return specs_; }
  iterator out() { return writer_.out(); }

  void write(bool value) {
    string_view sv(value ? "true" : "false");
    specs_ ? writer_.write(sv, *specs_) : writer_.write(sv);
  }

  void write(const char_type* value) {
    if (!value) FMT_THROW(format_error("string pointer is null"));
    auto length = std::char_traits<char_type>::length(value);
    basic_string_view<char_type> sv(value, length);
    specs_ ? writer_.write(sv, *specs_) : writer_.write(sv);
  }

 public:
  arg_formatter_base(Range r, format_specs* s, locale_ref loc)
      : writer_(r, loc), specs_(s) {}

  iterator operator()(monostate) {
    FMT_ASSERT(false, "invalid argument type");
    return out();
  }

  template <typename T, FMT_ENABLE_IF(std::is_integral<T>::value)>
  iterator operator()(T value) {
    if (specs_)
      writer_.write_int(value, *specs_);
    else
      writer_.write(value);
    return out();
  }

  iterator operator()(char_type value) {
    internal::handle_char_specs(
        specs_, char_spec_handler(*this, static_cast<char_type>(value)));
    return out();
  }

  iterator operator()(bool value) {
    if (specs_ && specs_->type) return (*this)(value ? 1 : 0);
    write(value != 0);
    return out();
  }

  template <typename T, FMT_ENABLE_IF(std::is_floating_point<T>::value)>
  iterator operator()(T value) {
    writer_.write_double(value, specs_ ? *specs_ : format_specs());
    return out();
  }

  struct char_spec_handler : ErrorHandler {
    arg_formatter_base& formatter;
    char_type value;

    char_spec_handler(arg_formatter_base& f, char_type val)
        : formatter(f), value(val) {}

    void on_int() {
      if (formatter.specs_)
        formatter.writer_.write_int(value, *formatter.specs_);
      else
        formatter.writer_.write(value);
    }
    void on_char() { formatter.write_char(value); }
  };

  struct cstring_spec_handler : internal::error_handler {
    arg_formatter_base& formatter;
    const char_type* value;

    cstring_spec_handler(arg_formatter_base& f, const char_type* val)
        : formatter(f), value(val) {}

    void on_string() { formatter.write(value); }
    void on_pointer() { formatter.write_pointer(value); }
  };

  iterator operator()(const char_type* value) {
    if (!specs_) return write(value), out();
    internal::handle_cstring_type_spec(specs_->type,
                                       cstring_spec_handler(*this, value));
    return out();
  }

  iterator operator()(basic_string_view<char_type> value) {
    if (specs_) {
      internal::check_string_type_spec(specs_->type, internal::error_handler());
      writer_.write(value, *specs_);
    } else {
      writer_.write(value);
    }
    return out();
  }

  iterator operator()(const void* value) {
    if (specs_)
      check_pointer_type_spec(specs_->type, internal::error_handler());
    write_pointer(value);
    return out();
  }
};

template <typename Char> FMT_CONSTEXPR bool is_name_start(Char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || '_' == c;
}

// Parses the range [begin, end) as an unsigned integer. This function assumes
// that the range is non-empty and the first character is a digit.
template <typename Char, typename ErrorHandler>
FMT_CONSTEXPR unsigned parse_nonnegative_int(const Char*& begin,
                                             const Char* end,
                                             ErrorHandler&& eh) {
  assert(begin != end && '0' <= *begin && *begin <= '9');
  if (*begin == '0') {
    ++begin;
    return 0;
  }
  unsigned value = 0;
  // Convert to unsigned to prevent a warning.
  unsigned max_int = (std::numeric_limits<int>::max)();
  unsigned big = max_int / 10;
  do {
    // Check for overflow.
    if (value > big) {
      value = max_int + 1;
      break;
    }
    value = value * 10 + unsigned(*begin - '0');
    ++begin;
  } while (begin != end && '0' <= *begin && *begin <= '9');
  if (value > max_int) eh.on_error("number is too big");
  return value;
}

template <typename Context> class custom_formatter {
 private:
  typedef typename Context::char_type char_type;

  basic_parse_context<char_type>& parse_ctx_;
  Context& ctx_;

 public:
  explicit custom_formatter(basic_parse_context<char_type>& parse_ctx,
                            Context& ctx)
      : parse_ctx_(parse_ctx), ctx_(ctx) {}

  bool operator()(typename basic_format_arg<Context>::handle h) const {
    h.format(parse_ctx_, ctx_);
    return true;
  }

  template <typename T> bool operator()(T) const { return false; }
};

template <typename T> struct is_integer {
  enum {
    value = std::is_integral<T>::value && !std::is_same<T, bool>::value &&
            !std::is_same<T, char>::value && !std::is_same<T, wchar_t>::value
  };
};

template <typename ErrorHandler> class width_checker {
 public:
  explicit FMT_CONSTEXPR width_checker(ErrorHandler& eh) : handler_(eh) {}

  template <typename T, FMT_ENABLE_IF(is_integer<T>::value)>
  FMT_CONSTEXPR unsigned long long operator()(T value) {
    if (is_negative(value)) handler_.on_error("negative width");
    return static_cast<unsigned long long>(value);
  }

  template <typename T, FMT_ENABLE_IF(!is_integer<T>::value)>
  FMT_CONSTEXPR unsigned long long operator()(T) {
    handler_.on_error("width is not integer");
    return 0;
  }

 private:
  ErrorHandler& handler_;
};

template <typename ErrorHandler> class precision_checker {
 public:
  explicit FMT_CONSTEXPR precision_checker(ErrorHandler& eh) : handler_(eh) {}

  template <typename T, FMT_ENABLE_IF(is_integer<T>::value)>
  FMT_CONSTEXPR unsigned long long operator()(T value) {
    if (is_negative(value)) handler_.on_error("negative precision");
    return static_cast<unsigned long long>(value);
  }

  template <typename T, FMT_ENABLE_IF(!is_integer<T>::value)>
  FMT_CONSTEXPR unsigned long long operator()(T) {
    handler_.on_error("precision is not integer");
    return 0;
  }

 private:
  ErrorHandler& handler_;
};

// A format specifier handler that sets fields in basic_format_specs.
template <typename Char> class specs_setter {
 public:
  explicit FMT_CONSTEXPR specs_setter(basic_format_specs<Char>& specs)
      : specs_(specs) {}

  FMT_CONSTEXPR specs_setter(const specs_setter& other)
      : specs_(other.specs_) {}

  FMT_CONSTEXPR void on_align(alignment align) { specs_.align_ = align; }
  FMT_CONSTEXPR void on_fill(Char fill) { specs_.fill_ = fill; }
  FMT_CONSTEXPR void on_plus() { specs_.flags |= SIGN_FLAG | PLUS_FLAG; }
  FMT_CONSTEXPR void on_minus() { specs_.flags |= MINUS_FLAG; }
  FMT_CONSTEXPR void on_space() { specs_.flags |= SIGN_FLAG; }
  FMT_CONSTEXPR void on_hash() { specs_.flags |= HASH_FLAG; }

  FMT_CONSTEXPR void on_zero() {
    specs_.align_ = ALIGN_NUMERIC;
    specs_.fill_ = '0';
  }

  FMT_CONSTEXPR void on_width(unsigned width) { specs_.width_ = width; }
  FMT_CONSTEXPR void on_precision(unsigned precision) {
    specs_.precision = static_cast<int>(precision);
  }
  FMT_CONSTEXPR void end_precision() {}

  FMT_CONSTEXPR void on_type(Char type) {
    specs_.type = static_cast<char>(type);
  }

 protected:
  basic_format_specs<Char>& specs_;
};

template <typename ErrorHandler> class numeric_specs_checker {
 public:
  FMT_CONSTEXPR numeric_specs_checker(ErrorHandler& eh, internal::type arg_type)
      : error_handler_(eh), arg_type_(arg_type) {}

  FMT_CONSTEXPR void require_numeric_argument() {
    if (!is_arithmetic(arg_type_))
      error_handler_.on_error("format specifier requires numeric argument");
  }

  FMT_CONSTEXPR void check_sign() {
    require_numeric_argument();
    if (is_integral(arg_type_) && arg_type_ != int_type &&
        arg_type_ != long_long_type && arg_type_ != internal::char_type) {
      error_handler_.on_error("format specifier requires signed argument");
    }
  }

  FMT_CONSTEXPR void check_precision() {
    if (is_integral(arg_type_) || arg_type_ == internal::pointer_type)
      error_handler_.on_error("precision not allowed for this argument type");
  }

 private:
  ErrorHandler& error_handler_;
  internal::type arg_type_;
};

// A format specifier handler that checks if specifiers are consistent with the
// argument type.
template <typename Handler> class specs_checker : public Handler {
 public:
  FMT_CONSTEXPR specs_checker(const Handler& handler, internal::type arg_type)
      : Handler(handler), checker_(*this, arg_type) {}

  FMT_CONSTEXPR specs_checker(const specs_checker& other)
      : Handler(other), checker_(*this, other.arg_type_) {}

  FMT_CONSTEXPR void on_align(alignment align) {
    if (align == ALIGN_NUMERIC) checker_.require_numeric_argument();
    Handler::on_align(align);
  }

  FMT_CONSTEXPR void on_plus() {
    checker_.check_sign();
    Handler::on_plus();
  }

  FMT_CONSTEXPR void on_minus() {
    checker_.check_sign();
    Handler::on_minus();
  }

  FMT_CONSTEXPR void on_space() {
    checker_.check_sign();
    Handler::on_space();
  }

  FMT_CONSTEXPR void on_hash() {
    checker_.require_numeric_argument();
    Handler::on_hash();
  }

  FMT_CONSTEXPR void on_zero() {
    checker_.require_numeric_argument();
    Handler::on_zero();
  }

  FMT_CONSTEXPR void end_precision() { checker_.check_precision(); }

 private:
  numeric_specs_checker<Handler> checker_;
};

template <template <typename> class Handler, typename T, typename FormatArg,
          typename ErrorHandler>
FMT_CONSTEXPR void set_dynamic_spec(T& value, FormatArg arg, ErrorHandler eh) {
  unsigned long long big_value =
      visit_format_arg(Handler<ErrorHandler>(eh), arg);
  if (big_value > to_unsigned((std::numeric_limits<int>::max)()))
    eh.on_error("number is too big");
  value = static_cast<T>(big_value);
}

struct auto_id {};

template <typename Context>
FMT_CONSTEXPR typename Context::format_arg get_arg(Context& ctx, unsigned id) {
  auto arg = ctx.arg(id);
  if (!arg) ctx.on_error("argument index out of range");
  return arg;
}

// The standard format specifier handler with checking.
template <typename ParseContext, typename Context>
class specs_handler : public specs_setter<typename Context::char_type> {
 public:
  typedef typename Context::char_type char_type;

  FMT_CONSTEXPR specs_handler(basic_format_specs<char_type>& specs,
                              ParseContext& parse_ctx, Context& ctx)
      : specs_setter<char_type>(specs),
        parse_context_(parse_ctx),
        context_(ctx) {}

  template <typename Id> FMT_CONSTEXPR void on_dynamic_width(Id arg_id) {
    set_dynamic_spec<width_checker>(this->specs_.width_, get_arg(arg_id),
                                    context_.error_handler());
  }

  template <typename Id> FMT_CONSTEXPR void on_dynamic_precision(Id arg_id) {
    set_dynamic_spec<precision_checker>(this->specs_.precision, get_arg(arg_id),
                                        context_.error_handler());
  }

  void on_error(const char* message) { context_.on_error(message); }

 private:
  // This is only needed for compatibility with gcc 4.4.
  typedef typename Context::format_arg format_arg;

  FMT_CONSTEXPR format_arg get_arg(auto_id) {
    return internal::get_arg(context_, parse_context_.next_arg_id());
  }

  FMT_CONSTEXPR format_arg get_arg(unsigned arg_id) {
    parse_context_.check_arg_id(arg_id);
    return internal::get_arg(context_, arg_id);
  }

  FMT_CONSTEXPR format_arg get_arg(basic_string_view<char_type> arg_id) {
    parse_context_.check_arg_id(arg_id);
    return context_.arg(arg_id);
  }

  ParseContext& parse_context_;
  Context& context_;
};

struct string_view_metadata {
  FMT_CONSTEXPR string_view_metadata() : offset_(0u), size_(0u) {}
  template <typename Char>
  FMT_CONSTEXPR string_view_metadata(basic_string_view<Char> primary_string,
                                     basic_string_view<Char> view)
      : offset_(view.data() - primary_string.data()), size_(view.size()) {}
  FMT_CONSTEXPR string_view_metadata(std::size_t offset, std::size_t size)
      : offset_(offset), size_(size) {}
  template <typename Char>
  FMT_CONSTEXPR basic_string_view<Char> to_view(const Char* str) const {
    return {str + offset_, size_};
  }

  std::size_t offset_;
  std::size_t size_;
};

// An argument reference.
template <typename Char> struct arg_ref {
  enum Kind { NONE, INDEX, NAME };
  typedef Char char_type;

  FMT_CONSTEXPR arg_ref() : kind(NONE), val() {}
  FMT_CONSTEXPR explicit arg_ref(unsigned index) : kind(INDEX), val(index) {}
  FMT_CONSTEXPR explicit arg_ref(string_view_metadata name)
      : kind(NAME), val(name) {}

  FMT_CONSTEXPR arg_ref& operator=(unsigned idx) {
    kind = INDEX;
    val.index = idx;
    return *this;
  }

  Kind kind;
  union value {
    FMT_CONSTEXPR value() : index(0u) {}
    FMT_CONSTEXPR value(unsigned id) : index(id) {}
    FMT_CONSTEXPR value(string_view_metadata n) : name(n) {}

    unsigned index;
    string_view_metadata name;
  } val;
};

// Format specifiers with width and precision resolved at formatting rather
// than parsing time to allow re-using the same parsed specifiers with
// different sets of arguments (precompilation of format strings).
template <typename Char>
struct dynamic_format_specs : basic_format_specs<Char> {
  arg_ref<Char> width_ref;
  arg_ref<Char> precision_ref;
};

// Format spec handler that saves references to arguments representing dynamic
// width and precision to be resolved at formatting time.
template <typename ParseContext>
class dynamic_specs_handler
    : public specs_setter<typename ParseContext::char_type> {
 public:
  typedef typename ParseContext::char_type char_type;

  FMT_CONSTEXPR dynamic_specs_handler(dynamic_format_specs<char_type>& specs,
                                      ParseContext& ctx)
      : specs_setter<char_type>(specs), specs_(specs), context_(ctx) {}

  FMT_CONSTEXPR dynamic_specs_handler(const dynamic_specs_handler& other)
      : specs_setter<char_type>(other),
        specs_(other.specs_),
        context_(other.context_) {}

  template <typename Id> FMT_CONSTEXPR void on_dynamic_width(Id arg_id) {
    specs_.width_ref = make_arg_ref(arg_id);
  }

  template <typename Id> FMT_CONSTEXPR void on_dynamic_precision(Id arg_id) {
    specs_.precision_ref = make_arg_ref(arg_id);
  }

  FMT_CONSTEXPR void on_error(const char* message) {
    context_.on_error(message);
  }

 private:
  typedef arg_ref<char_type> arg_ref_type;

  FMT_CONSTEXPR arg_ref_type make_arg_ref(unsigned arg_id) {
    context_.check_arg_id(arg_id);
    return arg_ref_type(arg_id);
  }

  FMT_CONSTEXPR arg_ref_type make_arg_ref(auto_id) {
    return arg_ref_type(context_.next_arg_id());
  }

  FMT_CONSTEXPR arg_ref_type make_arg_ref(basic_string_view<char_type> arg_id) {
    context_.check_arg_id(arg_id);
    basic_string_view<char_type> format_str(context_.begin(),
                                            context_.end() - context_.begin());
    const auto id_metadata = string_view_metadata(format_str, arg_id);
    return arg_ref_type(id_metadata);
  }

  dynamic_format_specs<char_type>& specs_;
  ParseContext& context_;
};

template <typename Char, typename IDHandler>
FMT_CONSTEXPR const Char* parse_arg_id(const Char* begin, const Char* end,
                                       IDHandler&& handler) {
  assert(begin != end);
  Char c = *begin;
  if (c == '}' || c == ':') return handler(), begin;
  if (c >= '0' && c <= '9') {
    unsigned index = parse_nonnegative_int(begin, end, handler);
    if (begin == end || (*begin != '}' && *begin != ':'))
      return handler.on_error("invalid format string"), begin;
    handler(index);
    return begin;
  }
  if (!is_name_start(c))
    return handler.on_error("invalid format string"), begin;
  auto it = begin;
  do {
    ++it;
  } while (it != end && (is_name_start(c = *it) || ('0' <= c && c <= '9')));
  handler(basic_string_view<Char>(begin, to_unsigned(it - begin)));
  return it;
}

// Adapts SpecHandler to IDHandler API for dynamic width.
template <typename SpecHandler, typename Char> struct width_adapter {
  explicit FMT_CONSTEXPR width_adapter(SpecHandler& h) : handler(h) {}

  FMT_CONSTEXPR void operator()() { handler.on_dynamic_width(auto_id()); }
  FMT_CONSTEXPR void operator()(unsigned id) { handler.on_dynamic_width(id); }
  FMT_CONSTEXPR void operator()(basic_string_view<Char> id) {
    handler.on_dynamic_width(id);
  }

  FMT_CONSTEXPR void on_error(const char* message) {
    handler.on_error(message);
  }

  SpecHandler& handler;
};

// Adapts SpecHandler to IDHandler API for dynamic precision.
template <typename SpecHandler, typename Char> struct precision_adapter {
  explicit FMT_CONSTEXPR precision_adapter(SpecHandler& h) : handler(h) {}

  FMT_CONSTEXPR void operator()() { handler.on_dynamic_precision(auto_id()); }
  FMT_CONSTEXPR void operator()(unsigned id) {
    handler.on_dynamic_precision(id);
  }
  FMT_CONSTEXPR void operator()(basic_string_view<Char> id) {
    handler.on_dynamic_precision(id);
  }

  FMT_CONSTEXPR void on_error(const char* message) {
    handler.on_error(message);
  }

  SpecHandler& handler;
};

// Parses fill and alignment.
template <typename Char, typename Handler>
FMT_CONSTEXPR const Char* parse_align(const Char* begin, const Char* end,
                                      Handler&& handler) {
  FMT_ASSERT(begin != end, "");
  alignment align = ALIGN_DEFAULT;
  int i = 0;
  if (begin + 1 != end) ++i;
  do {
    switch (static_cast<char>(begin[i])) {
    case '<':
      align = ALIGN_LEFT;
      break;
    case '>':
      align = ALIGN_RIGHT;
      break;
    case '=':
      align = ALIGN_NUMERIC;
      break;
    case '^':
      align = ALIGN_CENTER;
      break;
    }
    if (align != ALIGN_DEFAULT) {
      if (i > 0) {
        auto c = *begin;
        if (c == '{')
          return handler.on_error("invalid fill character '{'"), begin;
        begin += 2;
        handler.on_fill(c);
      } else
        ++begin;
      handler.on_align(align);
      break;
    }
  } while (i-- > 0);
  return begin;
}

template <typename Char, typename Handler>
FMT_CONSTEXPR const Char* parse_width(const Char* begin, const Char* end,
                                      Handler&& handler) {
  FMT_ASSERT(begin != end, "");
  if ('0' <= *begin && *begin <= '9') {
    handler.on_width(parse_nonnegative_int(begin, end, handler));
  } else if (*begin == '{') {
    ++begin;
    if (begin != end)
      begin = parse_arg_id(begin, end, width_adapter<Handler, Char>(handler));
    if (begin == end || *begin != '}')
      return handler.on_error("invalid format string"), begin;
    ++begin;
  }
  return begin;
}

template <typename Char, typename Handler>
FMT_CONSTEXPR const Char* parse_precision(const Char* begin, const Char* end,
                                          Handler&& handler) {
  ++begin;
  auto c = begin != end ? *begin : Char();
  if ('0' <= c && c <= '9') {
    handler.on_precision(parse_nonnegative_int(begin, end, handler));
  } else if (c == '{') {
    ++begin;
    if (begin != end) {
      begin =
          parse_arg_id(begin, end, precision_adapter<Handler, Char>(handler));
    }
    if (begin == end || *begin++ != '}')
      return handler.on_error("invalid format string"), begin;
  } else {
    return handler.on_error("missing precision specifier"), begin;
  }
  handler.end_precision();
  return begin;
}

// Parses standard format specifiers and sends notifications about parsed
// components to handler.
template <typename Char, typename SpecHandler>
FMT_CONSTEXPR const Char* parse_format_specs(const Char* begin, const Char* end,
                                             SpecHandler&& handler) {
  if (begin == end || *begin == '}') return begin;

  begin = parse_align(begin, end, handler);
  if (begin == end) return begin;

  // Parse sign.
  switch (static_cast<char>(*begin)) {
  case '+':
    handler.on_plus();
    ++begin;
    break;
  case '-':
    handler.on_minus();
    ++begin;
    break;
  case ' ':
    handler.on_space();
    ++begin;
    break;
  }
  if (begin == end) return begin;

  if (*begin == '#') {
    handler.on_hash();
    if (++begin == end) return begin;
  }

  // Parse zero flag.
  if (*begin == '0') {
    handler.on_zero();
    if (++begin == end) return begin;
  }

  begin = parse_width(begin, end, handler);
  if (begin == end) return begin;

  // Parse precision.
  if (*begin == '.') {
    begin = parse_precision(begin, end, handler);
  }

  // Parse type.
  if (begin != end && *begin != '}') handler.on_type(*begin++);
  return begin;
}

// Return the result via the out param to workaround gcc bug 77539.
template <bool IS_CONSTEXPR, typename T, typename Ptr = const T*>
FMT_CONSTEXPR bool find(Ptr first, Ptr last, T value, Ptr& out) {
  for (out = first; out != last; ++out) {
    if (*out == value) return true;
  }
  return false;
}

template <>
inline bool find<false, char>(const char* first, const char* last, char value,
                              const char*& out) {
  out = static_cast<const char*>(
      std::memchr(first, value, internal::to_unsigned(last - first)));
  return out != nullptr;
}

template <typename Handler, typename Char> struct id_adapter {
  FMT_CONSTEXPR void operator()() { handler.on_arg_id(); }
  FMT_CONSTEXPR void operator()(unsigned id) { handler.on_arg_id(id); }
  FMT_CONSTEXPR void operator()(basic_string_view<Char> id) {
    handler.on_arg_id(id);
  }
  FMT_CONSTEXPR void on_error(const char* message) {
    handler.on_error(message);
  }
  Handler& handler;
};

template <bool IS_CONSTEXPR, typename Char, typename Handler>
FMT_CONSTEXPR void parse_format_string(basic_string_view<Char> format_str,
                                       Handler&& handler) {
  struct writer {
    FMT_CONSTEXPR void operator()(const Char* begin, const Char* end) {
      if (begin == end) return;
      for (;;) {
        const Char* p = nullptr;
        if (!find<IS_CONSTEXPR>(begin, end, '}', p))
          return handler_.on_text(begin, end);
        ++p;
        if (p == end || *p != '}')
          return handler_.on_error("unmatched '}' in format string");
        handler_.on_text(begin, p);
        begin = p + 1;
      }
    }
    Handler& handler_;
  } write{handler};
  auto begin = format_str.data();
  auto end = begin + format_str.size();
  while (begin != end) {
    // Doing two passes with memchr (one for '{' and another for '}') is up to
    // 2.5x faster than the naive one-pass implementation on big format strings.
    const Char* p = begin;
    if (*begin != '{' && !find<IS_CONSTEXPR>(begin, end, '{', p))
      return write(begin, end);
    write(begin, p);
    ++p;
    if (p == end) return handler.on_error("invalid format string");
    if (static_cast<char>(*p) == '}') {
      handler.on_arg_id();
      handler.on_replacement_field(p);
    } else if (*p == '{') {
      handler.on_text(p, p + 1);
    } else {
      p = parse_arg_id(p, end, id_adapter<Handler, Char>{handler});
      Char c = p != end ? *p : Char();
      if (c == '}') {
        handler.on_replacement_field(p);
      } else if (c == ':') {
        p = handler.on_format_specs(p + 1, end);
        if (p == end || *p != '}')
          return handler.on_error("unknown format specifier");
      } else {
        return handler.on_error("missing '}' in format string");
      }
    }
    begin = p + 1;
  }
}

template <typename T, typename ParseContext>
FMT_CONSTEXPR const typename ParseContext::char_type* parse_format_specs(
    ParseContext& ctx) {
  using char_type = typename ParseContext::char_type;
  using context = buffer_context<char_type>;
  using mapped_type =
      conditional_t<internal::mapped_type_constant<T, context>::value !=
                        internal::custom_type,
                    decltype(arg_mapper<context>().map(std::declval<T>())), T>;
  conditional_t<has_formatter<mapped_type, context>::value,
                formatter<mapped_type, char_type>,
                internal::fallback_formatter<T, char_type>>
      f;
  return f.parse(ctx);
}

template <typename Char, typename ErrorHandler, typename... Args>
class format_string_checker {
 public:
  explicit FMT_CONSTEXPR format_string_checker(
      basic_string_view<Char> format_str, ErrorHandler eh)
      : arg_id_((std::numeric_limits<unsigned>::max)()),
        context_(format_str, eh),
        parse_funcs_{&parse_format_specs<Args, parse_context_type>...} {}

  FMT_CONSTEXPR void on_text(const Char*, const Char*) {}

  FMT_CONSTEXPR void on_arg_id() {
    arg_id_ = context_.next_arg_id();
    check_arg_id();
  }
  FMT_CONSTEXPR void on_arg_id(unsigned id) {
    arg_id_ = id;
    context_.check_arg_id(id);
    check_arg_id();
  }
  FMT_CONSTEXPR void on_arg_id(basic_string_view<Char>) {
    on_error("compile-time checks don't support named arguments");
  }

  FMT_CONSTEXPR void on_replacement_field(const Char*) {}

  FMT_CONSTEXPR const Char* on_format_specs(const Char* begin, const Char*) {
    advance_to(context_, begin);
    return arg_id_ < NUM_ARGS ? parse_funcs_[arg_id_](context_) : begin;
  }

  FMT_CONSTEXPR void on_error(const char* message) {
    context_.on_error(message);
  }

 private:
  typedef basic_parse_context<Char, ErrorHandler> parse_context_type;
  enum { NUM_ARGS = sizeof...(Args) };

  FMT_CONSTEXPR void check_arg_id() {
    if (arg_id_ >= NUM_ARGS) context_.on_error("argument index out of range");
  }

  // Format specifier parsing function.
  typedef const Char* (*parse_func)(parse_context_type&);

  unsigned arg_id_;
  parse_context_type context_;
  parse_func parse_funcs_[NUM_ARGS > 0 ? NUM_ARGS : 1];
};

template <typename Char, typename ErrorHandler, typename... Args>
FMT_CONSTEXPR bool do_check_format_string(basic_string_view<Char> s,
                                          ErrorHandler eh = ErrorHandler()) {
  format_string_checker<Char, ErrorHandler, Args...> checker(s, eh);
  parse_format_string<true>(s, checker);
  return true;
}

template <typename... Args, typename S,
          enable_if_t<(is_compile_string<S>::value), int>>
void check_format_string(S format_str) {
  typedef typename S::char_type char_t;
  FMT_CONSTEXPR_DECL bool invalid_format =
      internal::do_check_format_string<char_t, internal::error_handler,
                                       Args...>(to_string_view(format_str));
  (void)invalid_format;
}

template <template <typename> class Handler, typename Spec, typename Context>
void handle_dynamic_spec(Spec& value, arg_ref<typename Context::char_type> ref,
                         Context& ctx,
                         const typename Context::char_type* format_str) {
  typedef typename Context::char_type char_type;
  switch (ref.kind) {
  case arg_ref<char_type>::NONE:
    break;
  case arg_ref<char_type>::INDEX:
    internal::set_dynamic_spec<Handler>(value, ctx.arg(ref.val.index),
                                        ctx.error_handler());
    break;
  case arg_ref<char_type>::NAME: {
    const auto arg_id = ref.val.name.to_view(format_str);
    internal::set_dynamic_spec<Handler>(value, ctx.arg(arg_id),
                                        ctx.error_handler());
  } break;
  }
}
}  // namespace internal

/** The default argument formatter. */
template <typename Range>
class arg_formatter : public internal::arg_formatter_base<Range> {
 private:
  typedef typename Range::value_type char_type;
  typedef internal::arg_formatter_base<Range> base;
  typedef basic_format_context<typename base::iterator, char_type> context_type;

  context_type& ctx_;
  basic_parse_context<char_type>* parse_ctx_;

 public:
  typedef Range range;
  typedef typename base::iterator iterator;
  typedef typename base::format_specs format_specs;

  /**
    \rst
    Constructs an argument formatter object.
    *ctx* is a reference to the formatting context,
    *spec* contains format specifier information for standard argument types.
    \endrst
   */
  explicit arg_formatter(context_type& ctx,
                         basic_parse_context<char_type>* parse_ctx = nullptr,
                         format_specs* spec = nullptr)
      : base(Range(ctx.out()), spec, ctx.locale()),
        ctx_(ctx),
        parse_ctx_(parse_ctx) {}

  FMT_DEPRECATED arg_formatter(context_type& ctx, format_specs& spec)
      : base(Range(ctx.out()), &spec), ctx_(ctx) {}

  using base::operator();

  /** Formats an argument of a user-defined type. */
  iterator operator()(typename basic_format_arg<context_type>::handle handle) {
    handle.format(*parse_ctx_, ctx_);
    return this->out();
  }
};

/**
 An error returned by an operating system or a language runtime,
 for example a file opening error.
*/
class FMT_API system_error : public std::runtime_error {
 private:
  FMT_API void init(int err_code, string_view format_str, format_args args);

 protected:
  int error_code_;

  system_error() : std::runtime_error("") {}

 public:
  /**
   \rst
   Constructs a :class:`fmt::system_error` object with a description
   formatted with `fmt::format_system_error`. *message* and additional
   arguments passed into the constructor are formatted similarly to
   `fmt::format`.

   **Example**::

     // This throws a system_error with the description
     //   cannot open file 'madeup': No such file or directory
     // or similar (system message may vary).
     const char *filename = "madeup";
     std::FILE *file = std::fopen(filename, "r");
     if (!file)
       throw fmt::system_error(errno, "cannot open file '{}'", filename);
   \endrst
  */
  template <typename... Args>
  system_error(int error_code, string_view message, const Args&... args)
      : std::runtime_error("") {
    init(error_code, message, make_format_args(args...));
  }
  ~system_error() FMT_NOEXCEPT;

  int error_code() const { return error_code_; }
};

/**
  \rst
  Formats an error returned by an operating system or a language runtime,
  for example a file opening error, and writes it to *out* in the following
  form:

  .. parsed-literal::
     *<message>*: *<system-message>*

  where *<message>* is the passed message and *<system-message>* is
  the system message corresponding to the error code.
  *error_code* is a system error code as given by ``errno``.
  If *error_code* is not a valid error code such as -1, the system message
  may look like "Unknown error -1" and is platform-dependent.
  \endrst
 */
FMT_API void format_system_error(internal::buffer<char>& out, int error_code,
                                 fmt::string_view message) FMT_NOEXCEPT;

/**
  This template provides operations for formatting and writing data into a
  character range.
 */
template <typename Range> class basic_writer {
 public:
  typedef typename Range::value_type char_type;
  typedef decltype(std::declval<Range>().begin()) iterator;
  typedef basic_format_specs<char_type> format_specs;

 private:
  iterator out_;  // Output iterator.
  internal::locale_ref locale_;

  // Attempts to reserve space for n extra characters in the output range.
  // Returns a pointer to the reserved range or a reference to out_.
  auto reserve(std::size_t n) -> decltype(internal::reserve(out_, n)) {
    return internal::reserve(out_, n);
  }

  // Writes a value in the format
  //   <left-padding><value><right-padding>
  // where <value> is written by f(it).
  template <typename F> void write_padded(const align_spec& spec, F&& f) {
    unsigned width = spec.width();  // User-perceived width (in code points).
    size_t size = f.size();         // The number of code units.
    size_t num_code_points = width != 0 ? f.width() : size;
    if (width <= num_code_points) return f(reserve(size));
    auto&& it = reserve(width + (size - num_code_points));
    char_type fill = static_cast<char_type>(spec.fill());
    std::size_t padding = width - num_code_points;
    if (spec.align() == ALIGN_RIGHT) {
      it = std::fill_n(it, padding, fill);
      f(it);
    } else if (spec.align() == ALIGN_CENTER) {
      std::size_t left_padding = padding / 2;
      it = std::fill_n(it, left_padding, fill);
      f(it);
      it = std::fill_n(it, padding - left_padding, fill);
    } else {
      f(it);
      it = std::fill_n(it, padding, fill);
    }
  }

  template <typename F> struct padded_int_writer {
    size_t size_;
    string_view prefix;
    char_type fill;
    std::size_t padding;
    F f;

    size_t size() const { return size_; }
    size_t width() const { return size_; }

    template <typename It> void operator()(It&& it) const {
      if (prefix.size() != 0)
        it = internal::copy_str<char_type>(prefix.begin(), prefix.end(), it);
      it = std::fill_n(it, padding, fill);
      f(it);
    }
  };

  // Writes an integer in the format
  //   <left-padding><prefix><numeric-padding><digits><right-padding>
  // where <digits> are written by f(it).
  template <typename Spec, typename F>
  void write_int(int num_digits, string_view prefix, const Spec& spec, F f) {
    std::size_t size = prefix.size() + internal::to_unsigned(num_digits);
    char_type fill = static_cast<char_type>(spec.fill());
    std::size_t padding = 0;
    if (spec.align() == ALIGN_NUMERIC) {
      if (spec.width() > size) {
        padding = spec.width() - size;
        size = spec.width();
      }
    } else if (spec.precision > num_digits) {
      size = prefix.size() + internal::to_unsigned(spec.precision);
      padding = internal::to_unsigned(spec.precision - num_digits);
      fill = static_cast<char_type>('0');
    }
    align_spec as = spec;
    if (spec.align() == ALIGN_DEFAULT) as.align_ = ALIGN_RIGHT;
    write_padded(as, padded_int_writer<F>{size, prefix, fill, padding, f});
  }

  // Writes a decimal integer.
  template <typename Int> void write_decimal(Int value) {
    typedef typename internal::int_traits<Int>::main_type main_type;
    main_type abs_value = static_cast<main_type>(value);
    bool is_negative = internal::is_negative(value);
    if (is_negative) abs_value = 0 - abs_value;
    int num_digits = internal::count_digits(abs_value);
    auto&& it =
        reserve((is_negative ? 1 : 0) + static_cast<size_t>(num_digits));
    if (is_negative) *it++ = static_cast<char_type>('-');
    it = internal::format_decimal<char_type>(it, abs_value, num_digits);
  }

  // The handle_int_type_spec handler that writes an integer.
  template <typename Int, typename Spec> struct int_writer {
    typedef typename internal::int_traits<Int>::main_type unsigned_type;

    basic_writer<Range>& writer;
    const Spec& spec;
    unsigned_type abs_value;
    char prefix[4];
    unsigned prefix_size;

    string_view get_prefix() const { return string_view(prefix, prefix_size); }

    int_writer(basic_writer<Range>& w, Int value, const Spec& s)
        : writer(w),
          spec(s),
          abs_value(static_cast<unsigned_type>(value)),
          prefix_size(0) {
      if (internal::is_negative(value)) {
        prefix[0] = '-';
        ++prefix_size;
        abs_value = 0 - abs_value;
      } else if (spec.has(SIGN_FLAG)) {
        prefix[0] = spec.has(PLUS_FLAG) ? '+' : ' ';
        ++prefix_size;
      }
    }

    struct dec_writer {
      unsigned_type abs_value;
      int num_digits;

      template <typename It> void operator()(It&& it) const {
        it = internal::format_decimal<char_type>(it, abs_value, num_digits);
      }
    };

    void on_dec() {
      int num_digits = internal::count_digits(abs_value);
      writer.write_int(num_digits, get_prefix(), spec,
                       dec_writer{abs_value, num_digits});
    }

    struct hex_writer {
      int_writer& self;
      int num_digits;

      template <typename It> void operator()(It&& it) const {
        it = internal::format_uint<4, char_type>(it, self.abs_value, num_digits,
                                                 self.spec.type != 'x');
      }
    };

    void on_hex() {
      if (spec.has(HASH_FLAG)) {
        prefix[prefix_size++] = '0';
        prefix[prefix_size++] = static_cast<char>(spec.type);
      }
      int num_digits = internal::count_digits<4>(abs_value);
      writer.write_int(num_digits, get_prefix(), spec,
                       hex_writer{*this, num_digits});
    }

    template <int BITS> struct bin_writer {
      unsigned_type abs_value;
      int num_digits;

      template <typename It> void operator()(It&& it) const {
        it = internal::format_uint<BITS, char_type>(it, abs_value, num_digits);
      }
    };

    void on_bin() {
      if (spec.has(HASH_FLAG)) {
        prefix[prefix_size++] = '0';
        prefix[prefix_size++] = static_cast<char>(spec.type);
      }
      int num_digits = internal::count_digits<1>(abs_value);
      writer.write_int(num_digits, get_prefix(), spec,
                       bin_writer<1>{abs_value, num_digits});
    }

    void on_oct() {
      int num_digits = internal::count_digits<3>(abs_value);
      if (spec.has(HASH_FLAG) && spec.precision <= num_digits) {
        // Octal prefix '0' is counted as a digit, so only add it if precision
        // is not greater than the number of digits.
        prefix[prefix_size++] = '0';
      }
      writer.write_int(num_digits, get_prefix(), spec,
                       bin_writer<3>{abs_value, num_digits});
    }

    enum { SEP_SIZE = 1 };

    struct num_writer {
      unsigned_type abs_value;
      int size;
      char_type sep;

      template <typename It> void operator()(It&& it) const {
        basic_string_view<char_type> s(&sep, SEP_SIZE);
        it = internal::format_decimal<char_type>(
            it, abs_value, size, internal::add_thousands_sep<char_type>(s));
      }
    };

    void on_num() {
      int num_digits = internal::count_digits(abs_value);
      char_type sep = internal::thousands_sep<char_type>(writer.locale_);
      int size = num_digits + SEP_SIZE * ((num_digits - 1) / 3);
      writer.write_int(size, get_prefix(), spec,
                       num_writer{abs_value, size, sep});
    }

    FMT_NORETURN void on_error() {
      FMT_THROW(format_error("invalid type specifier"));
    }
  };

  // Writes a formatted integer.
  template <typename T, typename Spec>
  void write_int(T value, const Spec& spec) {
    internal::handle_int_type_spec(spec.type,
                                   int_writer<T, Spec>(*this, value, spec));
  }

  enum { INF_SIZE = 3 };  // This is an enum to workaround a bug in MSVC.

  struct inf_or_nan_writer {
    char sign;
    bool as_percentage;
    const char* str;

    size_t size() const {
      return static_cast<std::size_t>(INF_SIZE + (sign ? 1 : 0) +
                                      (as_percentage ? 1 : 0));
    }
    size_t width() const { return size(); }

    template <typename It> void operator()(It&& it) const {
      if (sign) *it++ = static_cast<char_type>(sign);
      it = internal::copy_str<char_type>(
          str, str + static_cast<std::size_t>(INF_SIZE), it);
      if (as_percentage) *it++ = static_cast<char_type>('%');
    }
  };

  struct double_writer {
    char sign;
    internal::buffer<char>& buffer;

    size_t size() const { return buffer.size() + (sign ? 1 : 0); }
    size_t width() const { return size(); }

    template <typename It> void operator()(It&& it) {
      if (sign) *it++ = static_cast<char_type>(sign);
      it = internal::copy_str<char_type>(buffer.begin(), buffer.end(), it);
    }
  };

  class grisu_writer {
   private:
    internal::buffer<char>& digits_;
    size_t size_;
    char sign_;
    int exp_;
    internal::gen_digits_params params_;

   public:
    grisu_writer(char sign, internal::buffer<char>& digits, int exp,
                 const internal::gen_digits_params& params)
        : digits_(digits), sign_(sign), exp_(exp), params_(params) {
      int num_digits = static_cast<int>(digits.size());
      int full_exp = num_digits + exp - 1;
      int precision = params.num_digits > 0 ? params.num_digits : 11;
      params_.fixed |= full_exp >= -4 && full_exp < precision;
      auto it = internal::grisu_prettify<char>(
          digits.data(), num_digits, exp, internal::counting_iterator<char>(),
          params_);
      size_ = it.count();
    }

    size_t size() const { return size_ + (sign_ ? 1 : 0); }
    size_t width() const { return size(); }

    template <typename It> void operator()(It&& it) {
      if (sign_) *it++ = static_cast<char_type>(sign_);
      int num_digits = static_cast<int>(digits_.size());
      it = internal::grisu_prettify<char_type>(digits_.data(), num_digits, exp_,
                                               it, params_);
    }
  };

  // Formats a floating-point number (double or long double).
  template <typename T> void write_double(T value, const format_specs& spec);

  template <typename Char> struct str_writer {
    const Char* s;
    size_t size_;

    size_t size() const { return size_; }
    size_t width() const {
      return internal::count_code_points(basic_string_view<Char>(s, size_));
    }

    template <typename It> void operator()(It&& it) const {
      it = internal::copy_str<char_type>(s, s + size_, it);
    }
  };

  template <typename UIntPtr> struct pointer_writer {
    UIntPtr value;
    int num_digits;

    size_t size() const { return num_digits + 2; }
    size_t width() const { return size(); }

    template <typename It> void operator()(It&& it) const {
      *it++ = static_cast<char_type>('0');
      *it++ = static_cast<char_type>('x');
      it = internal::format_uint<4, char_type>(it, value, num_digits);
    }
  };

  template <typename Char, typename ErrorHandler>
  friend class internal::arg_formatter_base;

 public:
  /** Constructs a ``basic_writer`` object. */
  explicit basic_writer(Range out,
                        internal::locale_ref loc = internal::locale_ref())
      : out_(out.begin()), locale_(loc) {}

  iterator out() const { return out_; }

  void write(int value) { write_decimal(value); }
  void write(long value) { write_decimal(value); }
  void write(long long value) { write_decimal(value); }

  void write(unsigned value) { write_decimal(value); }
  void write(unsigned long value) { write_decimal(value); }
  void write(unsigned long long value) { write_decimal(value); }

  /**
    \rst
    Formats *value* and writes it to the buffer.
    \endrst
   */
  template <typename T, typename FormatSpec, typename... FormatSpecs,
            FMT_ENABLE_IF(std::is_integral<T>::value)>
  void write(T value, FormatSpec spec, FormatSpecs... specs) {
    format_specs s(spec, specs...);
    s.align_ = ALIGN_RIGHT;
    write_int(value, s);
  }

  void write(double value, const format_specs& spec = format_specs()) {
    write_double(value, spec);
  }

  /**
    \rst
    Formats *value* using the general format for floating-point numbers
    (``'g'``) and writes it to the buffer.
    \endrst
   */
  void write(long double value, const format_specs& spec = format_specs()) {
    write_double(value, spec);
  }

  /** Writes a character to the buffer. */
  void write(char value) {
    auto&& it = reserve(1);
    *it++ = value;
  }

  template <typename Char, FMT_ENABLE_IF(std::is_same<Char, char_type>::value)>
  void write(Char value) {
    auto&& it = reserve(1);
    *it++ = value;
  }

  /**
    \rst
    Writes *value* to the buffer.
    \endrst
   */
  void write(string_view value) {
    auto&& it = reserve(value.size());
    it = internal::copy_str<char_type>(value.begin(), value.end(), it);
  }
  void write(wstring_view value) {
    static_assert(std::is_same<char_type, wchar_t>::value, "");
    auto&& it = reserve(value.size());
    it = std::copy(value.begin(), value.end(), it);
  }

  // Writes a formatted string.
  template <typename Char>
  void write(const Char* s, std::size_t size, const align_spec& spec) {
    write_padded(spec, str_writer<Char>{s, size});
  }

  template <typename Char>
  void write(basic_string_view<Char> s,
             const format_specs& spec = format_specs()) {
    const Char* data = s.data();
    std::size_t size = s.size();
    if (spec.precision >= 0 && internal::to_unsigned(spec.precision) < size)
      size = internal::to_unsigned(spec.precision);
    write(data, size, spec);
  }

  template <typename UIntPtr>
  void write_pointer(UIntPtr value, const align_spec* spec) {
    int num_digits = internal::count_digits<4>(value);
    auto pw = pointer_writer<UIntPtr>{value, num_digits};
    if (!spec) return pw(reserve(num_digits + 2));
    align_spec as = *spec;
    if (as.align() == ALIGN_DEFAULT) as.align_ = ALIGN_RIGHT;
    write_padded(as, pw);
  }
};

struct float_spec_handler {
  char type;
  bool upper;
  bool fixed;
  bool as_percentage;

  explicit float_spec_handler(char t)
      : type(t), upper(false), fixed(false), as_percentage(false) {}

  void on_general() {
    if (type == 'G') upper = true;
  }

  void on_exp() {
    if (type == 'E') upper = true;
  }

  void on_fixed() {
    fixed = true;
    if (type == 'F') upper = true;
  }

  void on_percent() {
    fixed = true;
    as_percentage = true;
  }

  void on_hex() {
    if (type == 'A') upper = true;
  }

  FMT_NORETURN void on_error() {
    FMT_THROW(format_error("invalid type specifier"));
  }
};

template <typename Range>
template <typename T>
void basic_writer<Range>::write_double(T value, const format_specs& spec) {
  // Check type.
  float_spec_handler handler(static_cast<char>(spec.type));
  internal::handle_float_type_spec(handler.type, handler);

  char sign = 0;
  // Use signbit instead of value < 0 since the latter is always false for NaN.
  if (std::signbit(value)) {
    sign = '-';
    value = -value;
  } else if (spec.has(SIGN_FLAG)) {
    sign = spec.has(PLUS_FLAG) ? '+' : ' ';
  }

  if (!std::isfinite(value)) {
    // Format infinity and NaN ourselves because sprintf's output is not
    // consistent across platforms.
    const char* str = std::isinf(value) ? (handler.upper ? "INF" : "inf")
                                        : (handler.upper ? "NAN" : "nan");
    return write_padded(spec,
                        inf_or_nan_writer{sign, handler.as_percentage, str});
  }

  if (handler.as_percentage) value *= 100;

  memory_buffer buffer;
  int exp = 0;
  int precision = spec.has_precision() || !spec.type ? spec.precision : 6;
  unsigned options = handler.fixed ? internal::grisu_options::fixed : 0;
  bool use_grisu = fmt::internal::use_grisu<T>() &&
                   (spec.type != 'a' && spec.type != 'A' && spec.type != 'e' &&
                    spec.type != 'E') &&
                   internal::grisu_format(static_cast<double>(value), buffer,
                                          precision, options, exp);
  if (!use_grisu) internal::sprintf_format(value, buffer, spec);

  if (handler.as_percentage) {
    buffer.push_back('%');
    --exp;  // Adjust decimal place position.
  }
  align_spec as = spec;
  if (spec.align() == ALIGN_NUMERIC) {
    if (sign) {
      auto&& it = reserve(1);
      *it++ = static_cast<char_type>(sign);
      sign = 0;
      if (as.width_) --as.width_;
    }
    as.align_ = ALIGN_RIGHT;
  } else if (spec.align() == ALIGN_DEFAULT) {
    as.align_ = ALIGN_RIGHT;
  }
  if (use_grisu) {
    auto params = internal::gen_digits_params();
    params.fixed = handler.fixed;
    params.num_digits = precision;
    params.trailing_zeros = (precision != 0 && (handler.fixed || !spec.type)) ||
                            spec.has(HASH_FLAG);
    write_padded(as, grisu_writer{sign, buffer, exp, params});
  } else {
    write_padded(as, double_writer{sign, buffer});
  }
}

// Reports a system error without throwing an exception.
// Can be used to report errors from destructors.
FMT_API void report_system_error(int error_code,
                                 string_view message) FMT_NOEXCEPT;

#if FMT_USE_WINDOWS_H

/** A Windows error. */
class windows_error : public system_error {
 private:
  FMT_API void init(int error_code, string_view format_str, format_args args);

 public:
  /**
   \rst
   Constructs a :class:`fmt::windows_error` object with the description
   of the form

   .. parsed-literal::
     *<message>*: *<system-message>*

   where *<message>* is the formatted message and *<system-message>* is the
   system message corresponding to the error code.
   *error_code* is a Windows error code as given by ``GetLastError``.
   If *error_code* is not a valid error code such as -1, the system message
   will look like "error -1".

   **Example**::

     // This throws a windows_error with the description
     //   cannot open file 'madeup': The system cannot find the file specified.
     // or similar (system message may vary).
     const char *filename = "madeup";
     LPOFSTRUCT of = LPOFSTRUCT();
     HFILE file = OpenFile(filename, &of, OF_READ);
     if (file == HFILE_ERROR) {
       throw fmt::windows_error(GetLastError(),
                                "cannot open file '{}'", filename);
     }
   \endrst
  */
  template <typename... Args>
  windows_error(int error_code, string_view message, const Args&... args) {
    init(error_code, message, make_format_args(args...));
  }
};

// Reports a Windows error without throwing an exception.
// Can be used to report errors from destructors.
FMT_API void report_windows_error(int error_code,
                                  string_view message) FMT_NOEXCEPT;

#endif

/** Fast integer formatter. */
class format_int {
 private:
  // Buffer should be large enough to hold all digits (digits10 + 1),
  // a sign and a null character.
  enum { BUFFER_SIZE = std::numeric_limits<unsigned long long>::digits10 + 3 };
  mutable char buffer_[BUFFER_SIZE];
  char* str_;

  // Formats value in reverse and returns a pointer to the beginning.
  char* format_decimal(unsigned long long value) {
    char* ptr = buffer_ + (BUFFER_SIZE - 1);  // Parens to workaround MSVC bug.
    while (value >= 100) {
      // Integer division is slow so do it for a group of two digits instead
      // of for every digit. The idea comes from the talk by Alexandrescu
      // "Three Optimization Tips for C++". See speed-test for a comparison.
      unsigned index = static_cast<unsigned>((value % 100) * 2);
      value /= 100;
      *--ptr = internal::data::DIGITS[index + 1];
      *--ptr = internal::data::DIGITS[index];
    }
    if (value < 10) {
      *--ptr = static_cast<char>('0' + value);
      return ptr;
    }
    unsigned index = static_cast<unsigned>(value * 2);
    *--ptr = internal::data::DIGITS[index + 1];
    *--ptr = internal::data::DIGITS[index];
    return ptr;
  }

  void format_signed(long long value) {
    unsigned long long abs_value = static_cast<unsigned long long>(value);
    bool negative = value < 0;
    if (negative) abs_value = 0 - abs_value;
    str_ = format_decimal(abs_value);
    if (negative) *--str_ = '-';
  }

 public:
  explicit format_int(int value) { format_signed(value); }
  explicit format_int(long value) { format_signed(value); }
  explicit format_int(long long value) { format_signed(value); }
  explicit format_int(unsigned value) : str_(format_decimal(value)) {}
  explicit format_int(unsigned long value) : str_(format_decimal(value)) {}
  explicit format_int(unsigned long long value) : str_(format_decimal(value)) {}

  /** Returns the number of characters written to the output buffer. */
  std::size_t size() const {
    return internal::to_unsigned(buffer_ - str_ + BUFFER_SIZE - 1);
  }

  /**
    Returns a pointer to the output buffer content. No terminating null
    character is appended.
   */
  const char* data() const { return str_; }

  /**
    Returns a pointer to the output buffer content with terminating null
    character appended.
   */
  const char* c_str() const {
    buffer_[BUFFER_SIZE - 1] = '\0';
    return str_;
  }

  /**
    \rst
    Returns the content of the output buffer as an ``std::string``.
    \endrst
   */
  std::string str() const { return std::string(str_, size()); }
};

// A formatter specialization for the core types corresponding to internal::type
// constants.
template <typename T, typename Char>
struct formatter<T, Char,
                 enable_if_t<internal::type_constant<T, Char>::value !=
                             internal::custom_type>> {
  FMT_CONSTEXPR formatter() : format_str_(nullptr) {}

  // Parses format specifiers stopping either at the end of the range or at the
  // terminating '}'.
  template <typename ParseContext>
  FMT_CONSTEXPR auto parse(ParseContext& ctx) -> decltype(ctx.begin()) {
    format_str_ = ctx.begin();
    using handler_type = internal::dynamic_specs_handler<ParseContext>;
    auto type = internal::type_constant<T, Char>::value;
    internal::specs_checker<handler_type> handler(handler_type(specs_, ctx),
                                                  type);
    auto it = parse_format_specs(ctx.begin(), ctx.end(), handler);
    auto eh = ctx.error_handler();
    switch (type) {
    case internal::none_type:
    case internal::named_arg_type:
      FMT_ASSERT(false, "invalid argument type");
      break;
    case internal::int_type:
    case internal::uint_type:
    case internal::long_long_type:
    case internal::ulong_long_type:
    case internal::bool_type:
      handle_int_type_spec(specs_.type,
                           internal::int_type_checker<decltype(eh)>(eh));
      break;
    case internal::char_type:
      handle_char_specs(
          &specs_, internal::char_specs_checker<decltype(eh)>(specs_.type, eh));
      break;
    case internal::double_type:
    case internal::long_double_type:
      handle_float_type_spec(specs_.type,
                             internal::float_type_checker<decltype(eh)>(eh));
      break;
    case internal::cstring_type:
      internal::handle_cstring_type_spec(
          specs_.type, internal::cstring_type_checker<decltype(eh)>(eh));
      break;
    case internal::string_type:
      internal::check_string_type_spec(specs_.type, eh);
      break;
    case internal::pointer_type:
      internal::check_pointer_type_spec(specs_.type, eh);
      break;
    case internal::custom_type:
      // Custom format specifiers should be checked in parse functions of
      // formatter specializations.
      break;
    }
    return it;
  }

  template <typename FormatContext>
  auto format(const T& val, FormatContext& ctx) -> decltype(ctx.out()) {
    internal::handle_dynamic_spec<internal::width_checker>(
        specs_.width_, specs_.width_ref, ctx, format_str_);
    internal::handle_dynamic_spec<internal::precision_checker>(
        specs_.precision, specs_.precision_ref, ctx, format_str_);
    using range_type =
        internal::output_range<typename FormatContext::iterator,
                               typename FormatContext::char_type>;
    return visit_format_arg(arg_formatter<range_type>(ctx, nullptr, &specs_),
                            internal::make_arg<FormatContext>(val));
  }

 private:
  internal::dynamic_format_specs<Char> specs_;
  const Char* format_str_;
};

#define FMT_FORMAT_AS(Type, Base)                                             \
  template <typename Char>                                                    \
  struct formatter<Type, Char> : formatter<Base, Char> {                      \
    template <typename FormatContext>                                         \
    auto format(const Type& val, FormatContext& ctx) -> decltype(ctx.out()) { \
      return formatter<Base, Char>::format(val, ctx);                         \
    }                                                                         \
  }

FMT_FORMAT_AS(signed char, int);
FMT_FORMAT_AS(unsigned char, unsigned);
FMT_FORMAT_AS(short, int);
FMT_FORMAT_AS(unsigned short, unsigned);
FMT_FORMAT_AS(long, long long);
FMT_FORMAT_AS(unsigned long, unsigned long long);
FMT_FORMAT_AS(float, double);
FMT_FORMAT_AS(Char*, const Char*);
FMT_FORMAT_AS(std::basic_string<Char>, basic_string_view<Char>);
FMT_FORMAT_AS(std::nullptr_t, const void*);
FMT_FORMAT_AS(internal::std_string_view<Char>, basic_string_view<Char>);

template <typename Char>
struct formatter<void*, Char> : formatter<const void*, Char> {
  template <typename FormatContext>
  auto format(void* val, FormatContext& ctx) -> decltype(ctx.out()) {
    return formatter<const void*, Char>::format(val, ctx);
  }
};

template <typename Char, size_t N>
struct formatter<Char[N], Char> : formatter<basic_string_view<Char>, Char> {
  template <typename FormatContext>
  auto format(const Char* val, FormatContext& ctx) -> decltype(ctx.out()) {
    return formatter<basic_string_view<Char>, Char>::format(val, ctx);
  }
};

// A formatter for types known only at run time such as variant alternatives.
//
// Usage:
//   typedef std::variant<int, std::string> variant;
//   template <>
//   struct formatter<variant>: dynamic_formatter<> {
//     void format(buffer &buf, const variant &v, context &ctx) {
//       visit([&](const auto &val) { format(buf, val, ctx); }, v);
//     }
//   };
template <typename Char = char> class dynamic_formatter {
 private:
  struct null_handler : internal::error_handler {
    void on_align(alignment) {}
    void on_plus() {}
    void on_minus() {}
    void on_space() {}
    void on_hash() {}
  };

 public:
  template <typename ParseContext>
  auto parse(ParseContext& ctx) -> decltype(ctx.begin()) {
    format_str_ = ctx.begin();
    // Checks are deferred to formatting time when the argument type is known.
    internal::dynamic_specs_handler<ParseContext> handler(specs_, ctx);
    return parse_format_specs(ctx.begin(), ctx.end(), handler);
  }

  template <typename T, typename FormatContext>
  auto format(const T& val, FormatContext& ctx) -> decltype(ctx.out()) {
    handle_specs(ctx);
    internal::specs_checker<null_handler> checker(
        null_handler(),
        internal::mapped_type_constant<T, FormatContext>::value);
    checker.on_align(specs_.align());
    if (specs_.flags == 0)
      ;  // Do nothing.
    else if (specs_.has(SIGN_FLAG))
      specs_.has(PLUS_FLAG) ? checker.on_plus() : checker.on_space();
    else if (specs_.has(MINUS_FLAG))
      checker.on_minus();
    else if (specs_.has(HASH_FLAG))
      checker.on_hash();
    if (specs_.precision != -1) checker.end_precision();
    typedef internal::output_range<typename FormatContext::iterator,
                                   typename FormatContext::char_type>
        range;
    visit_format_arg(arg_formatter<range>(ctx, nullptr, &specs_),
                     internal::make_arg<FormatContext>(val));
    return ctx.out();
  }

 private:
  template <typename Context> void handle_specs(Context& ctx) {
    internal::handle_dynamic_spec<internal::width_checker>(
        specs_.width_, specs_.width_ref, ctx, format_str_);
    internal::handle_dynamic_spec<internal::precision_checker>(
        specs_.precision, specs_.precision_ref, ctx, format_str_);
  }

  internal::dynamic_format_specs<Char> specs_;
  const Char* format_str_;
};

template <typename Range, typename Char>
typename basic_format_context<Range, Char>::format_arg
basic_format_context<Range, Char>::arg(basic_string_view<char_type> name) {
  map_.init(args_);
  format_arg arg = map_.find(name);
  if (arg.type() == internal::none_type) this->on_error("argument not found");
  return arg;
}

template <typename Char, typename ErrorHandler>
FMT_CONSTEXPR void advance_to(basic_parse_context<Char, ErrorHandler>& ctx,
                              const Char* p) {
  ctx.advance_to(ctx.begin() + (p - &*ctx.begin()));
}

template <typename ArgFormatter, typename Char, typename Context>
struct format_handler : internal::error_handler {
  typedef typename ArgFormatter::range range;

  format_handler(range r, basic_string_view<Char> str,
                 basic_format_args<Context> format_args,
                 internal::locale_ref loc)
      : parse_context(str), context(r.begin(), format_args, loc) {}

  void on_text(const Char* begin, const Char* end) {
    auto size = internal::to_unsigned(end - begin);
    auto out = context.out();
    auto&& it = internal::reserve(out, size);
    it = std::copy_n(begin, size, it);
    context.advance_to(out);
  }

  void get_arg(unsigned id) { arg = internal::get_arg(context, id); }

  void on_arg_id() { get_arg(parse_context.next_arg_id()); }
  void on_arg_id(unsigned id) {
    parse_context.check_arg_id(id);
    get_arg(id);
  }
  void on_arg_id(basic_string_view<Char> id) { arg = context.arg(id); }

  void on_replacement_field(const Char* p) {
    advance_to(parse_context, p);
    internal::custom_formatter<Context> f(parse_context, context);
    if (!visit_format_arg(f, arg))
      context.advance_to(
          visit_format_arg(ArgFormatter(context, &parse_context), arg));
  }

  const Char* on_format_specs(const Char* begin, const Char* end) {
    advance_to(parse_context, begin);
    internal::custom_formatter<Context> f(parse_context, context);
    if (visit_format_arg(f, arg)) return parse_context.begin();
    basic_format_specs<Char> specs;
    using internal::specs_handler;
    typedef basic_parse_context<Char> parse_context_t;
    internal::specs_checker<specs_handler<parse_context_t, Context>> handler(
        specs_handler<parse_context_t, Context>(specs, parse_context, context),
        arg.type());
    begin = parse_format_specs(begin, end, handler);
    if (begin == end || *begin != '}') on_error("missing '}' in format string");
    advance_to(parse_context, begin);
    context.advance_to(
        visit_format_arg(ArgFormatter(context, &parse_context, &specs), arg));
    return begin;
  }

  basic_parse_context<Char> parse_context;
  Context context;
  basic_format_arg<Context> arg;
};

/** Formats arguments and writes the output to the range. */
template <typename ArgFormatter, typename Char, typename Context>
typename Context::iterator vformat_to(
    typename ArgFormatter::range out, basic_string_view<Char> format_str,
    basic_format_args<Context> args,
    internal::locale_ref loc = internal::locale_ref()) {
  format_handler<ArgFormatter, Char, Context> h(out, format_str, args, loc);
  internal::parse_format_string<false>(format_str, h);
  return h.context.out();
}

// Casts ``p`` to ``const void*`` for pointer formatting.
// Example:
//   auto s = format("{}", ptr(p));
template <typename T> inline const void* ptr(const T* p) { return p; }
template <typename T> inline const void* ptr(const std::unique_ptr<T>& p) {
  return p.get();
}
template <typename T> inline const void* ptr(const std::shared_ptr<T>& p) {
  return p.get();
}

template <typename It, typename Char> struct arg_join {
  It begin;
  It end;
  basic_string_view<Char> sep;
};

template <typename It, typename Char>
struct formatter<arg_join<It, Char>, Char>
    : formatter<typename std::iterator_traits<It>::value_type, Char> {
  template <typename FormatContext>
  auto format(const arg_join<It, Char>& value, FormatContext& ctx)
      -> decltype(ctx.out()) {
    typedef formatter<typename std::iterator_traits<It>::value_type, Char> base;
    auto it = value.begin;
    auto out = ctx.out();
    if (it != value.end) {
      out = base::format(*it++, ctx);
      while (it != value.end) {
        out = std::copy(value.sep.begin(), value.sep.end(), out);
        ctx.advance_to(out);
        out = base::format(*it++, ctx);
      }
    }
    return out;
  }
};

/**
  Returns an object that formats the iterator range `[begin, end)` with elements
  separated by `sep`.
 */
template <typename It>
arg_join<It, char> join(It begin, It end, string_view sep) {
  return {begin, end, sep};
}

template <typename It>
arg_join<It, wchar_t> join(It begin, It end, wstring_view sep) {
  return {begin, end, sep};
}

// gcc 4.4 on join: internal compiler error: in tsubst_copy, at cp/pt.c:10122
#if !FMT_GCC_VERSION || FMT_GCC_VERSION >= 405
/**
  \rst
  Returns an object that formats `range` with elements separated by `sep`.

  **Example**::

    std::vector<int> v = {1, 2, 3};
    fmt::print("{}", fmt::join(v, ", "));
    // Output: "1, 2, 3"
  \endrst
 */
template <typename Range>
arg_join<internal::iterator_t<const Range>, char> join(const Range& range,
                                                       string_view sep) {
  return join(std::begin(range), std::end(range), sep);
}

template <typename Range>
arg_join<internal::iterator_t<const Range>, wchar_t> join(const Range& range,
                                                          wstring_view sep) {
  return join(std::begin(range), std::end(range), sep);
}
#endif

/**
  \rst
  Converts *value* to ``std::string`` using the default format for type *T*.
  It doesn't support user-defined types with custom formatters.

  **Example**::

    #include <fmt/format.h>

    std::string answer = fmt::to_string(42);
  \endrst
 */
template <typename T> inline std::string to_string(const T& value) {
  return format("{}", value);
}

/**
  Converts *value* to ``std::wstring`` using the default format for type *T*.
 */
template <typename T> inline std::wstring to_wstring(const T& value) {
  return format(L"{}", value);
}

template <typename Char, std::size_t SIZE>
std::basic_string<Char> to_string(const basic_memory_buffer<Char, SIZE>& buf) {
  return std::basic_string<Char>(buf.data(), buf.size());
}

template <typename Char>
typename buffer_context<Char>::iterator internal::vformat_to(
    internal::buffer<Char>& buf, basic_string_view<Char> format_str,
    basic_format_args<buffer_context<Char>> args) {
  using range = buffer_range<Char>;
  return vformat_to<arg_formatter<range>>(buf, to_string_view(format_str),
                                          args);
}

template <typename S, typename Char = char_t<S>,
          FMT_ENABLE_IF(internal::is_string<S>::value)>
inline typename buffer_context<Char>::iterator vformat_to(
    internal::buffer<Char>& buf, const S& format_str,
    basic_format_args<buffer_context<Char>> args) {
  return internal::vformat_to(buf, to_string_view(format_str), args);
}

template <typename S, typename... Args, std::size_t SIZE = inline_buffer_size,
          typename Char = enable_if_t<internal::is_string<S>::value, char_t<S>>>
inline typename buffer_context<Char>::iterator format_to(
    basic_memory_buffer<Char, SIZE>& buf, const S& format_str,
    const Args&... args) {
  internal::check_format_string<Args...>(format_str);
  using context = buffer_context<Char>;
  format_arg_store<context, Args...> as{args...};
  return internal::vformat_to(buf, to_string_view(format_str),
                              basic_format_args<context>(as));
}

namespace internal {

// Detect the iterator category of *any* given type in a SFINAE-friendly way.
// Unfortunately, older implementations of std::iterator_traits are not safe
// for use in a SFINAE-context.

// the gist of C++17's void_t magic
template <typename... Ts> struct void_ { typedef void type; };

template <typename T, typename Enable = void>
struct it_category : std::false_type {};

template <typename T> struct it_category<T*> {
  typedef std::random_access_iterator_tag type;
};

template <typename T>
struct it_category<T, typename void_<typename T::iterator_category>::type> {
  typedef typename T::iterator_category type;
};

// Detect if *any* given type models the OutputIterator concept.
template <typename It> class is_output_iterator {
  // Check for mutability because all iterator categories derived from
  // std::input_iterator_tag *may* also meet the requirements of an
  // OutputIterator, thereby falling into the category of 'mutable iterators'
  // [iterator.requirements.general] clause 4.
  // The compiler reveals this property only at the point of *actually
  // dereferencing* the iterator!
  template <typename U>
  static decltype(*(std::declval<U>())) test(std::input_iterator_tag);
  template <typename U> static char& test(std::output_iterator_tag);
  template <typename U> static const char& test(...);

  typedef decltype(test<It>(typename it_category<It>::type{})) type;
  typedef remove_reference_t<type> result;

 public:
  static const bool value = !std::is_const<result>::value;
};
}  // namespace internal

template <typename OutputIt, typename Char = char>
// using format_context_t = basic_format_context<OutputIt, Char>;
struct format_context_t {
  typedef basic_format_context<OutputIt, Char> type;
};

template <typename OutputIt, typename Char = char>
// using format_args_t = basic_format_args<format_context_t<OutputIt, Char>>;
struct format_args_t {
  typedef basic_format_args<typename format_context_t<OutputIt, Char>::type>
      type;
};

template <typename S, typename OutputIt, typename... Args,
          FMT_ENABLE_IF(
              internal::is_output_iterator<OutputIt>::value &&
              !internal::is_contiguous_back_insert_iterator<OutputIt>::value)>
inline OutputIt vformat_to(
    OutputIt out, const S& format_str,
    typename format_args_t<OutputIt, char_t<S>>::type args) {
  typedef internal::output_range<OutputIt, char_t<S>> range;
  return vformat_to<arg_formatter<range>>(range(out),
                                          to_string_view(format_str), args);
}

/**
 \rst
 Formats arguments, writes the result to the output iterator ``out`` and returns
 the iterator past the end of the output range.

 **Example**::

   std::vector<char> out;
   fmt::format_to(std::back_inserter(out), "{}", 42);
 \endrst
 */
template <typename OutputIt, typename S, typename... Args>
inline OutputIt format_to(OutputIt out, const S& format_str,
                          const Args&... args) {
  static_assert(internal::is_output_iterator<OutputIt>::value &&
                    internal::is_string<S>::value,
                "");
  internal::check_format_string<Args...>(format_str);
  typedef typename format_context_t<OutputIt, char_t<S>>::type context;
  format_arg_store<context, Args...> as{args...};
  return vformat_to(out, to_string_view(format_str),
                    basic_format_args<context>(as));
}

template <typename OutputIt> struct format_to_n_result {
  /** Iterator past the end of the output range. */
  OutputIt out;
  /** Total (not truncated) output size. */
  std::size_t size;
};

template <typename OutputIt, typename Char = typename OutputIt::value_type>
struct format_to_n_context
    : format_context_t<fmt::internal::truncating_iterator<OutputIt>, Char> {};

template <typename OutputIt, typename Char = typename OutputIt::value_type>
struct format_to_n_args {
  typedef basic_format_args<typename format_to_n_context<OutputIt, Char>::type>
      type;
};

template <typename OutputIt, typename Char, typename... Args>
inline format_arg_store<typename format_to_n_context<OutputIt, Char>::type,
                        Args...>
make_format_to_n_args(const Args&... args) {
  return format_arg_store<typename format_to_n_context<OutputIt, Char>::type,
                          Args...>(args...);
}

template <typename OutputIt, typename Char, typename... Args,
          FMT_ENABLE_IF(internal::is_output_iterator<OutputIt>::value)>
inline format_to_n_result<OutputIt> vformat_to_n(
    OutputIt out, std::size_t n, basic_string_view<Char> format_str,
    typename format_to_n_args<OutputIt, Char>::type args) {
  typedef internal::truncating_iterator<OutputIt> It;
  auto it = vformat_to(It(out, n), format_str, args);
  return {it.base(), it.count()};
}

/**
 \rst
 Formats arguments, writes up to ``n`` characters of the result to the output
 iterator ``out`` and returns the total output size and the iterator past the
 end of the output range.
 \endrst
 */
template <typename OutputIt, typename S, typename... Args,
          FMT_ENABLE_IF(internal::is_string<S>::value&&
                            internal::is_output_iterator<OutputIt>::value)>
inline format_to_n_result<OutputIt> format_to_n(OutputIt out, std::size_t n,
                                                const S& format_str,
                                                const Args&... args) {
  internal::check_format_string<Args...>(format_str);
  using Char = char_t<S>;
  format_arg_store<typename format_to_n_context<OutputIt, Char>::type, Args...>
  as(args...);
  return vformat_to_n(out, n, to_string_view(format_str),
                      typename format_to_n_args<OutputIt, Char>::type(as));
}

template <typename Char>
inline std::basic_string<Char> internal::vformat(
    basic_string_view<Char> format_str,
    basic_format_args<buffer_context<Char>> args) {
  basic_memory_buffer<Char> buffer;
  internal::vformat_to(buffer, format_str, args);
  return fmt::to_string(buffer);
}

/**
  Returns the number of characters in the output of
  ``format(format_str, args...)``.
 */
template <typename... Args>
inline std::size_t formatted_size(string_view format_str, const Args&... args) {
  auto it = format_to(internal::counting_iterator<char>(), format_str, args...);
  return it.count();
}

#if FMT_USE_USER_DEFINED_LITERALS
namespace internal {

#  if FMT_USE_UDL_TEMPLATE
template <typename Char, Char... CHARS> class udl_formatter {
 public:
  template <typename... Args>
  std::basic_string<Char> operator()(const Args&... args) const {
    FMT_CONSTEXPR_DECL Char s[] = {CHARS..., '\0'};
    FMT_CONSTEXPR_DECL bool invalid_format =
        do_check_format_string<Char, error_handler, Args...>(
            basic_string_view<Char>(s, sizeof...(CHARS)));
    (void)invalid_format;
    return format(s, args...);
  }
};
#  else
template <typename Char> struct udl_formatter {
  const Char* str;

  template <typename... Args>
  auto operator()(Args&&... args) const
      -> decltype(format(str, std::forward<Args>(args)...)) {
    return format(str, std::forward<Args>(args)...);
  }
};
#  endif  // FMT_USE_UDL_TEMPLATE

template <typename Char> struct udl_arg {
  const Char* str;

  template <typename T> named_arg<T, Char> operator=(T&& value) const {
    return {str, std::forward<T>(value)};
  }
};

}  // namespace internal

inline namespace literals {
#  if FMT_USE_UDL_TEMPLATE
#    pragma GCC diagnostic push
#    if FMT_CLANG_VERSION
#      pragma GCC diagnostic ignored "-Wgnu-string-literal-operator-template"
#    endif
template <typename Char, Char... CHARS>
FMT_CONSTEXPR internal::udl_formatter<Char, CHARS...> operator""_format() {
  return {};
}
#    pragma GCC diagnostic pop
#  else
/**
  \rst
  User-defined literal equivalent of :func:`fmt::format`.

  **Example**::

    using namespace fmt::literals;
    std::string message = "The answer is {}"_format(42);
  \endrst
 */
inline internal::udl_formatter<char> operator"" _format(const char* s,
                                                        std::size_t) {
  return {s};
}
inline internal::udl_formatter<wchar_t> operator"" _format(const wchar_t* s,
                                                           std::size_t) {
  return {s};
}
#  endif  // FMT_USE_UDL_TEMPLATE

/**
  \rst
  User-defined literal equivalent of :func:`fmt::arg`.

  **Example**::

    using namespace fmt::literals;
    fmt::print("Elapsed time: {s:.2f} seconds", "s"_a=1.23);
  \endrst
 */
inline internal::udl_arg<char> operator"" _a(const char* s, std::size_t) {
  return {s};
}
inline internal::udl_arg<wchar_t> operator"" _a(const wchar_t* s, std::size_t) {
  return {s};
}
}  // namespace literals
#endif  // FMT_USE_USER_DEFINED_LITERALS
FMT_END_NAMESPACE

/**
  \rst
  Constructs a compile-time format string.

  **Example**::

    // A compile-time error because 'd' is an invalid specifier for strings.
    std::string s = format(FMT_STRING("{:d}"), "foo");
  \endrst
 */
#define FMT_STRING(s)                                                     \
  [] {                                                                    \
    struct str : fmt::compile_string {                                    \
      typedef typename std::remove_cv<std::remove_pointer<                \
          typename std::decay<decltype(s)>::type>::type>::type char_type; \
      FMT_CONSTEXPR operator fmt::basic_string_view<char_type>() const {  \
        return {s, sizeof(s) / sizeof(char_type) - 1};                    \
      }                                                                   \
    } result;                                                             \
    /* Suppress Qt Creator warning about unused operator. */              \
    (void)static_cast<fmt::basic_string_view<typename str::char_type>>(   \
        result);                                                          \
    return result;                                                        \
  }()

#if defined(FMT_STRING_ALIAS) && FMT_STRING_ALIAS
/**
  \rst
  Constructs a compile-time format string. This macro is disabled by default to
  prevent potential name collisions. To enable it define ``FMT_STRING_ALIAS`` to
  1 before including ``fmt/format.h``.

  **Example**::

    #define FMT_STRING_ALIAS 1
    #include <fmt/format.h>
    // A compile-time error because 'd' is an invalid specifier for strings.
    std::string s = format(fmt("{:d}"), "foo");
  \endrst
 */
#  define fmt(s) FMT_STRING(s)
#endif

#ifdef FMT_HEADER_ONLY
#  define FMT_FUNC inline
#  include "format-inl.h"
#else
#  define FMT_FUNC
#endif

#endif  // FMT_FORMAT_H_
