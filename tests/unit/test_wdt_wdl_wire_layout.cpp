#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <type_traits>

#include <wowlib/formats/wdl/wdl.hpp>
#include <wowlib/formats/wdt/wdt.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::common;

namespace wdtc = wowlib::formats::wdt::root::chunks;
namespace occc = wowlib::formats::wdt::occlusion::chunks;
namespace lgtc = wowlib::formats::wdt::lights::chunks;
namespace fogc = wowlib::formats::wdt::fogs::chunks;
namespace mpvc = wowlib::formats::wdt::mpv::chunks;
namespace wdlc = wowlib::formats::wdl::chunks;

// Sizes are asserted in the headers themselves; here we lock triviality (bulk
// memcpy reads depend on it) and a few load-bearing offsets.

template <typename... Ts>
constexpr bool all_trivially_copyable = (std::is_trivially_copyable_v<Ts> && ...);

static_assert(all_trivially_copyable<SMMapObjDef, SMDoodadDef>);
static_assert(all_trivially_copyable<wdtc::SMMapHeader<versions::wotlk>,
                                     wdtc::SMMapHeader<versions::shadowlands>,
                                     wdtc::SMAreaInfo, wdtc::MapFileDataIDs>);
static_assert(all_trivially_copyable<occc::OcclusionIndex>);
static_assert(all_trivially_copyable<lgtc::MapPointLightLegacy, lgtc::MapPointLight,
                                     lgtc::MapPointLight3, lgtc::MapSpotLight,
                                     lgtc::LightAnimation>);
static_assert(all_trivially_copyable<fogc::VolumetricFog, fogc::VolumetricFogExtra>);
static_assert(all_trivially_copyable<mpvc::ParticulatePoint, mpvc::ParticulateBounds>);
static_assert(all_trivially_copyable<wdlc::TileHeights, wdlc::TileHoles, wdlc::TileOcean,
                                     wdlc::LodMapObjDef, wdlc::LodExtent,
                                     wdlc::SkyScene, wdlc::SkySceneCondition,
                                     wdlc::SkySceneObject, wdlc::SkySceneObjectParams,
                                     wdlc::SceneLivingDef>);

TEST_CASE("wire offsets match the wowdev layout", "[formats][wdt][wdl]")
{
  STATIC_CHECK(offsetof(SMMapObjDef, position) == 0x08);
  STATIC_CHECK(offsetof(SMMapObjDef, extents) == 0x20);
  STATIC_CHECK(offsetof(SMMapObjDef, flags) == 0x38);
  STATIC_CHECK(offsetof(SMMapObjDef, scale) == 0x3E);

  STATIC_CHECK(offsetof(SMDoodadDef, rotation) == 0x14);
  STATIC_CHECK(offsetof(SMDoodadDef, scale) == 0x20);
  STATIC_CHECK(offsetof(SMDoodadDef, flags) == 0x22);

  using NewHeader = wdtc::SMMapHeader<versions::shadowlands>;
  STATIC_CHECK(offsetof(NewHeader, lgt_fdid) == 0x04);
  STATIC_CHECK(offsetof(NewHeader, wdl_fdid) == 0x18);
  STATIC_CHECK(offsetof(NewHeader, pd4_fdid) == 0x1C);

  STATIC_CHECK(offsetof(lgtc::MapPointLight, attenuation_start) == 0x14);
  STATIC_CHECK(offsetof(lgtc::MapPointLight, tile_x) == 0x2C);
  STATIC_CHECK(offsetof(lgtc::MapPointLight3, flags) == 0x34);
  STATIC_CHECK(offsetof(lgtc::MapPointLight3, scale_half) == 0x36);
  STATIC_CHECK(offsetof(lgtc::MapSpotLight, spotlight_radius) == 0x2C);
  STATIC_CHECK(offsetof(lgtc::MapSpotLight, tile_x) == 0x38);

  STATIC_CHECK(offsetof(fogc::VolumetricFog, position) == 0x1C);
  STATIC_CHECK(offsetof(fogc::VolumetricFog, rotation) == 0x2C);
  STATIC_CHECK(offsetof(fogc::VolumetricFog, flags) == 0x58);
  STATIC_CHECK(offsetof(fogc::VolumetricFog, id) == 0x64);
  STATIC_CHECK(offsetof(fogc::VolumetricFogExtra, fog_id) == 0x44);

  STATIC_CHECK(offsetof(mpvc::ParticulateBounds, point_indices) == 0x1C);
  STATIC_CHECK(offsetof(mpvc::ParticulateBounds, complete) == 0x3C);

  STATIC_CHECK(offsetof(wdlc::TileHeights, inner) == 17 * 17 * 2);
  STATIC_CHECK(offsetof(wdlc::LodMapObjDef, flags) == 0x20);
  STATIC_CHECK(offsetof(wdlc::LodExtent, radius) == 0x18);
  STATIC_CHECK(offsetof(wdlc::SkySceneObject, translation) == 0x0C);
  STATIC_CHECK(offsetof(wdlc::SkySceneObject, params_index) == 0x28);
}

// --- version-gated member existence -------------------------------------------
// (dependent variable templates: a non-dependent requires-expression makes a
// missing member a hard error instead of `false`)

template <typename T> constexpr bool has_map_fdids = requires(T v) { v.map_fdids; };
template <typename T> constexpr bool has_map_anima = requires(T v) { v.map_anima; };
template <typename T> constexpr bool has_header_fdids = requires(T v) { v.header.occ_fdid; };
template <typename T> constexpr bool has_header_something = requires(T v) { v.header.something; };
template <typename T> constexpr bool has_occlusion = requires(T v) { v.occlusion; };
template <typename T> constexpr bool has_lights = requires(T v) { v.lights; };
template <typename T> constexpr bool has_fogs = requires(T v) { v.fogs; };
template <typename T> constexpr bool has_particulates = requires(T v) { v.particulates; };
template <typename T> constexpr bool has_legacy_points = requires(T v) { v.legacy_point_lights; };
template <typename T> constexpr bool has_points = requires(T v) { v.point_lights; };
template <typename T> constexpr bool has_points_v3 = requires(T v) { v.point_lights_v3; };
template <typename T> constexpr bool has_holes = requires(T v) { v.holes; };
template <typename T> constexpr bool has_occ_meshes = requires(T v) { v.occlusion_meshes; };
template <typename T> constexpr bool has_lod_doodads = requires(T v) { v.lod_doodads; };
template <typename T> constexpr bool has_ocean_masks = requires(T v) { v.ocean_masks; };
template <typename T> constexpr bool has_sky_scenes = requires(T v) { v.sky_scenes; };
template <typename T> constexpr bool has_scene_living = requires(T v) { v.scene_living_defs; };

using WdtRootOld = formats::wdt::root::WDTRoot<versions::wotlk>;
using WdtRootNew = formats::wdt::root::WDTRoot<versions::shadowlands>;
static_assert(!has_map_fdids<WdtRootOld>, "MAID is 8.1+; a WotLK root must not carry it");
static_assert(has_map_fdids<WdtRootNew>);
static_assert(!has_map_anima<WdtRootOld>);
static_assert(has_map_anima<WdtRootNew>);
static_assert(!has_header_fdids<WdtRootOld>);
static_assert(has_header_fdids<WdtRootNew>);
static_assert(has_header_something<WdtRootOld>);

using WdtOld = formats::wdt::WDT<versions::wotlk>;
using WdtWod = formats::wdt::WDT<versions::wod>;
using WdtNew = formats::wdt::WDT<versions::shadowlands>;
static_assert(!has_occlusion<WdtOld>, "the _occ/_lgt satellites are WoD+");
static_assert(has_occlusion<WdtWod>);
static_assert(has_lights<WdtWod>);
static_assert(!has_fogs<WdtWod>, "the _fogs satellite is Legion 7.2.5+");
static_assert(!has_particulates<WdtWod>, "the _mpv satellite is BfA+");
static_assert(has_fogs<WdtNew>);
static_assert(has_particulates<WdtNew>);

using WdtLightsWod = formats::wdt::lights::WDTLights<versions::wod>;
using WdtLightsNew = formats::wdt::lights::WDTLights<versions::shadowlands>;
static_assert(has_legacy_points<WdtLightsWod>);
static_assert(!has_points<WdtLightsWod>);
static_assert(!has_legacy_points<WdtLightsNew>);
static_assert(has_points<WdtLightsNew>);
static_assert(has_points_v3<WdtLightsNew>);

using WdlVanilla = formats::wdl::WDL<versions::vanilla>;
using WdlTbc = formats::wdl::WDL<versions::tbc>;
using WdlOld = formats::wdl::WDL<versions::wotlk>;
using WdlNew = formats::wdl::WDL<versions::shadowlands>;
using WdlTww = formats::wdl::WDL<versions::tww>;
static_assert(!has_holes<WdlVanilla>, "MAHO debuts in TBC, so vanilla carries none");
static_assert(has_holes<WdlTbc>, "MAHO debuts in TBC (not WotLK, pace wowdev.wiki)");
static_assert(has_occ_meshes<WdlVanilla>);
static_assert(has_holes<WdlOld>);
static_assert(!has_lod_doodads<WdlOld>);
static_assert(!has_occ_meshes<WdlNew>, "MAOC is pre-Legion");
static_assert(has_lod_doodads<WdlNew>);
static_assert(has_ocean_masks<WdlNew>);
static_assert(has_sky_scenes<WdlNew>);
static_assert(!has_scene_living<WdlNew>, "MSLD is TWW+");
static_assert(has_scene_living<WdlTww>);

// every version collapses onto its range representative; MAHO splits vanilla
// (MARE only) from TBC..WoD (MARE + hole masks), which share one instantiation
static_assert(!std::is_same_v<formats::wdl::WDL<versions::vanilla>,
                              formats::wdl::WDL<versions::tbc>>);
static_assert(std::is_same_v<formats::wdl::WDL<versions::tbc>,
                             formats::wdl::WDL<versions::wotlk>>);
static_assert(std::is_same_v<formats::wdl::WDL<versions::wotlk>,
                             formats::wdl::WDL<versions::wod>>);
static_assert(std::is_same_v<formats::wdt::WDT<versions::vanilla>,
                             formats::wdt::WDT<versions::mop>>);
static_assert(!std::is_same_v<formats::wdt::WDT<versions::mop>,
                              formats::wdt::WDT<versions::wod>>);
static_assert(std::is_same_v<formats::wdt::root::WDTRoot<versions::vanilla>,
                             formats::wdt::root::WDTRoot<versions::legion>>);
static_assert(std::is_same_v<formats::wdt::WDT<versions::shadowlands>,
                             formats::wdt::WDT<versions::dragonflight>>);
