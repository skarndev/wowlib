#pragma once

/** @file
    StringBlock — the decoded representation of a chunk of zero-terminated
    strings (MOTX, MOGN, MODN, ...), keyed by the byte offsets other chunks
    reference entries with. */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::formats
{
  /** A chunk of zero-terminated strings, stored decoded.

      Client files carry alignment padding and stray zero bytes between
      strings, and offsets stored in other chunks (MOMT texture fields, MOGI
      name offsets, MODD name indices) point into the raw blob — so each
      parsed string is kept with the byte offset it occupied, and the original
      blob size is remembered. Blobifying happens only at serialization:
      write() lays the strings back down at their recorded offsets over a
      zero-filled buffer, which reproduces any client blob exactly (every
      non-zero byte belongs to some entry by construction, and the padding is
      zeros by definition). The mutation API appends past the current end; it
      never rewrites existing offsets, so stored references stay valid. */
  class
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A chunk of zero-terminated strings (texture and model filenames, group
        names), decoded into (offset, value) entries. Other chunks reference
        entries by their byte offset in the on-disk blob; adding appends, and
        existing offsets never move.)")
  ]]
  StringBlock
  {
  public:
    /** One decoded string and the blob offset it lives at — the value other
        chunks store to reference it. */
    struct
    [[=welder::doc("One decoded string and the blob byte offset other chunks "
                   "reference it by.")]]
    Entry
    {
      [[=welder::doc("Byte offset of the string in the on-disk blob.")]]
      std::uint32_t offset = 0;

      [[=welder::doc("The string, without its terminator.")]]
      std::string value;
    };

    StringBlock() = default;

    /** Decode a chunk payload (the serializer's read hook): every run of
        non-zero bytes becomes an entry at its offset; zero bytes between runs
        are padding and are reproduced by write().
        @param payload the chunk payload bytes.
        @return nothing; every byte pattern decodes. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> payload)
    {
      entries_.clear();
      blob_size_ = static_cast<std::uint32_t>(payload.size());
      const auto* bytes = reinterpret_cast<const char*>(payload.data());
      for (std::size_t pos = 0; pos < payload.size();)
      {
        if (bytes[pos] == '\0')
        {
          ++pos;
          continue;
        }
        std::size_t end = pos;
        while (end < payload.size() && bytes[end] != '\0')
          ++end;
        entries_.push_back({static_cast<std::uint32_t>(pos), std::string{bytes + pos, end - pos}});
        pos = end + 1;
      }
      return {};
    }

    /** Blobify (the serializer's write hook): append size() zero bytes to
        @a out, then lay each entry's characters down at its offset — the zero
        fill provides the terminators and the padding.
        @param out the destination buffer (appended, not cleared).
        @return nothing; emitting the blob cannot fail. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const
    {
      const std::size_t base = out.size();
      out.resize(base + blob_size_);  // value-initialized: zero fill
      for (const Entry& entry : entries_)
        std::memcpy(out.data() + base + entry.offset, entry.value.data(), entry.value.size());
      return {};
    }

    /** The string at @a offset (a value another chunk stored). Offsets into
        the middle of an entry yield its suffix — some client data references
        shared filename tails that way.
        @param offset byte offset into the on-disk blob.
        @return the string starting at that offset; empty if the offset lands
                on padding, a terminator, or out of range. */
    [[nodiscard]]
    [[=welder::doc("The string at a byte offset another chunk stored; empty if "
                   "the offset lands on padding or out of range."),
      =welder::returns("the referenced string (a mid-entry offset yields the "
                       "suffix)")]]
    std::string_view at(std::uint32_t offset
                        [[=welder::doc("byte offset into the on-disk blob")]]) const
    {
      // entries are ordered by offset: find the last one starting at or before
      const auto after = std::ranges::upper_bound(entries_, offset, {}, &Entry::offset);
      if (after == entries_.begin())
        return {};
      const Entry& entry = *std::prev(after);
      const std::uint32_t delta = offset - entry.offset;
      if (delta >= entry.value.size())
        return {};
      return std::string_view{entry.value}.substr(delta);
    }

    /** Append @a string as a new entry after the current blob end.
        @param string the string to add; must not contain zero bytes (they
                      would terminate it early on disk).
        @return the offset the string starts at — the value to store in
                referencing chunks. */
    [[=welder::doc("Append a string; existing offsets never move."),
      =welder::returns("the offset the new string starts at - the value to "
                       "store in referencing chunks")]]
    std::uint32_t add(std::string_view string
                      [[=welder::doc("the string to append (no embedded zero "
                                     "bytes)")]])
    {
      const std::uint32_t offset = blob_size_;
      entries_.push_back({offset, std::string{string}});
      blob_size_ += static_cast<std::uint32_t>(string.size()) + 1;
      return offset;
    }

    /** The decoded entries in blob order.
        @return (offset, value) entries; padding runs are not represented. */
    [[nodiscard]]
    [[=welder::doc("The decoded entries, in blob order."),
      =welder::returns("the (offset, value) entries")]]
    const std::vector<Entry>& entries() const
    {
      return entries_;
    }

    [[nodiscard]]
    [[=welder::getter, =welder::doc("Whether the on-disk blob holds any bytes.")]]
    bool empty() const
    {
      return blob_size_ == 0;
    }

    [[=welder::getter,
      =welder::doc("The on-disk blob size in bytes, trailing padding included.")]]
    std::size_t size() const
    {
      return blob_size_;
    }

  private:
    std::vector<Entry> entries_;  /**< Decoded strings, ordered by offset. */
    std::uint32_t blob_size_ = 0; /**< On-disk blob size, padding included. */
  };
}
