#pragma once

/** @file
    The offset-format storage vocabulary: the OffsetFile mixin that gives an
    offset-based entity (M2 and its satellite files) its read()/write()
    serialization methods, and the I/O contexts that route per-sequence data
    to/from external buffers (.anim files).

    An offset entity is the in-memory face of a wire layout made of inline
    scalars and `M2Array{count, offset}` references — the M2 family's format,
    as opposed to the fourcc+size chunk streams ChunkedFile covers. Members
    store `std::vector<T>`/`std::string`; the wire M2Array never surfaces as a
    user type. Unlike the chunk framework there is NO byte-perfect round-trip
    guarantee: writes always produce wowlib's canonical relayout (user decision
    2026-07-24, see .claude/context/m2-architecture.md), and the tested
    guarantee is semantic — an entity written and re-read compares equal. */

#include <cstddef>
#include <functional>
#include <span>

#include <welder/vocabulary.hpp>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>

namespace wowlib::formats
{
  /** The non-template marker base of every offset entity (what the OffsetEntity
      concept detects). Carries no state — offset entities have no round-trip
      bookkeeping to store. */
  struct OffsetBase
  {
    // excluded: the parameter type is this unwelded base, not the entity
    [[=welder::mark::exclude]]
    bool operator==(const OffsetBase&) const = default;
  };

  /** Resolution of `sequence_data` members while reading: where each outer
      element's nested data lives. Without a context (or with an empty
      function) everything is inline in the entity's own buffer. */
  struct OffsetReadContext
  {
    /** The base span the inner arrays of outer element @a i resolve their
        offsets against — the matching .anim file's bytes for an M2 sequence
        stored externally, or the entity's own buffer for an inline one.
        Returning an empty span skips the element (its vectors stay empty):
        the graceful path for a missing .anim file. */
    std::function<std::span<const std::byte>(std::size_t i)> sequence_base;
  };

  /** Resolution of `sequence_data` members while writing: which buffer each
      outer element's nested data blocks are appended to (offsets recorded
      relative to that buffer). Without a context (or a nullptr result) the
      data is written inline after the entity's own image. */
  struct OffsetWriteContext
  {
    /** The destination buffer for outer element @a i's nested data — the
        .anim file buffer being assembled for an external M2 sequence, or
        nullptr for inline. */
    std::function<FileBuffer*(std::size_t i)> sequence_sink;
  };

  /** The serialization face of an offset entity, mixed in CRTP-style: an
      entity `struct E : OffsetFile<E>` gains the read()/write() methods.
      Method definitions live in offset_serializer.hpp (the engine they
      drive), so an entity's translation unit includes that header.

      The `read(span)` / `write(FileBuffer&)` / `empty()` trio deliberately
      matches the chunk framework's SelfSerializing concept, so an offset
      entity can sit directly behind a chunk member (the Legion+ MD21 chunk
      carries the whole MD20 image as its payload).
      @tparam Derived the entity itself (the CRTP pattern). */
  template <typename Derived>
  struct OffsetFile : OffsetBase
  {
    [[=welder::doc("Deserialize file bytes into this entity, replacing its "
                   "contents. Offsets resolve against the given buffer; "
                   "sequence-gated data is read inline.")]]
    Result<void> read(std::span<const std::byte> data
                      [[=welder::doc("the file (or containing-chunk payload) bytes")]]);

    /** Deserialize @a data with external sequence data resolved through
        @a ctx (the M2 .anim baking path).
        @param data the file (or containing-chunk payload) bytes.
        @param ctx  per-sequence base resolution for `sequence_data` members.
        @return nothing, or the first structural error. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> data, const OffsetReadContext& ctx);

    [[nodiscard]]
    [[=welder::doc("Serialize this entity in wowlib's canonical layout (an "
                   "offset format has no byte-perfect round-trip guarantee; "
                   "a written entity re-reads equal instead)."),
      =welder::returns("the file bytes")]]
    Result<FileBuffer> write() const;

    /** Serialize with external sequence data routed through @a ctx (the M2
        .anim splitting path).
        @param ctx per-sequence sink resolution for `sequence_data` members.
        @return the file bytes, or the first error. */
    [[=welder::mark::exclude]]
    Result<FileBuffer> write(const OffsetWriteContext& ctx) const;

    /** Append this entity's serialized image to @a out (the chunk
        serializer's SelfSerializing write hook — offsets are relative to the
        start of the appended image).
        @param out the destination buffer (appended, not cleared).
        @return nothing, or the first error. */
    [[=welder::mark::exclude]]
    Result<void> write(FileBuffer& out) const;

    /** Never empty: an offset entity always has a header image worth writing
        (the SelfSerializing engagement hook — an MD21-style carrier chunk is
        always emitted). */
    [[=welder::mark::exclude]]
    bool empty() const
    {
      return false;
    }

    [[=welder::mark::exclude]]
    bool operator==(const OffsetFile&) const = default;
  };
}
