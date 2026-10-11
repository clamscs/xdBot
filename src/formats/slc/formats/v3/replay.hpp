#ifndef _SLC_V3_REPLAY_HPP
#define _SLC_V3_REPLAY_HPP

#include "formats/slc/formats/v3/atom.hpp"
#include "formats/slc/formats/v3/builtin.hpp"
#include "formats/slc/formats/v3/error.hpp"
#include "formats/slc/formats/v3/metadata.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused"
#include "formats/slc/formats/v3/section.hpp"
#pragma GCC diagnostic pop

#include "formats/slc/util.hpp"

#include <array>
#include <cassert>
#include "formats/slc/expected.hpp"
#include <iostream>

SLC_NS_BEGIN

namespace v3 {

using DefaultRegistry = AtomRegistry<NullAtom, ActionAtom>;

template <typename Registry = DefaultRegistry> class Replay {
private:
  using Self = Replay;

public:
  Metadata m_meta;
  Registry m_atoms;

private:
public:
  static constexpr uint64_t HEADER_SIZE = 8;
  static constexpr std::array<uint8_t, HEADER_SIZE> HEADER = {
      'S', 'L', 'C', '3', 'R', 'P', 'L', 'Y'};

  static constexpr uint8_t FOOTER = 0xCC;

  static constexpr uint16_t META_SIZE = sizeof(Metadata);

  static Result<Self> read(std::istream &in) {
    std::array<uint8_t, HEADER_SIZE> headerBuf{};
    if (!in.read(reinterpret_cast<char *>(headerBuf.data()), HEADER_SIZE)) {
      return slc::compat::unexpected("unexpected end of stream while reading header");
    }

    if (HEADER != headerBuf) {
      return slc::compat::unexpected("invalid header in given container");
    }

    Self replay;

    uint16_t metaSize = TRY(readChecked<uint16_t>(in, "metadata size"));
    if (META_SIZE != metaSize) {
      return slc::compat::unexpected(
          "invalid metadata size, likely outdated or malformed replay");
    }

    // this is assuming metadata is actually correct in the replay
    // checksums will validate metadata but it could still be garbage
    // a way to prevent it would be to checksum the metadata itself
    // but that kinda sucks
    replay.m_meta = TRY(readChecked<Metadata>(in, "metadata"));

    TRY(replay.m_atoms.readAll(in));

    uint8_t footerBuf = TRY(readChecked<uint8_t>(in, "footer"));
    if (FOOTER != footerBuf) {
      return slc::compat::unexpected("invalid footer in given container");
    }

    return replay;
  }

  Result<> write(std::ostream &out) {
    out.write(reinterpret_cast<const char *>(HEADER.data()), HEADER_SIZE);

    util::binWrite(out, META_SIZE);
    util::binWrite(out, m_meta);

    m_atoms.writeAll(out);

    util::binWrite(out, FOOTER);

    return {};
  }
};

} // namespace v3

SLC_NS_END
#endif
