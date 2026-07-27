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
        =since(builds::BfA_TidesOfVengeance),
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
    version the chunk appeared/vanished at, as wowdev.wiki documents it, spelled
    through the named build constants of core/client_builds.hpp) — wire
    structs carry none. The serializer and the bindings read the same specs, so
    version activity has a single source of truth. */

#include <meta>

#include <cstdint>
#include <string_view>

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

    /** Stored form of `repeating`: the chunk appears once per element, any
        number of times (WDL MARE tiles, _mpv PVMI/PVPD/PVBD groups). */
    struct repeating_spec
    {
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

    /** Stored form of `offset_after` (offset entities): the trait-base member's
        positional layout position — right after the named own member of the
        entity. The name is interned via define_static_string (annotations must
        be structural; std::string_view is not). */
    struct offset_after_spec
    {
      const char* name;

      constexpr std::string_view view() const { return name; }
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

  /** Mark a chunk that appears once PER ELEMENT of a `std::vector<Element>`
      member, any number of times: each encounter appends one element (whole
      payload -> element), and each element writes back as its own chunk —
      unlike a plain vector member, whose single chunk payload is the whole
      array. The per-tile WDL chunks (MARE/MAHO) and the repeated _mpv groups
      (PVMI/PVPD/PVBD) are the motivating cases. Interleaving with other
      repeating chunks round-trips through the journal; fresh entities emit a
      member's elements consecutively unless the entity resequences its
      journal (see write_entity's resequenced_journal hook). */
  inline constexpr detail::repeating_spec repeating{};

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

  /** Anchor a version-trait member at its positional layout position: the offset
      serializer walks the entity's OWN members in declaration order and splices
      each trait-base member right after the own member named here. Required on
      every member an offset entity inherits from a conditionally-inherited
      trait base — base flattening is by-base, never the interleaved layout
      order. Note this is about correct positional READING of the flat MD20
      layout, not about byte-perfect writes (offset formats have none): a field
      read at the wrong position misaligns every offset after it.
      @param name the own member this one is laid out after. */
  consteval detail::offset_after_spec offset_after(std::string_view name)
  {
    return {std::define_static_string(name)};
  }
}
