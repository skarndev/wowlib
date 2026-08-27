#pragma once

/** @file
    The WDT main-file entity (namespace wowlib::formats::wdt::root): the map
    header, the 64x64 tile table, the per-tile FileDataID table (8.1+) and the
    global-WMO reference of WMO-only maps. Version-gated chunks live in
    conditionally-inherited trait bases (wdt::root::detail); the binary structs
    live in wdt::root::chunks. */

#include <cstdint>
#include <vector>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/chunked_file.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/map_placements.hpp>
#include <wowlib/formats/common/string_block.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>
#include <wowlib/formats/wdt/root/chunks/header.hpp>

namespace wowlib::formats::wdt::root {
  using namespace wowlib::formats::wdt::root::chunks;

  /** The version-agnostic base of every WDTRoot<V> (welded as "WDTRoot").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WDTRoot* classes a common welded supertype, so binding
      users can write version-agnostic code (`isinstance(x, WDTRoot)`, a
      `x: WDTRoot` annotation, `WDTRoot.for_version(expansion)`). It has no role in
      the C++ API, where you use the concrete WDTRoot<V> directly.

      @see https://wowdev.wiki/WDT */
  struct [[
      =welder::weld,
      =welder::weld_as("WDTRoot"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A WDT main file, abstract over the client version. The main file says
        which map tiles exist (and, since 8.1, which FileDataIDs serve them),
        or references the one global WMO of an object-only map. Construct a
        concrete version with WDTRoot.for_version(expansion); the per-version
        WDTRoot* classes are subclasses. See https://wowdev.wiki/WDT.)")
    ]] WDTRootBase {};

  namespace detail {
    // --- version-range trait bases (unwelded) ---------------------------------
    // One struct per availability range; members keep their chunk/since/until/
    // doc/marks (read off the declaring class, so flattening preserves them).

    /** 8.1+ main-file chunks (the FileDataID era). */
    struct Root81 {
      [[
        =chunk("MAID"),
        =since(builds::BfA_TidesOfVengeance_28294),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(Per-tile FileDataIDs (MAID, 8.1.0.28294+): 64 x 64 records in
                        row-major order (y outer, x inner), one per map tile —
                        engaged by the has_maid header flag. WMO-only maps omit
                        it.)")]]
      std::vector<MapFileDataIDs> mapFdids;
    };

    /** 9.0+ main-file chunks. */
    struct Root90 {
      [[
        =chunk("MANM"),
        =since(builds::SL_Alpha_33978),
        =formats::Optional,
        =welder::doc(
          R"(Map anima paths (MANM, 9.0+): the Shadowlands anima stream
                        cables and their node chains. A versioned variable-length
                        layout, kept opaque.)")]]
      ChunkBlob mapAnima;
    };
  }

  namespace detail {
    /** A WDT main file for one client version.

        The main file holds the map-wide header (MPHD), the 64x64 tile table
        (MAIN) saying which tiles have terrain, since 8.1 the per-tile
        FileDataID table (MAID), and — for WMO-only maps — the single global
        map object (MWMO/MODF). Version-gated chunks are inherited from the
        detail:: trait bases active for @a V. Instantiate through the
        canonicalizing wdt::root::WDTRoot alias, never directly.

        @tparam V the client version this layout targets.
        @see https://wowdev.wiki/WDT */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          A WDT main file for one client version: the map header, the 64x64
          tile table (row-major, y outer), the per-tile FileDataIDs (8.1+) and
          the global WMO of object-only maps. An instance read from a client
          file rewrites byte-for-byte until modified. See
          https://wowdev.wiki/WDT.)")
      ]] WDTRoot
      : ChunkedFile<WDTRoot<V>>,
        WDTRootBase,
        Slot<V, builds::BfA_TidesOfVengeance_28294, Root81>,
        Slot<V, builds::SL_Alpha_33978, Root90> {
      static constexpr ClientVersion Version = V;

      [[
        =chunk("MVER"),
        =formats::expectedValue(WdtVersion18),
        =welder::doc("The WDT format version; 18 for every supported client.")]]
      std::uint32_t mver = WdtVersion18;

      [[
        =chunk("MPHD"),
        =welder::doc(
          "The map header (MPHD): flags and, since 8.1, the satellite "
          "FileDataIDs.")]]
      SMMapHeader<V> header{};

      [[
        =chunk("MAIN"),
        =welder::mark::no_reassign,
        =welder::doc(
          R"(The tile table (MAIN): 64 x 64 records in row-major order
                        (y outer, x inner), flagging which tiles have an ADT.)")
      ]]
      std::vector<SMAreaInfo> tiles;

      [[
        =chunk("MWMO"),
        =formats::Optional,
        =welder::doc(
          R"(The global-WMO filename (MWMO) of a WMO-only map, as a string
                        block holding the one zero-terminated path (at most 0x100
                        bytes). Terrain maps of pre-8.1 clients still carry the chunk
                        empty; FileDataID-era terrain maps drop it.)")]]
      StringBlock globalWmoName;

      [[
        =chunk("MODF"),
        =formats::Optional,
        =welder::mark::no_reassign,
        =welder::doc(
          R"(The global-WMO placement (MODF) of a WMO-only map; at most one
                        record. Its name_id is unused — the client always loads the
                        MWMO content (or, in FileDataID-era files without MWMO, treats
                        name_id as the WMO FileDataID).)")]]
      std::vector<SMMapObjDef> globalWmo;

      /** The canonical chunk-stream order the serializer emits a fresh entity
          in (see writeOrder). Lists every chunk member exactly once.

          MAI2 (12.0.5+, Midnight) postdates the supported client range; it and
          any other unmodeled chunk round-trip through ChunkExtras::unknown. */
      static constexpr std::array ChunkOrder = {
        fourcc("MVER"),
        fourcc("MPHD"),
        fourcc("MAIN"),
        fourcc("MAID"),
        fourcc("MWMO"),
        fourcc("MODF"),
        fourcc("MANM"),
      };

      /** Validation hook (see formats::detail::validateValue): the map-wide
          tables the client indexes POSITIONALLY, so their size is the
          contract — the 64x64 MAIN grid and, when engaged, the matching MAID
          grid — plus the global-WMO records a WMO-only map is limited to.
          @param report the report findings land in. */
      [[=welder::mark::exclude]]
      void validateExtra(ValidationReport& report) const {
        // the client addresses a tile as tiles[y * 64 + x]; a short table
        // silently reads the wrong tiles rather than failing
        if (!tiles.empty() && tiles.size() != WdtTileSlots) report.addError(
          "tiles", std::format("the MAIN table holds {} records, not 64*64", tiles.size()));

        // MAID is the same grid: engaged, it must cover every tile
        if constexpr (requires { this->mapFdids; })
          if (!this->mapFdids.empty() && this->mapFdids.size() != WdtTileSlots)
            report.addError("mapFdids",
                             std::format("the MAID table holds {} records, not 64*64", this->mapFdids.size()));

        // a WMO-only map places exactly one global object
        if (globalWmo.size() > 1)
          report.addError("globalWmo",
                           std::format("{} global WMO placements; a map has at most one", globalWmo.size()));
      }
    };
  }

  /** A WDT main file — the canonicalizing face of detail::WDTRoot: every
      client version maps to its range's first grid version (WdtRootPivots),
      so e.g. one instantiation serves Vanilla through Legion. (Qualified
      root::detail — a bare detail:: is ambiguous against chunks::detail via
      the using-directive.) */
  template <ClientVersion V>
  using WDTRoot = root::detail::WDTRoot<canonicalVersion(V, WdtRootPivots, WdtVersions)>;
}
