#ifndef SLC_UTIL_HPP
#define SLC_UTIL_HPP

#include <bit>
#include <iostream>
#include <span>

#define SLC_NS_BEGIN namespace slc {
#define SLC_NS_END }

SLC_NS_BEGIN

namespace util {

template <typename T, T Left, T Right>
  requires std::totally_ordered<T>
constexpr bool inRange(T value) {
  static_assert(Left <= Right, "Lhs must be lesser or equal to Rhs");

  return value >= Left && value <= Right;
}

/**
 * Read a value from a stream into `out`.
 *
 * `out` is zeroed first, so bytes the stream could not supply read back as
 * zero rather than as indeterminate values.
 *
 * Returns false if the stream could not supply all `sizeof(T)` bytes; `out`
 * then holds whatever prefix was available, zero-padded.
 */
template <typename T> bool binReadInto(std::istream &s, T &out) {
  out = T{};

  return static_cast<bool>(s.read(reinterpret_cast<char *>(&out), sizeof(T)));
}

/**
 * Read a value from a stream, ignoring whether the read succeeded.
 *
 * A short read yields a zero-padded value. Prefer [`binReadInto`] (or, in v3,
 * `readChecked`) anywhere a truncated stream needs to be diagnosed.
 */
template <typename T> T binRead(std::istream &s) {
  T temp{};

  (void)binReadInto(s, temp);

  return temp;
}

/**
 * Query the total length of a stream, leaving the read position untouched.
 *
 * Returns -1 if the stream cannot be seeked or queried.
 */
inline std::streamoff streamLength(std::istream &s) {
  const auto pos = s.tellg();
  if (pos == -1) {
    return -1;
  }

  if (!s.seekg(0, std::ios::end)) {
    return -1;
  }

  const auto end = s.tellg();

  if (!s.seekg(pos, std::ios::beg)) {
    return -1;
  }

  return end;
}

template <typename T> void binWrite(std::ostream &s, const T &val) {
  s.write(reinterpret_cast<const char *>(&val), sizeof(T));
}

template <typename T>
  requires std::is_integral_v<T>
int exponentOfTwo(T n) {
  return n ? std::min(
                 static_cast<int>((sizeof(T) * 8 - 1 - std::countl_zero(n))),
                 15)
           : 0;
}

template <typename T>
  requires std::is_integral_v<T>
int largestPowerOfTwo(T n) {
  return n ? 1ULL << exponentOfTwo(n) : 0;
}
} // namespace util

SLC_NS_END

#endif // SLC_UTIL_HPP
