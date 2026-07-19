#pragma once

/** @file
    The chunk annotation vocabulary format entities declare their wire mapping
    with. Follows welder's pattern: structural `*_spec` payloads in `detail`,
    consteval factories / inline constants as the user-facing spelling.

    Usage (a chunked entity):
    @code
    template <ClientVersion V>
    struct WmoRoot : chunk_extras
    {
      static constexpr ClientVersion version = V;
      [[=chunk("MVER")]]                                   std::uint32_t mver = 17;
      [[=chunk("MOTX"), =until(wmo_fdid_refs), =optional]] string_block textures;
      [[=chunk("MOGP"), =container]]                       WmoGroupBody<V> body;
    };
    @endcode

    Annotations appear only on entity primary-template members, with
    non-dependent arguments (namespace-scope ClientVersion constants) — wire
    structs carry none. The serializer and the bindings read the same specs, so
    version activity has a single source of truth. */

#include <cstdint>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/chunk/fourcc.hpp>

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
      member must be a `repeated<T, max>`.
      @param max the maximum occurrence count. */
  consteval detail::repeats_spec repeats(std::uint32_t max) { return {max}; }
}
