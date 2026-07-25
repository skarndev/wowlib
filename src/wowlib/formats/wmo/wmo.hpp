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

// There are NO welded per-range alias tables, extern-template declarations or
// explicit instantiations here: C++ consumer TUs implicitly instantiate
// exactly the versions they use (the read/write definitions live in
// serializer.hpp and io.hpp). The language bindings, which weld and expand
// the FULL version matrix, declare the range alias tables and the
// instantiation matrix in their own translation units — see
// bindings/python/instantiations/wmo_ranges.hpp and wmo_matrix.inl.
