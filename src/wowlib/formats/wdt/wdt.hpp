#pragma once

/** @file
    The WDT entity (namespace wowlib::formats::wdt): a map description — the
    main .wdt file plus its era's satellite files (_occ/_lgt since WoD, _fogs
    since Legion 7.2.5, _mpv since BfA) unified, versioned on the client it is
    laid out for. Satellites locate by the "{map}_occ.wdt" naming convention
    up to 8.1 and by the MPHD FileDataIDs after; a missing satellite file is
    simply absent, not an error. */

#include <array>
#include <format>
#include <span>
#include <string>
#include <string_view>

#include <wowlib/core/client_builds.hpp>
#include <wowlib/core/client_version.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/core/file_key.hpp>
#include <wowlib/core/lang.hpp>
#include <wowlib/formats/common/file_entity.hpp>
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>
#include <wowlib/formats/wdt/fogs/fogs.hpp>
#include <wowlib/formats/wdt/lights/lights.hpp>
#include <wowlib/formats/wdt/mpv/mpv.hpp>
#include <wowlib/formats/wdt/occlusion/occlusion.hpp>
#include <wowlib/formats/wdt/root/root.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::wdt {
  using root::WDTRoot;

  /** The version-agnostic base of every WDT<V> (welded as "WDT").

      This empty base exists ENTIRELY for the language bindings (Python, Lua): it
      gives the per-version WDT* classes a common welded supertype, and the module
      glue attaches the for_version/read/write/convert surface to it (dispatching
      to the concrete version). It has no role in the C++ API, where you use the
      concrete WDT<V> directly.

      @see https://wowdev.wiki/WDT */
  struct [[
      =welder::weld,
      =welder::weld_as("WDT"),
  WOWLIB_CS_FAMILY_SURFACE
      =welder::doc(R"(
        A whole map description, abstract over the client version — the main
        .wdt file and its era's satellite files (_occ/_lgt/_fogs/_mpv) as one
        entity. Construct the concrete version with WDT.for_version(expansion),
        then read()/write(); the per-version WDT* classes are subclasses. See
        https://wowdev.wiki/WDT.)")
    ]] WDTBase : FileEntityBase {};

  namespace detail {
    // --- version-range satellite traits (unwelded) ----------------------------
    // One trait per satellite introduction; welder flattens an active trait's
    // entity member onto the assembly binding.

    /** The WoD satellites: occlusion and lights. */
    template <ClientVersion V>
    struct SatellitesWod {
      [[=welder::doc(
        "The _occ.wdt occlusion satellite (WoD+); default-empty when "
        "the file does not exist.")]]
      occlusion::WDTOcclusion<V> occlusion{};

      [[=welder::doc(
        "The _lgt.wdt lights satellite (WoD+); default-empty when the "
        "file does not exist.")]]
      lights::WDTLights<V> lights{};
    };

    /** The Legion 7.2.5 satellite: volumetric fogs. */
    template <ClientVersion V>
    struct SatellitesLegion725 {
      [[=welder::doc("The _fogs.wdt volumetric-fog satellite (Legion 7.2.5+); "
        "default-empty when the file does not exist.")]]
      fogs::WDTFogs<V> fogs{};
    };

    /** The BfA satellite: particulate volumes. */
    template <ClientVersion V>
    struct SatellitesBfa {
      [[=welder::doc("The _mpv.wdt particulate-volume satellite (BfA+); "
        "default-empty when the file does not exist.")]]
      mpv::WDTParticulates<V> particulates{};
    };
  }

  namespace detail {
    /** A whole WDT (map description) for one client version: the main file
        and its era's satellite files unified as one entity. Instantiate
        through the canonicalizing wdt::WDT alias, never directly.

        The main file (WDTRoot) says which terrain tiles exist or which global
        WMO an object-only map shows; the satellites carry the map-wide
        occlusion heightmaps, placed lights, volumetric fogs and particulate
        volumes of the eras that have them. Satellites locate by the
        "{map}_occ.wdt" naming convention up to 8.1 and by the MPHD
        FileDataIDs after; a missing satellite stays default-empty and is not
        written back.

        @tparam V the client version this assembly targets.
        @see https://wowdev.wiki/WDT */
    template <ClientVersion V>
    struct [[
        =welder::weld,
        =welder::doc(R"(
          A whole map description for one client version: the main .wdt file
          and its era's satellite files (_occ/_lgt since WoD, _fogs since
          Legion 7.2.5, _mpv since BfA) as one entity. Satellites locate by
          the "{map}_occ.wdt" naming convention up to 8.1 and by the MPHD
          FileDataIDs after; a missing satellite stays default-empty. An
          entity read from a client and left unmodified rewrites
          byte-for-byte. See https://wowdev.wiki/WDT.)")
      ]] WDT
      : WDTBase,
        Slot<V, builds::WoD, SatellitesWod<V>>,
        Slot<V, builds::Legion_TombOfSargeras, SatellitesLegion725<V>>,
        Slot<V, builds::BfA_Beta_26287, SatellitesBfa<V>> {
      static constexpr ClientVersion Version = V;

      [[=welder::doc("The main file contents.")]]
      WDTRoot<V> root{};

      // read()/write() weld the (FileSystem, FileKey) load/save on LUA AND C#
      // ONLY — on Python the module glue attaches the read()/write()/convert()/
      // for_version() surface to WDTBase instead (dispatching to the concrete
      // version), so the per-version Python classes stay pure data. Lua and C#
      // have no such glue, so they take these methods directly.

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc(
          "Load the WDT and every satellite file present from a client "
          "filesystem, replacing this entity's contents.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key [[=welder::doc("the main file identity (path and/or FileDataID)")]]);

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::Cs),
        =welder::doc("Serialize and store the WDT (main file and every engaged "
          "satellite) through the filesystem's project overlay; satellite "
          "file names are derived from the main key, which must resolve "
          "to a path.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key [[=welder::doc("the main file identity; must resolve to a path")]]) const;

      [[=welder::doc(R"(
          Check the logical integrity contracts this object must satisfy to
          LOAD in the client — across the main file AND every engaged
          satellite — which write() deliberately never enforces. Call it
          before writing when you want to know the files will load. An object
          read from a client and left unmodified reports no errors.)"),
        =welder::returns(R"(every violated contract, each with its member path
                            ("root..." / "occlusion..." / "lights..." /
                            "fogs..." / "particulates..."))")]]
      ValidationReport validate() const;

      [[nodiscard]]
      [[=welder::doc("Validate and raise on the first error instead of "
          "returning a report — the assert-style face of "
          "validate()."),
        =welder::returns("nothing; raises when validate() finds any error")]]
      Result<void> ensureValid() const;

    private:
      // --- internal fs-I/O helpers (definitions at the bottom of this header) --

      /** Derive a satellite file path from the main one:
          "world\maps\azeroth\azeroth.wdt" -> "world\maps\azeroth\azeroth_occ.wdt".
          @param rootPath the main file path.
          @param suffix    the satellite suffix ("occ", "lgt", "fogs", "mpv").
          @return the derived satellite path. */
      static std::string _satellitePath(std::string_view rootPath, std::string_view suffix);
    };
  }

  /** A whole WDT — the canonicalizing face of detail::WDT: every client
      version maps to its range's first grid version (WdtAssemblyPivots),
      so e.g. one instantiation serves Vanilla through MoP. */
  template <ClientVersion V>
  using WDT = detail::WDT<canonicalVersion(V, WdtAssemblyPivots, WdtVersions)>;
}

// --- fs-level read/write definitions -----------------------------------------
// Inline in this header: the entities are templates, so the definitions must be
// visible for implicit instantiation — the library ships NO explicit
// instantiations; the bindings expand the full matrix in their own TUs.
namespace wowlib::formats::wdt {
  template <ClientVersion V>
  std::string detail::WDT<V>::_satellitePath(std::string_view rootPath, std::string_view suffix) {
    std::string_view stem = rootPath;
    if (stem.ends_with(".wdt")) stem.remove_suffix(4);
    return std::format("{}_{}.wdt", stem, suffix);
  }

  template <ClientVersion V>
  Result<void> detail::WDT<V>::read(fs::FileSystem& fs, const FileKey& key) {
    const auto rootData = fs.readFile(key);
    if (!rootData) return std::unexpected{rootData.error()};

    *this = WDT{};

    if (auto r = root.read(*rootData); !r) return std::unexpected{r.error()};
    if (root.mver != WdtVersion18)
      return makeError(ErrorCode::FormatVersionMismatch,
                        std::format("WDT MVER is {}, expected {}", root.mver, WdtVersion18));

    if constexpr (requires { this->occlusion; }) {
      // 8.1+ headers carry the satellite FileDataIDs; before that the files
      // sit next to the main one under the "{map}_<suffix>.wdt" convention.
      constexpr bool byFdid = requires { this->root.header.occFdid; };

      std::string rootPath;
      if constexpr (!byFdid) {
        const FileKey resolved = fs.resolve(key);
        if (!resolved.path)
          return makeError(ErrorCode::PathNotResolvable,
                            "satellite files need the main path (pre-8.1 clients have no "
                            "satellite FileDataIDs and the main key has no resolvable path)");
        rootPath = *resolved.path;
      }

      const auto load = [&](auto& satellite, std::uint32_t fdid, std::string_view suffix) -> Result<void> {
        if constexpr (byFdid)
          if (fdid == 0) return {}; // the map has no such satellite
        // Not if-constexpr: both arms are well-formed either way, and gcc-16
        // false-positives -Wreturn-type on constexpr-exhaustive lambdas.
        const FileKey satelliteKey = byFdid ? FileKey{FileDataID{fdid}} : FileKey{_satellitePath(rootPath, suffix)};
        if (!fs.exists(satelliteKey)) return {}; // absent satellite: stays default-empty
        const auto data = fs.readFile(satelliteKey);
        if (!data)
          return makeError(data.error().code, std::format("_{} satellite: {}", suffix, data.error().message),
                            data.error().nativeError);
        if (auto r = satellite.read(*data); !r)
          return makeError(r.error().code, std::format("_{} satellite: {}", suffix, r.error().message),
                            r.error().nativeError);
        return {};
      };

      const auto headerFdid = [&](auto pick) -> std::uint32_t {
        if constexpr (byFdid) return pick(root.header);
        else return 0;
      };

      if (auto r = load(this->occlusion, headerFdid([](const auto& h) { return h.occFdid; }), "occ"); !r) return r;
      if (auto r = load(this->lights, headerFdid([](const auto& h) { return h.lgtFdid; }), "lgt"); !r) return r;
      if constexpr (requires { this->fogs; })
        if (auto r = load(this->fogs, headerFdid([](const auto& h) {
          return h.fogsFdid;
        }), "fogs"); !r)
          return r;
      if constexpr (requires { this->particulates; })
        if (auto r = load(this->particulates, headerFdid([](const auto& h) { return h.mpvFdid; }), "mpv"); !r) return
          r;
    }
    return {};
  }

  template <ClientVersion V>
  ValidationReport detail::WDT<V>::validate() const {
    ValidationReport report;
    {
      const std::size_t mark = report.size();
      formats::detail::validateEntity(root, report);
      report.prefixFrom(mark, "root");
    }
    // Each engaged satellite validates under its member path; the version
    // ranges the entity does not carry cost nothing to gate on.
    const auto validatePart = [&report](const auto& part, std::string_view name) {
      const std::size_t mark = report.size();
      formats::detail::validateEntity(part, report);
      report.prefixFrom(mark, std::string{name});
    };
    if constexpr (requires { this->occlusion; }) validatePart(this->occlusion, "occlusion");
    if constexpr (requires { this->lights; }) validatePart(this->lights, "lights");
    if constexpr (requires { this->fogs; }) validatePart(this->fogs, "fogs");
    if constexpr (requires { this->particulates; }) validatePart(this->particulates, "particulates");
    return report;
  }

  template <ClientVersion V>
  Result<void> detail::WDT<V>::ensureValid() const {
    return validate().toResult();
  }

  template <ClientVersion V>
  Result<void> detail::WDT<V>::write(fs::FileSystem& fs, const FileKey& key) const {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return makeError(ErrorCode::PathNotResolvable, "saving a WDT needs a path for the main key");

    const auto rootData = root.write();
    if (!rootData) return std::unexpected{rootData.error()};
    if (auto r = fs.addFile(*resolved.path, *rootData); !r) return std::unexpected{r.error()};

    if constexpr (requires { this->occlusion; }) {
      // a satellite writes only when engaged: read from a file (journaled) or
      // holding user data — a default-empty one stays unwritten
      const auto store = [&](const auto& satellite, std::string_view suffix) -> Result<void> {
        if (!formats::detail::entityEngaged(satellite)) return {};
        const auto data = satellite.write();
        if (!data)
          return makeError(data.error().code, std::format("_{} satellite: {}", suffix, data.error().message),
                            data.error().nativeError);
        if (auto r = fs.addFile(_satellitePath(*resolved.path, suffix), *data); !r)
          return makeError(r.error().code, std::format("_{} satellite: {}", suffix, r.error().message),
                            r.error().nativeError);
        return {};
      };

      if (auto r = store(this->occlusion, "occ"); !r) return r;
      if (auto r = store(this->lights, "lgt"); !r) return r;
      if constexpr (requires { this->fogs; })
        if (auto r = store(this->fogs, "fogs"); !r) return r;
      if constexpr (requires { this->particulates; })
        if (auto r = store(this->particulates, "mpv"); !r) return r;
    }
    return {};
  }
}
