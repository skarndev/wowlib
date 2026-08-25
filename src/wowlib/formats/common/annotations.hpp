#pragma once

/** @file
    The chunk annotation vocabulary format entities declare their binary mapping
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
    through the named build constants of core/client_builds.hpp) — binary
    structs carry none. The serializer and the bindings read the same specs, so
    version activity has a single source of truth. */

#include <meta>

#include <cstdint>
#include <string_view>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/fourcc.hpp>

namespace wowlib::formats {
  namespace detail {
    /** Stored form of a `chunk` annotation: the member's binary identity. */
    struct chunk_spec {
      std::uint32_t magic; /**< Comparison value, see four_cc(). */
      FourCCEndian endian; /**< Disk layout of the code (for diagnostics). */
    };

    /** Stored form of `since`: member active for entity versions >= v. */
    struct since_spec {
      ClientVersion v;
    };

    /** Stored form of `until`: member active for entity versions < v. */
    struct until_spec {
      ClientVersion v;
    };

    /** Stored form of `optional`: absence on read is not an error. */
    struct optional_spec {};

    /** Stored form of `header`: raw leading bytes of a container payload,
        memcpy'd before chunk scanning starts — not a chunk of its own. */
    struct header_spec {};

    /** Stored form of `container`: the chunk payload is itself a chunk stream;
        the member is a nested chunked entity read recursively. */
    struct container_spec {};

    /** Stored form of `repeats`: the chunk may appear up to max times. */
    struct repeats_spec {
      std::uint32_t max;
    };

    /** Stored form of `repeating`: the chunk appears once per element, any
        number of times (WDL MARE tiles, _mpv PVMI/PVPD/PVBD groups). */
    struct repeating_spec {};

    /** Stored form of `sequence_data` (offset entities): the member's nested
        per-element data may live in an external buffer (M2 .anim files) — the
        engine resolves each outer element's base span/sink through the I/O
        context instead of assuming the entity's own buffer. */
    struct sequence_data_spec {};

    /** Stored form of `gated_by` (offset entities): the member occupies binary
        bytes only when the entity's `global_flags` has any @a mask bit set. */
    struct gated_by_spec {
      std::uint32_t mask;
    };

    /** Stored form of `offset_after` (offset entities): the trait-base member's
        positional layout position — right after the named own member of the
        entity. The name is interned via define_static_string (annotations must
        be structural; std::string_view is not). */
    struct offset_after_spec {
      const char* name;

      constexpr std::string_view view() const { return name; }
    };

    // --- validation specs (validate(), never the serializer) ------------------

    /** Stored form of `count_matches`: when the member is engaged, its element
        count times @a scale must equal the named sibling member's count. */
    struct count_matches_spec {
      const char* name; /**< The sibling member (interned, see offset_after_spec). */
      std::uint32_t scale; /**< This member's count is 1/scale of the sibling's. */

      constexpr std::string_view view() const { return name; }
    };

    /** Stored form of `count_multiple_of`: the member's element count must be
        a multiple of @a divisor. */
    struct count_multiple_of_spec {
      std::uint32_t divisor;
    };

    /** Stored form of `count_exactly`: the member's element count is fixed by
        the format (an ADT map chunk's 145 height samples). */
    struct count_exactly_spec {
      std::uint32_t count;
    };

    /** Stored form of `indexes`: every element of the (integral) vector member
        must be a valid index into the named sibling member. */
    struct indexes_spec {
      const char* name; /**< The sibling member the elements index. */

      constexpr std::string_view view() const { return name; }
    };

    /** Stored form of `indexes_optional`: like `indexes`, but the client's
        "no reference" sentinel (a negative value, or the all-ones value of an
        unsigned element) is a legal element. */
    struct indexes_optional_spec {
      const char* name; /**< The sibling member the elements index. */

      constexpr std::string_view view() const { return name; }
    };

    /** Stored form of `indexes_in_root`: every element of the (integral)
        vector member must be a valid index into the named member of the
        ASSEMBLY's root entity. The member's own entity cannot check this — the
        assembly's validate() resolves the target and applies it. */
    struct indexes_in_root_spec {
      const char* name; /**< The root-entity member the elements index. */

      constexpr std::string_view view() const { return name; }
    };

    /** Stored form of `expected_value`: the (integral) data member must hold
        exactly this value (format-version fields). */
    struct expected_value_spec {
      std::uint32_t value;
    };

    /** Stored form of `nonempty`: the member must hold data for the file to be
        meaningful to the client, even though read() tolerates its absence. */
    struct nonempty_spec {};
  }

  /** Declare the chunk a member maps to.
      @param cc     the four-character code as on wowdev.wiki, e.g. "MOHD".
      @param endian disk layout of the code; reversed for all pre-Legion-M2 formats.
      @return the annotation payload. */
  consteval detail::chunk_spec chunk(const char(&cc)[5], FourCCEndian endian = FourCCEndian::reversed) {
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

  /** Make an offset-entity member's binary presence conditional on the entity's
      `global_flags`: it occupies bytes only when `global_flags & mask` is
      non-zero (M2's textureCombinerCombos behind global flag 0x8). The flags
      member must precede it in binary order.
      @param mask the flag bits that engage the member. */
  consteval detail::gated_by_spec gated_by(std::uint32_t mask) {
    return {mask};
  }

  /** Anchor a version-trait member at its positional layout position: the offset
      serializer walks the entity's OWN members in declaration order and splices
      each trait-base member right after the own member named here. Required on
      every member an offset entity inherits from a conditionally-inherited
      trait base — base flattening is by-base, never the interleaved layout
      order. Note this is about correct positional READING of the flat MD20
      layout, not about byte-perfect writes (offset formats have none): a field
      read at the wrong position misaligns every offset after it.
      @param name the own member this one is laid out after. */
  consteval detail::offset_after_spec offset_after(std::string_view name) {
    return {std::define_static_string(name)};
  }

  // --- validation annotations (validate() contracts, never the serializer) ----

  /** Declare a companion-count contract: when this member is engaged
      (non-empty), its element count times @a scale must equal the named
      sibling member's count. Examples: normals `count_matches("vertices")`
      (one normal per vertex); polys `count_matches("indices", 3)` (one
      per-triangle record per three indices). On a Repeated<> member the
      contract applies to every filled slot. The sibling name is checked
      against the entity's members at compile time.
      @param name  the sibling member whose count is the reference.
      @param scale this member's count is 1/scale of the sibling's. */
  consteval detail::count_matches_spec count_matches(std::string_view name, std::uint32_t scale = 1) {
    return {std::define_static_string(name), scale};
  }

  /** Declare a granularity contract: the member's element count must be a
      multiple of @a divisor (triangle index arrays: 3).
      @param divisor the required granularity. */
  consteval detail::count_multiple_of_spec count_multiple_of(std::uint32_t divisor) {
    return {divisor};
  }

  /** Declare a fixed-size contract: when engaged, the member holds exactly
      @a count elements because the format fixes the grid (an ADT map chunk's
      145 height samples, its 4096-byte shadow map). Absence stays legal —
      combine with `nonempty` when the data is also mandatory.
      @param count the required element count. */
  consteval detail::count_exactly_spec count_exactly(std::uint32_t count) {
    return {count};
  }

  /** Declare a referential contract: every element of this (integral) vector
      member is an index into the named sibling member, so each must be less
      than the sibling's element count. The sibling name is checked against the
      entity's members at compile time.
      @param name the sibling member the elements index. */
  consteval detail::indexes_spec indexes(std::string_view name) {
    return {std::define_static_string(name)};
  }

  /** Declare a referential contract that tolerates the "none" sentinel: like
      `indexes`, except an element that is negative (signed lookup) or all-ones
      (an unsigned `-1`, e.g. M2's 0xFFFF) means "no reference" and is skipped.
      The M2 lookup tables are the motivating case — key bones, replacable
      textures and transform lookups all leave unused slots at -1.
      @param name the sibling member the elements index. */
  consteval detail::indexes_optional_spec indexes_optional(std::string_view name) {
    return {std::define_static_string(name)};
  }

  /** Declare a cross-entity referential contract: every element of this
      (integral) vector member is an index into the named member of the
      ASSEMBLY's root entity (a WMO group's light_refs into the root's
      lights). The member's own entity validates nothing for it — the
      assembly's validate() resolves the target and applies the check.
      @param name the root-entity member the elements index. */
  consteval detail::indexes_in_root_spec
  indexes_in_root(std::string_view name) {
    return {std::define_static_string(name)};
  }

  /** Declare an exact-value contract on an integral data member (format
      version fields: WMO MVER is always 17).
      @param value the only valid member value. */
  consteval detail::expected_value_spec expected_value(std::uint32_t value) {
    return {value};
  }

  /** Declare a presence contract: the member must hold data for the file to be
      meaningful to the client, even though read() tolerates its absence (a
      required-content marker for optional-on-read chunks). */
  inline constexpr detail::nonempty_spec nonempty{};
}
