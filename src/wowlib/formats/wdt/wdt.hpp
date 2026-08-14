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
#include <wowlib/formats/common/version_slot.hpp>
#include <wowlib/formats/wdt/boundaries.hpp>
#include <wowlib/formats/wdt/fogs/fogs.hpp>
#include <wowlib/formats/wdt/lights/lights.hpp>
#include <wowlib/formats/wdt/mpv/mpv.hpp>
#include <wowlib/formats/wdt/occlusion/occlusion.hpp>
#include <wowlib/formats/wdt/root/root.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::wdt
{
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
    =welder::doc(R"(
        A whole map description, abstract over the client version — the main
        .wdt file and its era's satellite files (_occ/_lgt/_fogs/_mpv) as one
        entity. Construct the concrete version with WDT.for_version(expansion),
        then read()/write(); the per-version WDT* classes are subclasses. See
        https://wowdev.wiki/WDT.)")
  ]] WDTBase
  {
  };

  namespace detail
  {
    // --- version-range satellite traits (unwelded) ----------------------------
    // One trait per satellite introduction; welder flattens an active trait's
    // entity member onto the assembly binding.

    /** The WoD satellites: occlusion and lights. */
    template <ClientVersion V>
    struct SatellitesWod
    {
      [[=welder::doc("The _occ.wdt occlusion satellite (WoD+); default-empty when "
                     "the file does not exist.")]]
      occlusion::WDTOcclusion<V> occlusion{};

      [[=welder::doc("The _lgt.wdt lights satellite (WoD+); default-empty when the "
                     "file does not exist.")]]
      lights::WDTLights<V> lights{};
    };

    /** The Legion 7.2.5 satellite: volumetric fogs. */
    template <ClientVersion V>
    struct SatellitesLegion725
    {
      [[=welder::doc("The _fogs.wdt volumetric-fog satellite (Legion 7.2.5+); "
                     "default-empty when the file does not exist.")]]
      fogs::WDTFogs<V> fogs{};
    };

    /** The BfA satellite: particulate volumes. */
    template <ClientVersion V>
    struct SatellitesBfa
    {
      [[=welder::doc("The _mpv.wdt particulate-volume satellite (BfA+); "
                     "default-empty when the file does not exist.")]]
      mpv::WDTParticulates<V> particulates{};
    };
  }

  namespace detail
  {
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
        slot<V, builds::WoD, SatellitesWod<V>>,
        slot<V, builds::Legion_TombOfSargeras, SatellitesLegion725<V>>,
        slot<V, builds::BfA_Beta_26287, SatellitesBfa<V>>
    {
      static constexpr ClientVersion version = V;

      [[=welder::doc("The main file contents.")]]
      WDTRoot<V> root{};

      // read()/write() weld the (FileSystem, FileKey) load/save on LUA AND C#
      // ONLY — on Python the module glue attaches the read()/write()/convert()/
      // for_version() surface to WDTBase instead (dispatching to the concrete
      // version), so the per-version Python classes stay pure data. Lua and C#
      // have no such glue, so they take these methods directly.

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::cs),
        =welder::doc("Load the WDT and every satellite file present from a client "
                     "filesystem, replacing this entity's contents.")]]
      Result<void> read(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                        const FileKey& key
                        [[=welder::doc("the main file identity (path and/or FileDataID)")]]);

      [[=welder::mark::only(welder::lang::lua, wowlib::lang::cs),
        =welder::doc("Serialize and store the WDT (main file and every engaged "
                     "satellite) through the filesystem's project overlay; satellite "
                     "file names are derived from the main key, which must resolve "
                     "to a path.")]]
      Result<void> write(fs::FileSystem& fs [[=welder::doc("the filesystem gateway")]],
                         const FileKey& key
                         [[=welder::doc("the main file identity; must resolve to a path")]]) const;

    private:
      // --- internal fs-I/O helpers (definitions at the bottom of this header) --

      /** Derive a satellite file path from the main one:
          "world\maps\azeroth\azeroth.wdt" -> "world\maps\azeroth\azeroth_occ.wdt".
          @param root_path the main file path.
          @param suffix    the satellite suffix ("occ", "lgt", "fogs", "mpv").
          @return the derived satellite path. */
      static std::string satellite_path(std::string_view root_path, std::string_view suffix);
    };
  }

  /** A whole WDT — the canonicalizing face of detail::WDT: every client
      version maps to its range's first grid version (wdt_assembly_pivots),
      so e.g. one instantiation serves Vanilla through MoP. */
  template <ClientVersion V>
  using WDT = detail::WDT<canonical_version(V, wdt_assembly_pivots, wdt_versions)>;
}

// --- fs-level read/write definitions -----------------------------------------
// Inline in this header: the entities are templates, so the definitions must be
// visible for implicit instantiation — the library ships NO explicit
// instantiations; the bindings expand the full matrix in their own TUs.
namespace wowlib::formats::wdt
{
  template <ClientVersion V>
  std::string detail::WDT<V>::satellite_path(std::string_view root_path,
                                             std::string_view suffix)
  {
    std::string_view stem = root_path;
    if (stem.ends_with(".wdt"))
      stem.remove_suffix(4);
    return std::format("{}_{}.wdt", stem, suffix);
  }

  template <ClientVersion V>
  Result<void> detail::WDT<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto root_data = fs.read_file(key);
    if (!root_data)
      return std::unexpected{root_data.error()};

    *this = WDT{};

    if (auto r = root.read(*root_data); !r)
      return std::unexpected{r.error()};
    if (root.mver != wdt_version_18)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("WDT MVER is {}, expected {}", root.mver, wdt_version_18));

    if constexpr (requires { this->occlusion; })
    {
      // 8.1+ headers carry the satellite FileDataIDs; before that the files
      // sit next to the main one under the "{map}_<suffix>.wdt" convention.
      constexpr bool by_fdid = requires { this->root.header.occ_fdid; };

      std::string root_path;
      if constexpr (!by_fdid)
      {
        const FileKey resolved = fs.resolve(key);
        if (!resolved.path)
          return make_error(ErrorCode::PathNotResolvable,
                            "satellite files need the main path (pre-8.1 clients have no "
                            "satellite FileDataIDs and the main key has no resolvable path)");
        root_path = *resolved.path;
      }

      const auto load = [&](auto& satellite, std::uint32_t fdid,
                            std::string_view suffix) -> Result<void> {
        if constexpr (by_fdid)
          if (fdid == 0)
            return {};  // the map has no such satellite
        const FileKey satellite_key = [&]() -> FileKey {
          if constexpr (by_fdid)
            return FileKey{FileDataID{fdid}};
          else
            return FileKey{satellite_path(root_path, suffix)};
        }();
        if (!fs.exists(satellite_key))
          return {};  // absent satellite: stays default-empty
        const auto data = fs.read_file(satellite_key);
        if (!data)
          return make_error(data.error().code,
                            std::format("_{} satellite: {}", suffix, data.error().message),
                            data.error().native_error);
        if (auto r = satellite.read(*data); !r)
          return make_error(r.error().code,
                            std::format("_{} satellite: {}", suffix, r.error().message),
                            r.error().native_error);
        return {};
      };

      const auto header_fdid = [&](auto pick) -> std::uint32_t {
        if constexpr (by_fdid)
          return pick(root.header);
        else
          return 0;
      };

      if (auto r = load(this->occlusion, header_fdid([](const auto& h) { return h.occ_fdid; }),
                        "occ");
          !r)
        return r;
      if (auto r = load(this->lights, header_fdid([](const auto& h) { return h.lgt_fdid; }),
                        "lgt");
          !r)
        return r;
      if constexpr (requires { this->fogs; })
        if (auto r = load(this->fogs, header_fdid([](const auto& h) { return h.fogs_fdid; }),
                          "fogs");
            !r)
          return r;
      if constexpr (requires { this->particulates; })
        if (auto r = load(this->particulates,
                          header_fdid([](const auto& h) { return h.mpv_fdid; }), "mpv");
            !r)
          return r;
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::WDT<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "saving a WDT needs a path for the main key");

    const auto root_data = root.write();
    if (!root_data)
      return std::unexpected{root_data.error()};
    if (auto r = fs.add_file(*resolved.path, *root_data); !r)
      return std::unexpected{r.error()};

    if constexpr (requires { this->occlusion; })
    {
      // a satellite writes only when engaged: read from a file (journaled) or
      // holding user data — a default-empty one stays unwritten
      const auto store = [&](const auto& satellite, std::string_view suffix) -> Result<void> {
        if (!formats::detail::entity_engaged(satellite))
          return {};
        const auto data = satellite.write();
        if (!data)
          return make_error(data.error().code,
                            std::format("_{} satellite: {}", suffix, data.error().message),
                            data.error().native_error);
        if (auto r = fs.add_file(satellite_path(*resolved.path, suffix), *data); !r)
          return make_error(r.error().code,
                            std::format("_{} satellite: {}", suffix, r.error().message),
                            r.error().native_error);
        return {};
      };

      if (auto r = store(this->occlusion, "occ"); !r)
        return r;
      if (auto r = store(this->lights, "lgt"); !r)
        return r;
      if constexpr (requires { this->fogs; })
        if (auto r = store(this->fogs, "fogs"); !r)
          return r;
      if constexpr (requires { this->particulates; })
        if (auto r = store(this->particulates, "mpv"); !r)
          return r;
    }
    return {};
  }
}
