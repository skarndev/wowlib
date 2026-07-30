#pragma once

/** @file
    Little-endian bit streams over record buffers — the primitive the WDC
    family's bitpacked column storage is built on. A "bit offset" here is
    absolute within the record: bit 0 is the least-significant bit of the
    record's first byte, bit 8 the LSB of its second byte, and so on — exactly
    the addressing field_storage_info's `field_offset_bits` uses. */

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wowlib::db::wdc
{
  /** A bounds-guarded little-endian bit reader over a record's byte span. */
  class BitReader
  {
  public:
    /** @param base  the record's first byte.
        @param limit bytes available from @a base (bounds guard). */
    BitReader(const std::byte* base, std::size_t limit) : base_(base), limit_(limit) {}

    /** Read @a bits (<= 64) starting at absolute bit offset @a bit_offset.

        The value may straddle up to 9 bytes (a 7-bit shift plus 64 bits of
        payload), so the load happens in two 8-byte halves that are shifted and
        or-ed back together.
        @param bit_offset the starting bit position within the record.
        @param bits       the field width in bits.
        @return the zero-extended value (0 when the read would overrun). */
    std::uint64_t read(std::size_t bit_offset, std::size_t bits) const
    {
      if (bits == 0 || bits > 64)
        return 0;
      const std::size_t byte = bit_offset / 8;
      const std::size_t shift = bit_offset % 8;
      const std::size_t span_bytes = (shift + bits + 7) / 8;
      if (byte + span_bytes > limit_)
        return 0;
      // Load the touched bytes as two little-endian halves: `lo` holds the
      // first eight, `hi` the ninth when the value straddles that far.
      std::uint64_t lo = 0, hi = 0;
      const std::size_t lo_bytes = span_bytes > 8 ? 8 : span_bytes;
      std::memcpy(&lo, base_ + byte, lo_bytes);
      if (span_bytes > 8)
        std::memcpy(&hi, base_ + byte + 8, span_bytes - 8);
      // Drop the leading bits that belong to the previous field, then splice
      // the high half's contribution in above them.
      std::uint64_t raw = lo >> shift;
      if (shift != 0 && span_bytes > 8)
        raw |= hi << (64 - shift);
      if (bits < 64)
        raw &= (std::uint64_t{1} << bits) - 1;
      return raw;
    }

  private:
    const std::byte* base_;
    std::size_t limit_;
  };

  /** A little-endian bit writer over a record buffer — the encode counterpart
      of BitReader. write() OVERWRITES (sets and clears each bit), so it works
      on a zeroed buffer (canonical encode) and for patching an existing record
      in place alike. */
  class BitWriter
  {
  public:
    /** @param base  the record's first byte.
        @param limit bytes available from @a base (bounds guard). */
    BitWriter(std::byte* base, std::size_t limit) : base_(base), limit_(limit) {}

    /** Write the low @a bits of @a value starting at absolute bit offset
        @a bit_offset, overwriting whatever was there. Bits that would overrun
        the buffer are dropped.
        @param bit_offset the starting bit position within the record.
        @param bits       the field width in bits (<= 64).
        @param value      the value whose low @a bits are written. */
    void write(std::size_t bit_offset, std::size_t bits, std::uint64_t value)
    {
      for (std::size_t i = 0; i < bits; ++i)
      {
        const std::size_t bo = bit_offset + i;
        if (bo / 8 >= limit_)
          return;
        const std::byte mask{static_cast<unsigned char>(1u << (bo % 8))};
        if ((value >> i) & 1)
          base_[bo / 8] |= mask;
        else
          base_[bo / 8] &= ~mask;
      }
    }

  private:
    std::byte* base_;
    std::size_t limit_;
  };
}
