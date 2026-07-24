#pragma once

/** @file
    The chunk annotation vocabulary format entities declare their wire mapping
    with. Follows welder's pattern: structural `*_spec` payloads in `detail`,
    consteval factories / inline constants as the user-facing spelling.

    Usage (a chunked entity). The canonical order is: chunk() first; then the
    format annotations (since/until, optional/header/container, repeats); then
    welder's annotations, with welder::doc always last (a raw string literal when
    it spans several lines):
    @code
    template <ClientVersion V>
    struct WMORoot : ChunkedFile<WMORoot<V>>
    {
      static constexpr ClientVersion version = V;

      [[=chunk("MVER")]]
      std::uint32_t mver = 17;

      [[
        =chunk("MODI"),
        =since(ClientVersion{8, 1, 0, 27826}),
        =formats::optional,
        =welder::mark::no_reassign,
        =welder::doc("Doodad FileDataIDs (MODI, 8.1+).")]]
      std::vector<std::uint32_t> doodad_fdids;

      [[=chunk("MOGP"), =container]]
      WMOGroupBody<V> body;
    };
    @endcode

    Annotations appear only on entity primary-template members, with
    non-dependent arguments (a member's since()/until() carries the exact client
    version the chunk appeared/vanished at, as wowdev.wiki documents it) — wire
    structs carry none. The serializer and the bindings read the same specs, so
    version activity has a single source of truth. */

#include <cstdint>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/fourcc.hpp>

namespace wowlib::formats
{
  namespace detail
  {
    /** Stored form of a `chunk` annotation: the member's wire identity. */
    struct chunk_spec
    {
      std::uint32_t magic;  /**< Comparison value, see four_cc(). */
      FourCCEndian endian;  /**< Disk layout of the code (for diagnostics). */
    };

    /** Stored form of `since`: member active for entity versions >= v. */
    struct since_spec
    {
      ClientVersion v;
    };

    /** Stored form of `until`: member active for entity versions < v. */
    struct until_spec
    {
      ClientVersion v;
    };

    /** Stored form of `optional`: absence on read is not an error. */
    struct optional_spec
    {
    };

    /** Stored form of `header`: raw leading bytes of a container payload,
        memcpy'd before chunk scanning starts — not a chunk of its own. */
    struct header_spec
    {
    };

    /** Stored form of `container`: the chunk payload is itself a chunk stream;
        the member is a nested chunked entity read recursively. */
    struct container_spec
    {
    };

    /** Stored form of `repeats`: the chunk may appear up to max times. */
    struct repeats_spec
    {
      std::uint32_t max;
    };

    /** Stored form of `sequence_data` (offset entities): the member's nested
        per-element data may live in an external buffer (M2 .anim files) — the
        engine resolves each outer element's base span/sink through the I/O
        context instead of assuming the entity's own buffer. */
    struct sequence_data_spec
    {
    };

    /** Stored form of `gated_by` (offset entities): the member occupies wire
        bytes only when the entity's `global_flags` has any @a mask bit set. */
    struct gated_by_spec
    {
      std::uint32_t mask;
    };
  }

  /** Declare the chunk a member maps to.
      @param cc     the four-character code as on wowdev.wiki, e.g. "MOHD".
      @param endian disk layout of the code; reversed for all pre-Legion-M2 formats.
      @return the annotation payload. */
  consteval detail::chunk_spec chunk(const char (&cc)[5],
                                     FourCCEndian endian = FourCCEndian::reversed)
  {
    return {four_cc(cc, endian), endian};
  }

  /** Restrict a member to entity versions >= @a v (inclusive).
      @param v the first client version the chunk exists in. */
  consteval detail::since_spec since(ClientVersion v) { return {v}; }

  /** Restrict a member to entity versions < @a v (exclusive).
      @param v the first client version the chunk no longer exists in. */
  consteval detail::until_spec until(ClientVersion v) { return {v}; }

  /** Mark a chunk member the format does not require: absence on read is fine.
      Unmarked chunk members are required — absence is a ChunkMissing error. */
  inline constexpr detail::optional_spec optional{};

  /** Mark a member as a container payload's raw header prelude (e.g. the MOGP
      group header): memcpy'd off the payload front before its chunks scan. */
  inline constexpr detail::header_spec header{};

  /** Mark a chunk member whose payload is itself a chunk stream (e.g. MOGP):
      the member type must be a chunked entity and is read recursively. */
  inline constexpr detail::container_spec container{};

  /** Allow a chunk to appear up to @a max times (e.g. MOTV texcoord sets); the
      member must be a `Repeated<T, max>`.
      @param max the maximum occurrence count. */
  consteval detail::repeats_spec repeats(std::uint32_t max) { return {max}; }

  /** Mark an offset-entity member (a nested `std::vector<std::vector<T>>`,
      one inner array per animation sequence) whose inner data may live in an
      external buffer — M2 low-priority sequences store their track data in
      .anim files. The offset I/O contexts resolve each outer element's base
      span (read) / destination buffer (write); without a context the data is
      inline in the entity's own buffer. */
  inline constexpr detail::sequence_data_spec sequence_data{};

  /** Make an offset-entity member's wire presence conditional on the entity's
      `global_flags`: it occupies bytes only when `global_flags & mask` is
      non-zero (M2's textureCombinerCombos behind global flag 0x8). The flags
      member must precede it in wire order.
      @param mask the flag bits that engage the member. */
  consteval detail::gated_by_spec gated_by(std::uint32_t mask) { return {mask}; }
}
