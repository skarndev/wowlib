#pragma once

/** @file
    FourCC chunk identifiers: compile-time conversion of the four-letter codes
    to the host integers chunk scanning compares against, and back for
    diagnostics. */

#include <array>
#include <cstdint>
#include <string>

namespace wowlib::formats {
  /** How a chunk's FourCC characters are laid out on disk. */
  enum class FourCCEndian : std::uint8_t {
    /** The characters are stored reversed: 'MVER' appears in the file as the
        bytes "REVM". The common case — WMO, ADT, WDT, WDL and the M2 wrapper
        magic all use it. */
    Reversed,

    /** The characters are stored as written: 'AFID' appears in the file as the
        bytes "AFID". Used by the Legion+ M2 companion chunk ids. */
    Forward
  };

  /** The host integer a scanned chunk id compares equal to for code @a cc.

      The chunk scanner memcpy's four disk bytes into a host (little-endian)
      std::uint32_t and compares against this value directly — no per-chunk byte
      swapping on either side.
      @param cc     the four-character code as written on wowdev.wiki, e.g. "MVER".
      @param endian how the code is laid out on disk.
      @return the comparison value. */
  constexpr std::uint32_t fourcc(const char (&cc)[5], FourCCEndian endian = FourCCEndian::Reversed) {
    const auto b = [&](std::size_t i) {
      return static_cast<std::uint32_t>(cc[i]);
    };
    return endian == FourCCEndian::Reversed
             ? b(3) | b(2) << 8 | b(1) << 16 | b(0) << 24
             : b(0) | b(1) << 8 | b(2) << 16 | b(3) << 24;
  }

  /** The readable four-character spelling of a scanned chunk id.
      @param fourcc a chunk id as memcpy'd from disk by the scanner.
      @param endian how the code is laid out on disk.
      @return e.g. "MVER"; non-printable bytes are kept verbatim. */
  constexpr std::string fourccToString(std::uint32_t fourcc, FourCCEndian endian = FourCCEndian::Reversed) {
    const auto b = [&](unsigned shift) {
      return static_cast<char>((fourcc >> shift) & 0xFF);
    };
    return endian == FourCCEndian::Reversed
             ? std::string{b(24), b(16), b(8), b(0)}
             : std::string{b(0), b(8), b(16), b(24)};
  }
}
