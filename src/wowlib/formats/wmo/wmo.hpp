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
#include <wowlib/formats/common/file_entity.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/wmo/group/group.hpp>
#include <wowlib/formats/wmo/root/root.hpp>
#include <wowlib/fs/filesystem.hpp>


namespace wowlib::formats::wmo {
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
      =welder::weld,
      =welder::weld_as("WMO"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A whole world map object, abstract over the client version — the root
        file and all its group files as one entity. Construct the concrete
        version with WMO.for_version(expansion), then read()/write(); the
        per-version WMO* classes are subclasses. See https://wowdev.wiki/WMO.)")
    ]] WMOBase : FileEntityBase {};

  namespace detail {
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
        =welder::weld,
        =welder::doc(R"(
        A whole world map object for one client version: the root file and all
        its group files as one entity. Group files are located by GFID (Legion+
        clients) or the "{root}_NNN.wmo" naming convention. An entity read from
        a client and left unmodified rewrites byte-for-byte. See
        https://wowdev.wiki/WMO.)")
      ]] WMO : WMOBase {
      static constexpr ClientVersion Version = V;

      [[=welder::doc("The root file contents.")]]
      WMORoot<V> root{};

      [[=welder::doc("The group files, in group order."), =
        welder::mark::no_reassign]]
      std::vector<WMOGroup<V>> groups;

      // read()/write() weld the (FileSystem, FileKey) load/save on LUA AND C#
      // ONLY — that is the whole WMO scripting surface in both. On Python the
      // module glue attaches the read()/write()/convert()/for_version() surface to
      // WMOBase instead (dispatching to the concrete version), so the per-version
      // Python classes stay pure data and `w: WMO` speaks the ops; Lua and C# have
      // no such glue, so they take these methods directly. The span-of-spans parse
      // below stays C++/glue-only.

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc("Load the WMO and all its group files from a client "
          "filesystem, replacing this entity's contents.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key [[=welder::doc("the root file identity (path and/or FileDataID)")]]);

      /** Parse the WMO from already-loaded buffers (no filesystem access),
          replacing this entity's contents. Not welded: span-of-spans does not
          cross the binding boundary — the Python read() glue accepts a root
          buffer plus one buffer/file-like per group instead.
          @param rootData   the root file bytes.
          @param groupDatas one buffer per group file, in group order.
          @return nothing, or the first error. */
      [[=welder::mark::exclude]]
      Result<void> read(std::span<const std::byte> rootData, std::span<const std::span<const std::byte>> groupDatas);

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc(
          "Serialize and store the WMO (root and every group) through "
          "the filesystem's project overlay; group file names are "
          "derived from the root key, which must resolve to a path.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key [[=welder::doc("the root file identity; must resolve to a path")]]) const;

      // Beyond the root's and each group's own contracts, this sees the ones
      // only the assembly can: MOGI describing exactly the groups held, the
      // groups' indexesInRoot references, the header portal ranges into MOPR,
      // and every rendered face resolving its material in MOMT.
      [[nodiscard]]
      [[=welder::doc(R"(
        Check the logical integrity contracts this object must satisfy to LOAD
        in the client — across the root file AND every group file — which
        write() deliberately never enforces. Call it before writing when you
        want to know the files will load. An object read from a client and left
        unmodified reports no errors; warnings mark states real client files
        ship.)"),
        =welder::returns(R"(every violated contract, each with its member path
                          ("root..." / "groups[i]..."))")]]
      ValidationReport validate() const;

      [[nodiscard]]
      [[=welder::doc(
          "Validate and raise on the first error instead of returning "
          "a report — the assert-style face of validate()."),
        =welder::returns("nothing; raises when validate() finds any error")]]
      Result<void> ensureValid() const;

    private:
      // --- internal fs-I/O helpers (definitions at the bottom of this header;
      // --- private so the Python/Lua surface and the C++ API stay verbs-only) -

      /** Derive a group file path from its root: "world\wmo\thing.wmo" ->
          "world\wmo\thing_007.wmo".
          @param rootPath the root file path.
          @param index     the zero-based group index.
          @return the derived group path. */
      static std::string _groupPath(std::string_view rootPath, std::size_t index);

      /** Verify an MVER payload against the v17 the supported clients share.
          @param mver  the version value read from the file.
          @param which which file carried it, for the diagnostic ("root",
                       "group 3", ...).
          @return nothing, or FormatVersionMismatch. */
      static Result<void>
      _checkMver(std::uint32_t mver, std::string_view which);
    };
  }

  /** A whole WMO — the canonicalizing face of detail::WMO: every client
      version maps to its range's first grid version (WmoAssemblyPivots),
      so e.g. one instantiation serves Vanilla through WotLK. */
  template <ClientVersion V>
  using WMO = detail::WMO<canonicalVersion(V, WmoAssemblyPivots, WmoVersions)>;
}

// --- fs-level read/write definitions -----------------------------------------
// Inline in this header (not a separate io.hpp): the entities are templates,
// so the definitions must be visible for implicit instantiation anyway — the
// library ships NO explicit instantiations; every consumer TU instantiates
// exactly the versions it uses (the bindings expand the full matrix in their
// own translation units, see bindings/instantiations/).
namespace wowlib::formats::wmo {
  template <ClientVersion V>
  std::string detail::WMO<V>::_groupPath(std::string_view rootPath, std::size_t index) {
    std::string_view stem = rootPath;
    if (stem.ends_with(".wmo")) stem.remove_suffix(4);
    return std::format("{}_{:03}.wmo", stem, index);
  }

  template <ClientVersion V>
  ValidationReport detail::WMO<V>::validate() const {
    ValidationReport report;
    {
      const std::size_t mark = report.size();
      formats::detail::validateEntity(root, report);
      report.prefixFrom(mark, "root");
    }
    for (std::size_t i = 0; i < groups.size(); ++i) {
      const std::size_t mark = report.size();
      formats::detail::validateEntity(groups[i], report);
      report.prefixFrom(mark, std::format("groups[{}]", i));
    }

    // the MOGI table must describe exactly the groups the assembly holds
    if (root.groupInfos.size() != groups.size())
      report.addError("root.groupInfos",
                       std::format("count {} != {} group files held", root.groupInfos.size(), groups.size()));

    // Legion+: GFID locates the group files - too few cannot load; LOD WMOs
    // repeat the table per level, so any whole multiple is fine
    if constexpr (requires { root.groupFdids; })
      if (!groups.empty() && !root.groupFdids.empty()) {
        if (root.groupFdids.size() < groups.size())
          report.addError("root.group_fdids",
                           std::format("count {} < {} group files held", root.groupFdids.size(), groups.size()));
        else if (root.groupFdids.size() % groups.size() != 0)
          report.addWarning("root.group_fdids",
                             std::format(
                               "count {} is not a whole multiple of the {} groups " "(LOD tables repeat per level)",
                               root.groupFdids.size(), groups.size()));
      }

    constexpr std::size_t maxReported = 8;
    const std::size_t materialCount = root.materials.size();
    for (std::size_t i = 0; i < groups.size(); ++i) {
      const auto& body = groups[i].body;
      const std::string prefix = std::format("groups[{}].body", i);

      // the groups' declarative references into the root's arrays
      static constexpr auto BodyMembers = formats::detail::membersOf<std::remove_cvref_t<decltype(body)>>();
      template for (constexpr auto m : BodyMembers) {
        if constexpr (constexpr auto ir = formats::detail::annotation<formats::detail::IndexesInRootSpec, m>(); ir.
          has_value()) {
          constexpr auto target = formats::detail::memberNamed<WMORoot<V>>(ir->view());
          static_assert(target != std::meta::info{}, "indexes_in_root names no member of the root entity");
          constexpr const char* ident = std::define_static_string(std::meta::identifier_of(m));
          formats::detail::validateIndexElements(body.[:m:], root.[:target:].size(),
                                                   std::format("{}.{}", prefix, ident), ir->view(), report);
        }
      }

      // the header's portal slice references MOPR
      if (body.header.portalCount > 0 && body.header.portalStart + body.header.portalCount > root.portalRefs.size())
        report.addError(std::format("{}.header", prefix),
                         std::format("portal range [{}, {}) overruns the {} portal references",
                                     body.header.portalStart, body.header.portalStart + body.header.portalCount,
                                     root.portalRefs.size()));

      // every rendered face and batch must resolve its material in MOMT
      // (0xFF marks a collision-only face with no material)
      std::size_t badPolys = 0;
      for (std::size_t j = 0; j < body.polys.size(); ++j)
        if (body.polys[j].materialId != 0xFF && body.polys[j].materialId >= materialCount)
          if (++badPolys <= maxReported)
            report.addError(std::format("{}.polys[{}]", prefix, j),
                             std::format("material {} out of range: {} materials", body.polys[j].materialId,
                                         materialCount));
      if (badPolys > maxReported)
        report.addError(std::format("{}.polys", prefix),
                         std::format("... and {} more unresolvable materials", badPolys - maxReported));
      for (std::size_t j = 0; j < body.batches.size(); ++j) {
        const auto& batch = body.batches[j];
        std::size_t material = batch.materialId;
        if constexpr (requires { batch.materialIdLarge; })
          if ((batch.flags & 0x2) != 0) material = batch.materialIdLarge;
        if (material >= materialCount)
          report.addError(std::format("{}.batches[{}]", prefix, j),
                           std::format("material {} out of range: {} materials", material, materialCount));
      }

      // note: MOGI flags are deliberately NOT compared against the group
      // header's - real files differ on runtime-managed bits in nearly every
      // group (corpus: hundreds of divergences per client), so a mirror check
      // is pure noise
    }
    return report;
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::ensureValid() const {
    return validate().toResult();
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::_checkMver(std::uint32_t mver, std::string_view which) {
    if (mver != WmoVersionV17)
      return makeError(ErrorCode::FormatVersionMismatch,
                        std::format("{} MVER is {}, expected {}", which, mver, WmoVersionV17));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::read(std::span<const std::byte> rootData,
                                    std::span<const std::span<const std::byte>> groupDatas) {
    root = {};
    groups.clear();

    if (auto r = root.read(rootData); !r) return std::unexpected{r.error()};
    if (auto r = _checkMver(root.mver, "root"); !r) return std::unexpected{r.error()};

    groups.reserve(groupDatas.size());
    for (std::size_t i = 0; i < groupDatas.size(); ++i) {
      WMOGroup < V > group;
      if (auto r = group.read(groupDatas[i]); !r)
        return makeError(r.error().code, std::format("group {}: {}", i, r.error().message), r.error().nativeError);
      if (auto r = _checkMver(group.mver, std::format("group {}", i)); !r) return std::unexpected{r.error()};
      groups.push_back(std::move(group));
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::read(fs::FileSystem& fs, const FileKey& key) {
    const auto rootData = fs.readFile(key);
    if (!rootData) return std::unexpected{rootData.error()};

    root = {};
    groups.clear();

    if (auto r = root.read(*rootData); !r) return std::unexpected{r.error()};
    if (auto r = _checkMver(root.mver, "root"); !r) return std::unexpected{r.error()};

    // MOGI (one info record per group file) is the group count's source of
    // truth — header.nGroups is a derived binary field stamped from it.
    const std::size_t nGroups = root.groupInfos.size();
    // GFID (group FileDataIDs) is Legion+; pre-Legion roots have no such member
    // (it lives in a version trait that version does not inherit), so they always
    // locate groups by the "{root}_NNN.wmo" path convention.
    bool byFdid = false;
    if constexpr (requires { root.groupFdids; }) byFdid = root.groupFdids.size() >= nGroups;

    std::string rootPath;
    if (!byFdid) {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return makeError(ErrorCode::PathNotResolvable,
                          "group files need the root path (no GFID chunk and the root " "key has no resolvable path)");
      rootPath = *resolved.path;
    }

    groups.reserve(nGroups);
    for (std::size_t i = 0; i < nGroups; ++i) {
      const FileKey groupKey = [&]() -> FileKey {
        if constexpr (requires { root.groupFdids; })
          if (byFdid) return FileKey{FileDataID{root.groupFdids[i]}};
        return FileKey{_groupPath(rootPath, i)};
      }();
      const auto groupData = fs.readFile(groupKey);
      if (!groupData)
        return makeError(groupData.error().code, std::format("group {}: {}", i, groupData.error().message),
                          groupData.error().nativeError);

      WMOGroup < V > group;
      if (auto r = group.read(*groupData); !r)
        return makeError(r.error().code, std::format("group {}: {}", i, r.error().message), r.error().nativeError);
      if (auto r = _checkMver(group.mver, std::format("group {}", i)); !r) return std::unexpected{r.error()};
      groups.push_back(std::move(group));
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WMO<V>::write(fs::FileSystem& fs, const FileKey& key) const {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return makeError(ErrorCode::PathNotResolvable, "saving a WMO needs a path for the root key");
    // the derived nGroups is stamped from MOGI, so the two group tables the
    // entity does keep (info records and group files) must agree
    if (root.groupInfos.size() != groups.size())
      return makeError(ErrorCode::InvalidEntityState,
                        std::format(
                          "the MOGI group-info table holds {} records but {} group "
                          "files are baked in — every group needs its info record", root.groupInfos.size(),
                          groups.size()));

    const auto rootData = root.write();
    if (!rootData) return std::unexpected{rootData.error()};
    if (auto r = fs.addFile(*resolved.path, *rootData); !r) return std::unexpected{r.error()};

    for (std::size_t i = 0; i < groups.size(); ++i) {
      const auto groupData = groups[i].write();
      if (!groupData) return std::unexpected{groupData.error()};
      if (auto r = fs.addFile(_groupPath(*resolved.path, i), *groupData); !r)
        return makeError(r.error().code, std::format("group {}: {}", i, r.error().message), r.error().nativeError);
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
// bindings/instantiations/wmo_ranges.hpp and wmo_matrix.inl.
