#pragma once

/** @file
    The WMO entity: a v17 world map object with its root file and all group
    files unified, versioned on the client it is laid out for. Reading is
    chunk-order independent; writing replays the original chunk order so an
    untouched entity rewrites byte-for-byte. */

#include <array>
#include <span>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/convert.hpp>
#include <wowlib/formats/wmo/group.hpp>
#include <wowlib/formats/wmo/root.hpp>

namespace wowlib::fs
{
  class FileSystem;
}

namespace wowlib::formats::wmo
{
  /** The versions WMO is instantiated (and welded) for: every targeted
      last-minor-of-major release, in release order. Kept in sync with the
      alias/instantiation X-macro below (checked by static_assert). */
  inline constexpr std::array wmo_versions{
    versions::vanilla, versions::tbc,         versions::wotlk,
    versions::cata,    versions::mop,         versions::wod,
    versions::legion,  versions::bfa,         versions::shadowlands,
    versions::dragonflight, versions::tww};

  /** A whole WMO — the root and its group files as one entity.

      Group files are located through GFID when present (Legion+ clients),
      otherwise by the "{root}_{NNN}.wmo" naming convention. */
  template <ClientVersion V>
  struct
  [[
    =welder::weld(welder::lang::py, welder::lang::lua),
    =welder::doc(R"(
        A whole world map object: the root file and all its group files as one
        entity. Load through a FileSystem; an unmodified entity saves back
        byte-for-byte.)")
  ]]
  WMO
  {
    static constexpr ClientVersion version = V;

    [[=welder::doc("The root file contents.")]]
    WMORoot<V> root{};

    [[=welder::doc("The group files, in group order.")]]
    std::vector<WMOGroup<V>> groups;

    /** Load a WMO and all its groups from a client filesystem.
        @param fs  the filesystem gateway.
        @param key the root file identity (path and/or FileDataID).
        @return the assembled entity, or the first error. */
    [[=welder::doc("Load a WMO and all its group files."),
      =welder::returns("the assembled WMO")]]
    static Result<WMO> load(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                            const FileKey& key
                            [[=welder::doc("the root file identity (path and/or FileDataID)")]]);

    /** Serialize and store the WMO (root and groups) through the filesystem's
        project overlay. The root key must resolve to a path — group files are
        stored under derived "{root}_{NNN}.wmo" names.
        @param fs  the filesystem gateway.
        @param key the root file identity.
        @return nothing, or the first error. */
    [[=welder::doc("Serialize and store the WMO (root and groups) through the "
                   "filesystem's project overlay.")]]
    Result<void> save(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                      const FileKey& key
                      [[=welder::doc("the root file identity; must resolve to a path")]]) const;

    /** Assemble a WMO from already-loaded buffers (no filesystem access).
        Not welded: span-of-spans does not cross the binding boundary.
        @param root_data   the root file bytes.
        @param group_datas one buffer per group file, in group order.
        @return the assembled entity, or the first error. */
    [[=welder::mark::exclude]]
    static Result<WMO> parse(std::span<const std::byte> root_data,
                             std::span<const std::span<const std::byte>> group_datas);

    /** Serialize the root file.
        @return the root file bytes. */
    [[=welder::doc("Serialize the root file."),
      =welder::returns("the root file bytes")]]
    Result<FileBuffer> write_root() const;

    /** Serialize one group file.
        @param index the group index.
        @return the group file bytes. */
    [[=welder::doc("Serialize one group file."),
      =welder::returns("the group file bytes")]]
    Result<FileBuffer> write_group(std::size_t index
                                   [[=welder::doc("the group index")]]) const;
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

namespace wowlib::formats
{
  // The bindings surface for the versioned templates: welder welds a
  // class-template instantiation through a namespace-scope alias, whose
  // identifier is the target-language name (flat suffixed classes, e.g.
  // wowlib.formats.WMOWotlk). Declared in dependency order within each version
  // (referenced types before the types whose members name them);
  // version-invariant wire structs weld under their own names in the wmo
  // submodule instead.
#define WOWLIB_WMO_ALIASES(Suffix, version_)                                                       \
  using WMOGroupHeader##Suffix = wmo::SMOGroupHeader<versions::version_>;                          \
  using WMOBatch##Suffix = wmo::SMOBatch<versions::version_>;                                      \
  using WMORoot##Suffix = wmo::WMORoot<versions::version_>;                                        \
  using WMOGroupBody##Suffix = wmo::WMOGroupBody<versions::version_>;                              \
  using WMOGroup##Suffix = wmo::WMOGroup<versions::version_>;                                      \
  using WMO##Suffix = wmo::WMO<versions::version_>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_ALIASES)
#undef WOWLIB_WMO_ALIASES

  namespace detail
  {
    /** X-macro coverage check: every alias row must be a wmo_versions entry
        and vice versa (the row count matches and each row's version is
        present). */
    consteval bool wmo_macro_covers_versions()
    {
      std::size_t rows = 0;
#define WOWLIB_WMO_COUNT_ROW(Suffix, version_)                                                     \
  {                                                                                                \
    ++rows;                                                                                        \
    bool found = false;                                                                            \
    for (const ClientVersion& v : wmo::wmo_versions)                                               \
      found = found || v == versions::version_;                                                    \
    if (!found)                                                                                    \
      return false;                                                                                \
  }
      WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_COUNT_ROW)
#undef WOWLIB_WMO_COUNT_ROW
      return rows == wmo::wmo_versions.size();
    }
    static_assert(wmo_macro_covers_versions(),
                  "WOWLIB_WMO_FOR_EACH_VERSION must list exactly the wmo_versions entries");
  }
}

namespace wowlib::formats::wmo
{
#define WOWLIB_WMO_EXTERN(Suffix, version_)                                                        \
  extern template struct WMORoot<versions::version_>;                                              \
  extern template struct WMOGroupBody<versions::version_>;                                         \
  extern template struct WMOGroup<versions::version_>;                                             \
  extern template struct WMO<versions::version_>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_EXTERN)
#undef WOWLIB_WMO_EXTERN
}

namespace wowlib::formats
{
  // The ChunkedFile bases carry the read()/write() definitions that expand the
  // serializer; extern-ing them here (an explicit instantiation must sit in
  // the template's enclosing namespace) confines that expansion to wmo.cpp.
#define WOWLIB_WMO_EXTERN_SERIALIZER(Suffix, version_)                                             \
  extern template struct ChunkedFile<wmo::WMORoot<versions::version_>>;                            \
  extern template struct ChunkedFile<wmo::WMOGroupBody<versions::version_>>;                       \
  extern template struct ChunkedFile<wmo::WMOGroup<versions::version_>>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_EXTERN_SERIALIZER)
#undef WOWLIB_WMO_EXTERN_SERIALIZER
}
