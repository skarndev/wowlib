#pragma once

/** @file
    The chunk framework's storage vocabulary: the round-trip bookkeeping every
    chunked entity carries (chunk_extras), and the member types the serializer
    dispatches on beyond plain structs and vectors — string_block, chunk_blob
    and repeated. */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <welder/vocabulary.hpp>

namespace wowlib::formats
{
  /** A chunk the entity does not model, preserved verbatim for round-trip. */
  struct unknown_chunk
  {
    std::uint32_t fourcc = 0;     /**< The id as scanned (memcpy'd host u32). */
    std::vector<std::byte> bytes; /**< The payload, verbatim. */
  };

  /** One chunk encounter in file order — the write path replays the journal to
      reproduce the original byte layout exactly. */
  struct journal_entry
  {
    std::uint32_t fourcc = 0;    /**< The id as scanned. */

    /** Declaration index of the member the chunk was read into, or -1 for an
        unknown chunk. */
    std::int32_t member = -1;

    /** Which occurrence of the member this was (repeated chunks), or the index
        into unknown for member == -1. */
    std::uint32_t occurrence = 0;
  };

  /** Round-trip bookkeeping common to every chunked entity: the encounter
      journal, unmodeled chunks, and stray trailing bytes. All preserved so a
      read-then-write reproduces the original file byte for byte. */
  struct chunk_extras
  {
    [[=welder::mark::exclude]] std::vector<journal_entry> journal;
    [[=welder::mark::exclude]] std::vector<unknown_chunk> unknown;
    [[=welder::mark::exclude]] std::vector<std::byte> trailing;
  };

  /** An opaque chunk payload preserved as raw bytes — the storage for chunks
      whose internal structure wowlib does not model yet (e.g. MLIQ). */
  struct chunk_blob
  {
    std::vector<std::byte> bytes;

    [[nodiscard]] bool empty() const { return bytes.empty(); }
  };

  /** A chunk of zero-terminated strings (MOTX, MOGN, MODN, ...).

      The raw blob is the source of truth — client files carry alignment padding
      and stray zero bytes between strings, and offsets stored in other chunks
      point into the blob — so reads keep it verbatim (byte-perfect round-trip)
      and offset-based lookups stay valid. The mutation API appends and
      re-terminates; it never rewrites existing offsets. */
  class string_block
  {
  public:
    string_block() = default;

    /** The string starting at @a offset (a value another chunk stored).
        @param offset byte offset into the blob.
        @return the zero-terminated string at that offset; empty if out of range. */
    [[nodiscard]] std::string_view at(std::uint32_t offset) const
    {
      if (offset >= blob_.size())
        return {};
      return std::string_view{blob_.data() + offset};
    }

    /** Every non-empty string with its offset, in blob order. Padding runs of
        zero bytes are skipped (they separate, they do not participate).
        @return (offset, string) pairs viewing into the blob. */
    [[nodiscard]] std::vector<std::pair<std::uint32_t, std::string_view>> entries() const
    {
      std::vector<std::pair<std::uint32_t, std::string_view>> out;
      for (std::size_t pos = 0; pos < blob_.size();)
      {
        if (blob_[pos] == '\0')
        {
          ++pos;
          continue;
        }
        const std::string_view s{blob_.data() + pos};
        out.emplace_back(static_cast<std::uint32_t>(pos), s);
        pos += s.size() + 1;
      }
      return out;
    }

    /** Append @a string (plus terminator) to the blob.
        @param string the string to add; embedded zero bytes are not allowed.
        @return the offset the string starts at — the value to store in
                referencing chunks. */
    std::uint32_t add(std::string_view string)
    {
      const auto offset = static_cast<std::uint32_t>(blob_.size());
      blob_.insert(blob_.end(), string.begin(), string.end());
      blob_.push_back('\0');
      return offset;
    }

    [[nodiscard]] bool empty() const { return blob_.empty(); }
    [[nodiscard]] std::size_t size() const { return blob_.size(); }

    /** The verbatim blob — what the chunk payload is on disk. */
    [[nodiscard]] const std::vector<char>& raw() const { return blob_; }
    [[nodiscard]] std::vector<char>& raw() { return blob_; }

  private:
    std::vector<char> blob_;
  };

  /** Storage for a chunk that may appear up to N times in one entity (MOTV
      texcoord sets, MOCV vertex-color layers). Slots fill in encounter order. */
  template <typename T, std::size_t N>
  class repeated
  {
  public:
    /** Claim the next slot.
        @return the slot to read into, or nullptr if all N are taken. */
    T* push()
    {
      if (count_ == N)
        return nullptr;
      return &slots_[count_++];
    }

    [[nodiscard]] std::size_t size() const { return count_; }
    [[nodiscard]] bool empty() const { return count_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() { return N; }

    [[nodiscard]] T& operator[](std::size_t i) { return slots_[i]; }
    [[nodiscard]] const T& operator[](std::size_t i) const { return slots_[i]; }

    [[nodiscard]] T* begin() { return slots_.data(); }
    [[nodiscard]] T* end() { return slots_.data() + count_; }
    [[nodiscard]] const T* begin() const { return slots_.data(); }
    [[nodiscard]] const T* end() const { return slots_.data() + count_; }

  private:
    std::array<T, N> slots_{};
    std::size_t count_ = 0;
  };
}
