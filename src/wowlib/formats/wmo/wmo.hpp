#pragma once

/** @file
    The WMO entity (namespace wowlib::formats::wmo): a v17 world map object with
    its root file (wmo::root) and all group files (wmo::group) unified, versioned
    on the client it is laid out for. Reading is chunk-order independent; writing
    replays the original chunk order so an untouched entity rewrites
    byte-for-byte. */

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/wmo/group/group.hpp>
#include <wowlib/formats/wmo/root/root.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::formats::wmo
{
  using root::WMORoot;
  using group::WMOGroup;

  /** The version-agnostic base of every WMO<V> (welded as "WMO").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WMO* classes a common welded supertype, and the module
      glue attaches the for_version/read/write/convert surface to it (dispatching
      to the concrete version). Binding users therefore write version-agnostic
      code (`isinstance(x, WMO)`, a `w: WMO` annotation,
      `WMO.for_version(expansion)`). It has no role in the C++ API, where you use
      the concrete WMO<V> directly.

      @see https://wowdev.wiki/WMO */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMO"),
    =welder::doc(R"(
        A whole world map object, abstract over the client version — the root
        file and all its group files as one entity. Construct the concrete
        version with WMO.for_version(expansion), then read()/write(); the
        per-version WMO* classes are subclasses. See https://wowdev.wiki/WMO.)")
  ]] WMOBase
  {
  };

  namespace detail
  {
  /** A whole WMO (world map object) for one client version: the root file and all
      its group files unified as one entity. Instantiate through the
      canonicalizing wmo::WMO alias, never directly.

      A WMO is a v17 world map object — a building, cave, bridge or other placed
      structure. It is stored as a root file (shared data: materials, doodads,
      portals, lights; see WMORoot) plus one file per group (the geometry; see
      WMOGroup). Group files are located by GFID (Legion+ clients) or the
      "{root}_NNN.wmo" naming convention. Reading is chunk-order independent;
      writing replays the original chunk order, so an entity read from a client
      and left unmodified rewrites byte-for-byte.

      @tparam V the client version this assembly targets.
      @see https://wowdev.wiki/WMO */
  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A whole world map object for one client version: the root file and all
        its group files as one entity. Group files are located by GFID (Legion+
        clients) or the "{root}_NNN.wmo" naming convention. An entity read from
        a client and left unmodified rewrites byte-for-byte. See
        https://wowdev.wiki/WMO.)")
  ]] WMO : WMOBase
  {
    static constexpr ClientVersion version = V;

    [[=welder::doc("The root file contents.")]]
    WMORoot<V> root{};

    [[=welder::doc("The group files, in group order."), =welder::mark::no_reassign]]
    std::vector<WMOGroup<V>> groups;

    // read()/write() weld the (FileSystem, FileKey) load/save on LUA ONLY — that
    // is Lua's whole WMO scripting surface. On Python the module glue attaches
    // the read()/write()/convert()/for_version() surface to WMOBase instead
    // (dispatching to the concrete version), so the per-version Python classes
    // stay pure data and `w: WMO` speaks the ops. The span-of-spans parse below
    // stays C++/glue-only.

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Load the WMO and all its group files from a client "
                   "filesystem, replacing this entity's contents.")]]
    Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key
                      [[=welder::doc("the root file identity (path and/or FileDataID)")]]);

    /** Parse the WMO from already-loaded buffers (no filesystem access),
        replacing this entity's contents. Not welded: span-of-spans does not
        cross the binding boundary — the Python read() glue accepts a root
        buffer plus one buffer/file-like per group instead.
        @param root_data   the root file bytes.
        @param group_datas one buffer per group file, in group order.
        @return nothing, or the first error. */
    [[=welder::mark::exclude]]
    Result<void> read(std::span<const std::byte> root_data,
                      std::span<const std::span<const std::byte>> group_datas);

    [[=welder::mark::only(welder::lang::lua),
      =welder::doc("Serialize and store the WMO (root and every group) through "
                   "the filesystem's project overlay; group file names are "
                   "derived from the root key, which must resolve to a path.")]]
    Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                       const FileKey& key
                       [[=welder::doc("the root file identity; must resolve to a path")]]) const;

  private:
    // --- internal fs-I/O helpers (definitions in io.hpp; private so the
    // --- Python/Lua surface and the public C++ API stay verbs-only) --------

    /** Derive a group file path from its root: "world\wmo\thing.wmo" ->
        "world\wmo\thing_007.wmo".
        @param root_path the root file path.
        @param index     the zero-based group index.
        @return the derived group path. */
    static std::string group_path(std::string_view root_path, std::size_t index);

    /** Verify an MVER payload against the v17 the supported clients share.
        @param mver  the version value read from the file.
        @param which which file carried it, for the diagnostic ("root",
                     "group 3", ...).
        @return nothing, or FormatVersionMismatch. */
    static Result<void> check_mver(std::uint32_t mver, std::string_view which);
  };
  }

  /** A whole WMO — the canonicalizing face of detail::WMO: every client
      version maps to its range's first grid version (wmo_assembly_pivots),
      so e.g. one instantiation serves Vanilla through WotLK. */
  template <ClientVersion V>
  using WMO = detail::WMO<canonical_version(V, wmo_assembly_pivots, wmo_versions)>;
}

/** Per-RANGE expansion of the WMO template surface: each family's X-macro
    lists one row per REAL content permutation — X(Suffix, version) with the
    range's canonical grid version — and drives the welded aliases here plus
    the instantiation matrix in bindings/python/instantiations/. Every table
    is consteval-checked against the family's pivots (ranges_valid in
    wmo::detail below). Extending the version list means revisiting the pivot
    lists in boundaries.hpp; the checks then dictate the rows. */
#define WOWLIB_WMO_RANGES_ROOT(X)                                                                  \
  X(VanillaToWod, vanilla)                                                                         \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(ShadowlandsToDragonflight, shadowlands)                                                        \
  X(TheWarWithin, tww)

#define WOWLIB_WMO_RANGES_GROUP(X)                                                                 \
  X(VanillaToWotlk, vanilla)                                                                       \
  X(CataToMop, cata)                                                                               \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(DragonflightPlus, dragonflight)

#define WOWLIB_WMO_RANGES_GROUP_HEADER(X)                                                          \
  X(VanillaToBfa, vanilla)                                                                         \
  X(ShadowlandsPlus, shadowlands)

#define WOWLIB_WMO_RANGES_BATCH(X)                                                                 \
  X(VanillaToWod, vanilla)                                                                         \
  X(LegionPlus, legion)

#define WOWLIB_WMO_RANGES_ASSEMBLY(X)                                                              \
  X(VanillaToWotlk, vanilla)                                                                       \
  X(CataToMop, cata)                                                                               \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

// The bindings surface for the versioned templates: welder welds a
// class-template instantiation through a namespace-scope alias, whose
// identifier is the target-language name. Each family's aliases are declared
// in its own namespace so the per-range classes surface under the matching
// submodule, mirroring the C++ layout.
namespace wowlib::formats::wmo::root
{
#define WOWLIB_WMO_ROOT_ALIAS(Suffix, version_) using WMORoot##Suffix = WMORoot<versions::version_>;
  WOWLIB_WMO_RANGES_ROOT(WOWLIB_WMO_ROOT_ALIAS)
#undef WOWLIB_WMO_ROOT_ALIAS
}

namespace wowlib::formats::wmo::group
{
#define WOWLIB_WMO_GROUP_ALIAS(Suffix, version_)                                                   \
  using WMOGroupBody##Suffix = WMOGroupBody<versions::version_>;                                   \
  using WMOGroup##Suffix = WMOGroup<versions::version_>;
  WOWLIB_WMO_RANGES_GROUP(WOWLIB_WMO_GROUP_ALIAS)
#undef WOWLIB_WMO_GROUP_ALIAS
}

namespace wowlib::formats::wmo::group::chunks
{
#define WOWLIB_WMO_GROUP_HEADER_ALIAS(Suffix, version_)                                            \
  using WMOGroupHeader##Suffix = SMOGroupHeader<versions::version_>;
  WOWLIB_WMO_RANGES_GROUP_HEADER(WOWLIB_WMO_GROUP_HEADER_ALIAS)
#undef WOWLIB_WMO_GROUP_HEADER_ALIAS

#define WOWLIB_WMO_BATCH_ALIAS(Suffix, version_)                                                   \
  using WMOBatch##Suffix = SMOBatch<versions::version_>;
  WOWLIB_WMO_RANGES_BATCH(WOWLIB_WMO_BATCH_ALIAS)
#undef WOWLIB_WMO_BATCH_ALIAS
}

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_ALIAS(Suffix, version_) using WMO##Suffix = WMO<versions::version_>;
  WOWLIB_WMO_RANGES_ASSEMBLY(WOWLIB_WMO_ALIAS)
#undef WOWLIB_WMO_ALIAS

  namespace detail
  {
    // Range-table validation: every family's rows must exactly enumerate the
    // distinct canonicals of the grid, with the suffix range_suffix derives.
#define WOWLIB_WMO_RANGE_ROW(Suffix, version_)                                                     \
  ::wowlib::formats::RangeRow{#Suffix, ::wowlib::versions::version_},

    inline constexpr std::array wmo_root_rows{WOWLIB_WMO_RANGES_ROOT(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_root_rows, wmo_root_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_ROOT drifted from wmo_root_pivots");
    inline constexpr std::array wmo_group_rows{WOWLIB_WMO_RANGES_GROUP(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_group_rows, wmo_group_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_GROUP drifted from wmo_group_pivots");
    inline constexpr std::array wmo_group_header_rows{
      WOWLIB_WMO_RANGES_GROUP_HEADER(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_group_header_rows, wmo_group_header_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_GROUP_HEADER drifted from wmo_group_header_pivots");
    inline constexpr std::array wmo_batch_rows{WOWLIB_WMO_RANGES_BATCH(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_batch_rows, wmo_batch_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_BATCH drifted from wmo_batch_pivots");
    inline constexpr std::array wmo_assembly_rows{
      WOWLIB_WMO_RANGES_ASSEMBLY(WOWLIB_WMO_RANGE_ROW)};
    static_assert(ranges_valid(wmo_assembly_rows, wmo_assembly_pivots, wmo_versions),
                  "WOWLIB_WMO_RANGES_ASSEMBLY drifted from wmo_assembly_pivots");
#undef WOWLIB_WMO_RANGE_ROW
  }
}

// There are NO extern-template declarations or explicit instantiations here:
// consumer TUs implicitly instantiate exactly the versions they use (the
// read/write definitions live in serializer.hpp and io.hpp). The language
// bindings, which need the FULL version matrix, declare and expand it in
// their own translation units — see bindings/python/instantiations/wmo.hpp.
