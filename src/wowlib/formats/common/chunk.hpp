#pragma once

/** @file
    The chunk framework's storage vocabulary: the round-trip bookkeeping every
    chunked entity carries (ChunkExtras), the ChunkedFile mixin that gives an
    entity its read()/write() serialization methods, and the member types the
    serializer dispatches on beyond plain structs and vectors — ChunkBlob and
    Repeated (StringBlock lives in its own header). */

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::formats
{
  /** A chunk the entity does not model, preserved verbatim for round-trip. */
  struct UnknownChunk
  {
    std::uint32_t fourcc = 0;     /**< The id as scanned (memcpy'd host u32). */
    std::vector<std::byte> bytes; /**< The payload, verbatim. */

    bool operator==(const UnknownChunk&) const = default;
  };

  /** One chunk encounter in file order — the write path replays the journal to
      reproduce the original byte layout exactly. */
  struct JournalEntry
  {
    std::uint32_t fourcc = 0;    /**< The id as scanned. */

    /** Declaration index of the member the chunk was read into, or -1 for an
        unknown chunk. */
    std::int32_t member = -1;

    /** Which occurrence of the member this was (repeated chunks), or the index
        into unknown for member == -1. */
    std::uint32_t occurrence = 0;

    bool operator==(const JournalEntry&) const = default;
  };

  /** Round-trip bookkeeping common to every chunked entity: the encounter
      journal, unmodeled chunks, and stray trailing bytes. All preserved so a
      read-then-write reproduces the original file byte for byte. */
  struct ChunkExtras
  {
    [[=welder::mark::exclude]] std::vector<JournalEntry> journal;
    [[=welder::mark::exclude]] std::vector<UnknownChunk> unknown;
    [[=welder::mark::exclude]] std::vector<std::byte> trailing;

    bool operator==(const ChunkExtras&) const = default;
  };

  /** The serialization face of a chunked entity, mixed in CRTP-style: an
      entity `struct E : ChunkedFile<E>` carries the ChunkExtras bookkeeping
      and gains the read()/write() methods every file representation shares.
      Method definitions live in serializer.hpp (the engine they drive), so an
      entity's translation unit includes that header.
      @tparam Derived the entity itself (the CRTP pattern). */
  template <typename Derived>
  struct ChunkedFile : ChunkExtras
  {
    [[=welder::doc("Deserialize file bytes into this entity, replacing its "
                   "contents. Unmodeled chunks are preserved so an unmodified "
                   "entity rewrites byte-for-byte.")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the file bytes")]]);

    [[nodiscard]]
    [[=welder::doc("Serialize this entity."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const;

    bool operator==(const ChunkedFile&) const = default;
  };

  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc("An unparsed chunk payload, preserved verbatim for round-trip. "
                 "Backs chunks wowlib keeps opaque — offset-based (MOTA, MDDL) "
                 "or undocumented (MPVD, MOMX).")
  ]] ChunkBlob
  {
    [[=welder::mark::exclude]] std::vector<std::byte> bytes;

    /** Replace the payload with @a payload (the serializer's read hook).
        @param payload the chunk payload bytes.
        @return nothing; storing raw bytes cannot fail. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> payload)
    {
      bytes.assign(payload.begin(), payload.end());
      return {};
    }

    /** Append the payload to @a out (the serializer's write hook).
        @param out the destination buffer (appended, not cleared).
        @return nothing; emitting raw bytes cannot fail. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const
    {
      out.insert(out.end(), bytes.begin(), bytes.end());
      return {};
    }

    [[nodiscard]]
    [[=welder::getter, =welder::doc("Whether the payload holds any bytes.")]]
    bool empty() const
    {
      return bytes.empty();
    }

    [[=welder::getter, =welder::doc("The payload size in bytes.")]]
    std::size_t size() const
    {
      return bytes.size();
    }

    bool operator==(const ChunkBlob&) const = default;
  };

  /** Storage for a chunk that may appear up to N times in one entity (MOTV
      texcoord sets, MOCV vertex-color layers). Slots fill in encounter order.
      @tparam T the per-occurrence payload type.
      @tparam N the maximum occurrence count (matches the member's `repeats(N)`
                annotation). */
  template <typename T, std::size_t N>
  class Repeated
  {
  public:
    /** Claim the next slot (the serializer's per-encounter hook).
        @return the slot to read into, or nullptr if all N are taken. */
    T* push()
    {
      if (count_ == N)
        return nullptr;
      return &slots_[count_++];
    }

    /** @return the number of filled slots. */
    [[nodiscard]] std::size_t size() const { return count_; }

    /** @return whether no slot is filled. */
    [[nodiscard]] bool empty() const { return count_ == 0; }

    /** @return the slot capacity N. */
    [[nodiscard]] static constexpr std::size_t capacity() { return N; }

    /** @param i a filled-slot index, < size().
        @return the i-th filled slot. */
    [[nodiscard]] T& operator[](std::size_t i) { return slots_[i]; }

    /** @copydoc operator[](std::size_t) */
    [[nodiscard]] const T& operator[](std::size_t i) const { return slots_[i]; }

    /** @return the start of the filled slots (range-for support). */
    [[nodiscard]] T* begin() { return slots_.data(); }

    /** @return past-the-end of the filled slots. */
    [[nodiscard]] T* end() { return slots_.data() + count_; }

    /** @copydoc begin() */
    [[nodiscard]] const T* begin() const { return slots_.data(); }

    /** @copydoc end() */
    [[nodiscard]] const T* end() const { return slots_.data() + count_; }

  private:
    std::array<T, N> slots_{};  /**< The slot storage; the first count_ are live. */
    std::size_t count_ = 0;     /**< Number of filled slots. */
  };
}
