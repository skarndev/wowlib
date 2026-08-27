#pragma once

/** @file
    The _mpv.wdt particulate-volume satellite entity (namespace
    wowlib::formats::wdt::mpv), BfA 8.0.1+: weather particulate volumes. The
    PVMI/PVPD/PVBD chunks repeat as ordered groups (PVMI overrides a previous
    PVPD, PVBD finalizes a group), so all three are repeating members whose
    interleave round-trips through the journal. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>
#include <wowlib/formats/wdt/mpv/chunks/records.hpp>

namespace wowlib::formats::wdt::mpv {
  using namespace wowlib::formats::wdt::mpv::chunks;

  /** The version-agnostic base of every WDTParticulates<V> (welded as
      "WDTParticulates"); bindings-only, like every *Base.
      @see https://wowdev.wiki/WDT#mpv */
  struct [[
      =welder::weld,
      =welder::weld_as("WDTParticulates"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A _mpv.wdt particulate-volume satellite (BfA 8.0.1+), abstract over
        the client version: weather particulate volumes in repeated
        PVMI/PVPD/PVBD groups. Construct a concrete version with
        WDTParticulates.for_version(expansion). See https://wowdev.wiki/WDT.)")
    ]] WDTParticulatesBase {};

  namespace detail {
    /** A _mpv.wdt particulate-volume satellite for one client version (BfA
        8.0.1+). The i-th elements of volumeData / pointGroups /
        boundGroups belong together; the on-disk group interleave
        (PVMI, PVPD, PVBD, PVMI, ...) round-trips through the journal.
        Instantiate through the canonicalizing wdt::mpv::WDTParticulates
        alias, never directly.
        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WDT#mpv */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          A _mpv.wdt particulate-volume satellite for one client version (BfA
          8.0.1+): weather particulate volumes as repeated PVMI/PVPD/PVBD
          groups — the i-th elements of the three lists belong together. Most
          maps ship it empty. See https://wowdev.wiki/WDT.)")
      ]] WDTParticulates : ChunkedFile<WDTParticulates<V>>,
                           WDTParticulatesBase {
      static constexpr ClientVersion Version = V;

      [[
        =chunk("MVER"),
        =welder::doc(
          "The _mpv format version, 1 to 4; the PVMI record size is keyed "
          "on it (0xF5C / 0xFE8 / 0x10D8).")]]
      std::uint32_t mver = 4;

      [[
        =chunk("PVMI"),
        =formats::Optional,
        =formats::Repeating,
        =welder::doc(
          R"(One PVMI payload per volume group. The record layout is keyed
                        on the file's own version payload, not the client build, so
                        it is kept opaque.)")]]
      std::vector<ChunkBlob> volumeData;

      [[
        =chunk("PVPD"),
        =formats::Optional,
        =formats::Repeating,
        =welder::mark::no_reassign,
        =welder::doc("One PVPD point array per volume group.")]]
      std::vector<std::vector<ParticulatePoint>> pointGroups;

      [[
        =chunk("PVBD"),
        =formats::Optional,
        =formats::Repeating,
        =welder::mark::no_reassign,
        =welder::doc("One PVBD bounds array per volume group; reading a PVBD "
          "finalizes the group.")]]
      std::vector<std::vector<ParticulateBounds>> boundGroups;

      /** The canonical chunk-stream order the serializer emits a fresh entity
          in (see writeOrder). Lists every chunk member exactly once; note a
          fresh MULTI-group entity emits each member's elements consecutively
          rather than the client's per-group interleave — entities read from a
          file replay their journal and keep it. */
      static constexpr std::array ChunkOrder = {fourCc("MVER"), fourCc("PVMI"), fourCc("PVPD"), fourCc("PVBD"),};
    };
  }

  /** A _mpv.wdt satellite — the canonicalizing face of
      detail::WDTParticulates: stable since BfA (its record-size changes key
      on the FILE version, not the client), so a single instantiation serves
      every release. */
  template <ClientVersion V> requires(V >= builds::BfA_Beta_26287)
  using WDTParticulates = detail::WDTParticulates<canonicalVersion(V, WdtMpvPivots, WdtMpvVersions)>;
}
