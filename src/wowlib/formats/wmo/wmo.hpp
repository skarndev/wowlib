#pragma once

/** @file
    The WMO entity (namespace wowlib::formats::wmo): a v17 world map object with
    its root file (wmo::root) and all group files (wmo::group) unified, versioned
    on the client it is laid out for. Reading is chunk-order independent; writing
    replays the original chunk order so an untouched entity rewrites
    byte-for-byte. */

#include <array>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/formats/wmo/group/group.hpp>
#include <wowlib/formats/wmo/root/root.hpp>
#include <wowlib/fs/filesystem.hpp>


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
    // --- internal fs-I/O helpers (definitions at the bottom of this header;
    // --- private so the Python/Lua surface and the C++ API stay verbs-only) -

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

// --- fs-level read/write definitions -----------------------------------------
// Inline in this header (not a separate io.hpp): the entities are templates,
// so the definitions must be visible for implicit instantiation anyway — the
// library ships NO explicit instantiations; every consumer TU instantiates
// exactly the versions it uses (the bindings expand the full matrix in their
// own translation units, see bindings/python/instantiations/).
namespace wowlib::formats::wmo
{
  template <ClientVersion V>
  std::string detail::WMO<V>::group_path(std::string_view root_path, std::size_t index)
  {
    std::string_view stem = root_path;
    if (stem.ends_with(".wmo"))
      stem.remove_suffix(4);
    return std::format("{}_{:03}.wmo", stem, index);
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::check_mver(std::uint32_t mver, std::string_view which)
  {
    if (mver != wmo_version_v17)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("{} MVER is {}, expected {}", which, mver, wmo_version_v17));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::read(std::span<const std::byte> root_data,
                            std::span<const std::span<const std::byte>> group_datas)
  {
    root = {};
    groups.clear();

    if (auto r = root.read(root_data); !r)
      return std::unexpected{r.error()};
    if (auto r = check_mver(root.mver, "root"); !r)
      return std::unexpected{r.error()};

    groups.reserve(group_datas.size());
    for (std::size_t i = 0; i < group_datas.size(); ++i)
    {
      WMOGroup<V> group;
      if (auto r = group.read(group_datas[i]); !r)
        return make_error(r.error().code,
                          std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
      if (auto r = check_mver(group.mver, std::format("group {}", i)); !r)
        return std::unexpected{r.error()};
      groups.push_back(std::move(group));
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto root_data = fs.read_file(key);
    if (!root_data)
      return std::unexpected{root_data.error()};

    root = {};
    groups.clear();

    if (auto r = root.read(*root_data); !r)
      return std::unexpected{r.error()};
    if (auto r = check_mver(root.mver, "root"); !r)
      return std::unexpected{r.error()};

    // MOGI (one info record per group file) is the group count's source of
    // truth — header.n_groups is a derived binary field stamped from it.
    const std::size_t n_groups = root.group_infos.size();
    // GFID (group FileDataIDs) is Legion+; pre-Legion roots have no such member
    // (it lives in a version trait that version does not inherit), so they always
    // locate groups by the "{root}_NNN.wmo" path convention.
    bool by_fdid = false;
    if constexpr (requires { root.group_fdids; })
      by_fdid = root.group_fdids.size() >= n_groups;

    std::string root_path;
    if (!by_fdid)
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return make_error(ErrorCode::PathNotResolvable,
                          "group files need the root path (no GFID chunk and the root "
                          "key has no resolvable path)");
      root_path = *resolved.path;
    }

    groups.reserve(n_groups);
    for (std::size_t i = 0; i < n_groups; ++i)
    {
      const FileKey group_key = [&]() -> FileKey {
        if constexpr (requires { root.group_fdids; })
          if (by_fdid)
            return FileKey{FileDataID{root.group_fdids[i]}};
        return FileKey{group_path(root_path, i)};
      }();
      const auto group_data = fs.read_file(group_key);
      if (!group_data)
        return make_error(group_data.error().code,
                          std::format("group {}: {}", i, group_data.error().message),
                          group_data.error().native_error);

      WMOGroup<V> group;
      if (auto r = group.read(*group_data); !r)
        return make_error(r.error().code, std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
      if (auto r = check_mver(group.mver, std::format("group {}", i)); !r)
        return std::unexpected{r.error()};
      groups.push_back(std::move(group));
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "saving a WMO needs a path for the root key");
    // the derived n_groups is stamped from MOGI, so the two group tables the
    // entity does keep (info records and group files) must agree
    if (root.group_infos.size() != groups.size())
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("the MOGI group-info table holds {} records but {} group "
                                    "files are baked in — every group needs its info record",
                                    root.group_infos.size(), groups.size()));

    const auto root_data = root.write();
    if (!root_data)
      return std::unexpected{root_data.error()};
    if (auto r = fs.add_file(*resolved.path, *root_data); !r)
      return std::unexpected{r.error()};

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      const auto group_data = groups[i].write();
      if (!group_data)
        return std::unexpected{group_data.error()};
      if (auto r = fs.add_file(group_path(*resolved.path, i), *group_data); !r)
        return make_error(r.error().code, std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
    }
    return {};
  }
}

// There are NO welded per-range alias tables, extern-template declarations or
// explicit instantiations here: C++ consumer TUs implicitly instantiate
// exactly the versions they use (the read/write definitions live in this
// header and the chunk serializer). The language bindings, which weld and expand
// the FULL version matrix, declare the range alias tables and the
// instantiation matrix in their own translation units — see
// bindings/python/instantiations/wmo_ranges.hpp and wmo_matrix.inl.
