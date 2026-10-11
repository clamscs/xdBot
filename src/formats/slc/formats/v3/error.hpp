#ifndef _SLC_V3_ERROR_HPP
#define _SLC_V3_ERROR_HPP

#include "../../util.hpp"
#include "../../expected.hpp"
#include <string>

SLC_NS_BEGIN

namespace v3 {

#define TRY(failable)                                                          \
  ({                                                                           \
    auto result = (failable);                                                  \
    if (!result)                                                               \
      return slc::compat::unexpected{result.error()};                                  \
    *result;                                                                   \
  })

struct Error {
  // Concise error description.
  std::string m_message;

  Error(std::string msg) : m_message(msg) {}
  // std::string is constructible from const char*
  Error(const char *msg) : m_message(msg) {}
};

template <typename T = void> using Result = slc::compat::expected<T, Error>;

/**
 * Read a value from a stream, failing if the stream cannot supply all of it.
 *
 * `what` names the field being read and is used to build the error message.
 */
template <typename T>
Result<T> readChecked(std::istream &in, const char *what) {
  T value{};

  if (!util::binReadInto(in, value)) {
    return slc::compat::unexpected(
        Error(std::string("unexpected end of stream while reading ") + what));
  }

  return value;
}

} // namespace v3

SLC_NS_END

#endif
