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
#include <wowlib/formats/convert.hpp>
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

  /** The version-agnostic base of every WMO<V>. Welded as "WMO" — the facade's
      abstract entity: its per-version subclasses are the WMO* classes, so
      isinstance and a `w: WMO` annotation cover them all. Empty in C++; the
      for_version/read/write/convert surface a `w: WMO` speaks is attached to it
      in the Python module glue (dispatching to the concrete version). */
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::weld_as("WMO"),
    =welder::doc(R"(
        A whole world map object, abstract over the client version — the root
        file and all its group files as one entity. Construct the concrete
        version with WMO.for_version(expansion), then read()/write(); the
        per-version WMO* classes are subclasses.)")
  ]] WMOBase
  {
  };

  template <ClientVersion V>
  struct [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A whole world map object for one client version: the root file and all
        its group files as one entity. Group files are located by GFID (Legion+
        clients) or the "{root}_NNN.wmo" naming convention. An entity read from
        a client and left unmodified rewrites byte-for-byte.)")
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

namespace wowlib::formats
{
  template <>
  inline constexpr auto supported_versions<wmo::WMO> = wmo::wmo_versions;
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
// mirroring the C++ layout (wowlib.formats.wmo{,.root,.group,.group_chunks}).
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

namespace wowlib::formats::wmo::group_chunks
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

namespace wowlib::formats::wmo::root
{
#define WOWLIB_WMO_ROOT_EXTERN(Suffix, version_) extern template struct WMORoot<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_ROOT_EXTERN)
#undef WOWLIB_WMO_ROOT_EXTERN
}

namespace wowlib::formats::wmo::group
{
#define WOWLIB_WMO_GROUP_EXTERN(Suffix, version_)                                                  \
  extern template struct WMOGroupBody<versions::version_>;                                         \
  extern template struct WMOGroup<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_GROUP_EXTERN)
#undef WOWLIB_WMO_GROUP_EXTERN
}

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_EXTERN(Suffix, version_) extern template struct WMO<versions::version_>;
  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_EXTERN)
#undef WOWLIB_WMO_EXTERN
}

namespace wowlib::formats
{
  // The ChunkedFile bases carry the read()/write() definitions that expand the
  // serializer; extern-ing them here (an explicit instantiation must sit in the
  // template's enclosing namespace) confines that expansion to wmo.cpp.
#define WOWLIB_WMO_EXTERN_SERIALIZER(Suffix, version_)                                             \
  extern template struct ChunkedFile<wmo::root::WMORoot<versions::version_>>;                      \
  extern template struct ChunkedFile<wmo::group::WMOGroupBody<versions::version_>>;                \
  extern template struct ChunkedFile<wmo::group::WMOGroup<versions::version_>>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_EXTERN_SERIALIZER)
#undef WOWLIB_WMO_EXTERN_SERIALIZER
}
