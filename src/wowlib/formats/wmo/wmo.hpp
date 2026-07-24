#pragma once

/** @file
    The WMO entity (namespace wowlib::formats::wmo): a v17 world map object with
    its root file (wmo::root) and all group files (wmo::group) unified, versioned
    on the client it is laid out for. Reading is chunk-order independent; writing
    replays the original chunk order so an untouched entity rewrites
    byte-for-byte. */

#include <array>
#include <span>
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

  /** The versions WMO is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. Kept in sync with the
      alias/instantiation X-macro below (checked by static_assert). */
  inline constexpr std::array wmo_versions{
    versions::vanilla, versions::tbc,         versions::wotlk,
    versions::cata,    versions::mop,         versions::wod,
    versions::legion,  versions::bfa,         versions::shadowlands,
    versions::dragonflight, versions::tww};

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

  /** A whole WMO (world map object) for one client version: the root file and all
      its group files unified as one entity.

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
  };
}

/** Per-version expansion of the WMO template surface. X(Suffix, version) is
    invoked once per targeted release, in release order — the single spot that
    couples wmo::wmo_versions, the welded aliases and the explicit
    instantiations. Extending the version list means adding one row here. */
#define WOWLIB_WMO_FOR_EACH_VERSION(X)                                                             \
  X(Vanilla, vanilla)                                                                              \
  X(Tbc, tbc)                                                                                      \
  X(Wotlk, wotlk)                                                                                  \
  X(Cata, cata)                                                                                    \
  X(Mop, mop)                                                                                      \
  X(Wod, wod)                                                                                      \
  X(Legion, legion)                                                                                \
  X(Bfa, bfa)                                                                                      \
  X(Shadowlands, shadowlands)                                                                      \
  X(Dragonflight, dragonflight)                                                                    \
  X(TheWarWithin, tww)

// The bindings surface for the versioned templates: welder welds a
// class-template instantiation through a namespace-scope alias, whose identifier
// is the target-language name. Each family's aliases are declared in its own
// namespace so the per-version classes surface under the matching submodule,
// mirroring the C++ layout (wowlib.formats.wmo{,.root,.root.chunks,.group,
// .group.chunks}).
namespace wowlib::formats::wmo::root
{
#define WOWLIB_WMO_ROOT_ALIAS(Suffix, version_) using WMORoot##Suffix = WMORoot<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_ROOT_ALIAS)
#undef WOWLIB_WMO_ROOT_ALIAS
}

namespace wowlib::formats::wmo::group
{
#define WOWLIB_WMO_GROUP_ALIAS(Suffix, version_)                                                   \
  using WMOGroupBody##Suffix = WMOGroupBody<versions::version_>;                                   \
  using WMOGroup##Suffix = WMOGroup<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_GROUP_ALIAS)
#undef WOWLIB_WMO_GROUP_ALIAS
}

namespace wowlib::formats::wmo::group::chunks
{
#define WOWLIB_WMO_GROUP_CHUNK_ALIAS(Suffix, version_)                                             \
  using WMOGroupHeader##Suffix = SMOGroupHeader<versions::version_>;                               \
  using WMOBatch##Suffix = SMOBatch<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_GROUP_CHUNK_ALIAS)
#undef WOWLIB_WMO_GROUP_CHUNK_ALIAS
}

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_ALIAS(Suffix, version_) using WMO##Suffix = WMO<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_ALIAS)
#undef WOWLIB_WMO_ALIAS

  namespace detail
  {
    /** X-macro coverage check: every alias row must be a wmo_versions entry and
        vice versa (the row count matches and each row's version is present). */
    consteval bool wmo_macro_covers_versions()
    {
      std::size_t rows = 0;
#define WOWLIB_WMO_COUNT_ROW(Suffix, version_)                                                     \
  {                                                                                                \
    ++rows;                                                                                        \
    bool found = false;                                                                            \
    for (const ClientVersion& v : wmo_versions)                                                    \
      found = found || v == versions::version_;                                                    \
    if (!found)                                                                                    \
      return false;                                                                                \
  }
      WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_COUNT_ROW)
#undef WOWLIB_WMO_COUNT_ROW
      return rows == wmo_versions.size();
    }
    static_assert(wmo_macro_covers_versions(),
                  "WOWLIB_WMO_FOR_EACH_VERSION must list exactly the wmo_versions entries");
  }
}

// There are NO extern-template declarations or explicit instantiations here:
// consumer TUs implicitly instantiate exactly the versions they use (the
// read/write definitions live in serializer.hpp and io.hpp). The language
// bindings, which need the FULL version matrix, declare and expand it in
// their own translation units — see bindings/python/instantiations/wmo.hpp.
