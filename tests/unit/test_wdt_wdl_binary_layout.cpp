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
constexpr bool AllTriviallyCopyable = (std::is_trivially_copyable_v<Ts> && ...);

static_assert(AllTriviallyCopyable<SMMapObjDef, SMDoodadDef>);
static_assert(AllTriviallyCopyable<wdtc::SMMapHeader<versions::Wotlk>,
                                     wdtc::SMMapHeader<versions::Shadowlands>,
                                     wdtc::SMAreaInfo, wdtc::MapFileDataIDs>);
static_assert(AllTriviallyCopyable<occc::OcclusionIndex>);
static_assert(AllTriviallyCopyable<lgtc::MapPointLightLegacy, lgtc::MapPointLight,
                                     lgtc::MapPointLight3, lgtc::MapSpotLight,
                                     lgtc::LightAnimation>);
static_assert(AllTriviallyCopyable<fogc::VolumetricFog, fogc::VolumetricFogExtra>);
static_assert(AllTriviallyCopyable<mpvc::ParticulatePoint, mpvc::ParticulateBounds>);
static_assert(AllTriviallyCopyable<wdlc::TileHeights, wdlc::TileHoles, wdlc::TileOcean,
                                     wdlc::LodMapObjDef, wdlc::LodExtent,
                                     wdlc::SkyScene, wdlc::SkySceneCondition,
                                     wdlc::SkySceneObject, wdlc::SkySceneObjectParams,
                                     wdlc::SceneLivingDef>);

TEST_CASE("binary offsets match the wowdev layout", "[formats][wdt][wdl]")
{
  STATIC_CHECK(offsetof(SMMapObjDef, position) == 0x08);
  STATIC_CHECK(offsetof(SMMapObjDef, extents) == 0x20);
  STATIC_CHECK(offsetof(SMMapObjDef, flags) == 0x38);
  STATIC_CHECK(offsetof(SMMapObjDef, scale) == 0x3E);

  STATIC_CHECK(offsetof(SMDoodadDef, rotation) == 0x14);
  STATIC_CHECK(offsetof(SMDoodadDef, scale) == 0x20);
  STATIC_CHECK(offsetof(SMDoodadDef, flags) == 0x22);

  using NewHeader = wdtc::SMMapHeader<versions::Shadowlands>;
  STATIC_CHECK(offsetof(NewHeader, lgtFdid) == 0x04);
  STATIC_CHECK(offsetof(NewHeader, wdlFdid) == 0x18);
  STATIC_CHECK(offsetof(NewHeader, pd4Fdid) == 0x1C);

  STATIC_CHECK(offsetof(lgtc::MapPointLight, attenuationStart) == 0x14);
  STATIC_CHECK(offsetof(lgtc::MapPointLight, tileX) == 0x2C);
  STATIC_CHECK(offsetof(lgtc::MapPointLight3, flags) == 0x34);
  STATIC_CHECK(offsetof(lgtc::MapPointLight3, scaleHalf) == 0x36);
  STATIC_CHECK(offsetof(lgtc::MapSpotLight, spotlightRadius) == 0x2C);
  STATIC_CHECK(offsetof(lgtc::MapSpotLight, tileX) == 0x38);

  STATIC_CHECK(offsetof(fogc::VolumetricFog, position) == 0x1C);
  STATIC_CHECK(offsetof(fogc::VolumetricFog, rotation) == 0x2C);
  STATIC_CHECK(offsetof(fogc::VolumetricFog, flags) == 0x58);
  STATIC_CHECK(offsetof(fogc::VolumetricFog, id) == 0x64);
  STATIC_CHECK(offsetof(fogc::VolumetricFogExtra, fogId) == 0x44);

  STATIC_CHECK(offsetof(mpvc::ParticulateBounds, pointIndices) == 0x1C);
  STATIC_CHECK(offsetof(mpvc::ParticulateBounds, complete) == 0x3C);

  STATIC_CHECK(offsetof(wdlc::TileHeights, inner) == 17 * 17 * 2);
  STATIC_CHECK(offsetof(wdlc::LodMapObjDef, flags) == 0x20);
  STATIC_CHECK(offsetof(wdlc::LodExtent, radius) == 0x18);
  STATIC_CHECK(offsetof(wdlc::SkySceneObject, translation) == 0x0C);
  STATIC_CHECK(offsetof(wdlc::SkySceneObject, paramsIndex) == 0x28);
}

// --- version-gated member existence -------------------------------------------
// (dependent variable templates: a non-dependent requires-expression makes a
// missing member a hard error instead of `false`)

template <typename T> constexpr bool HasMapFdids = requires(T v) { v.mapFdids; };
template <typename T> constexpr bool HasMapAnima = requires(T v) { v.mapAnima; };
template <typename T> constexpr bool HasHeaderFdids = requires(T v) { v.header.occFdid; };
template <typename T> constexpr bool HasHeaderSomething = requires(T v) { v.header.something; };
template <typename T> constexpr bool HasOcclusion = requires(T v) { v.occlusion; };
template <typename T> constexpr bool HasLights = requires(T v) { v.lights; };
template <typename T> constexpr bool HasFogs = requires(T v) { v.fogs; };
template <typename T> constexpr bool HasParticulates = requires(T v) { v.particulates; };
template <typename T> constexpr bool HasLegacyPoints = requires(T v) { v.legacyPointLights; };
template <typename T> constexpr bool HasPoints = requires(T v) { v.pointLights; };
template <typename T> constexpr bool HasPointsV3 = requires(T v) { v.pointLightsV3; };
template <typename T> constexpr bool HasHoles = requires(T v) { v.holes; };
template <typename T> constexpr bool HasOccMeshes = requires(T v) { v.occlusionMeshes; };
template <typename T> constexpr bool HasLodDoodads = requires(T v) { v.lodDoodads; };
template <typename T> constexpr bool HasOceanMasks = requires(T v) { v.oceanMasks; };
template <typename T> constexpr bool HasSkyScenes = requires(T v) { v.skyScenes; };
template <typename T> constexpr bool HasSceneLiving = requires(T v) { v.sceneLivingDefs; };

using WdtRootOld = formats::wdt::root::WDTRoot<versions::Wotlk>;
using WdtRootNew = formats::wdt::root::WDTRoot<versions::Shadowlands>;
static_assert(!HasMapFdids<WdtRootOld>, "MAID is 8.1+; a WotLK root must not carry it");
static_assert(HasMapFdids<WdtRootNew>);
static_assert(!HasMapAnima<WdtRootOld>);
static_assert(HasMapAnima<WdtRootNew>);
static_assert(!HasHeaderFdids<WdtRootOld>);
static_assert(HasHeaderFdids<WdtRootNew>);
static_assert(HasHeaderSomething<WdtRootOld>);

using WdtOld = formats::wdt::WDT<versions::Wotlk>;
using WdtWod = formats::wdt::WDT<versions::Wod>;
using WdtNew = formats::wdt::WDT<versions::Shadowlands>;
static_assert(!HasOcclusion<WdtOld>, "the _occ/_lgt satellites are WoD+");
static_assert(HasOcclusion<WdtWod>);
static_assert(HasLights<WdtWod>);
static_assert(!HasFogs<WdtWod>, "the _fogs satellite is Legion 7.2.5+");
static_assert(!HasParticulates<WdtWod>, "the _mpv satellite is BfA+");
static_assert(HasFogs<WdtNew>);
static_assert(HasParticulates<WdtNew>);

using WdtLightsWod = formats::wdt::lights::WDTLights<versions::Wod>;
using WdtLightsNew = formats::wdt::lights::WDTLights<versions::Shadowlands>;
static_assert(HasLegacyPoints<WdtLightsWod>);
static_assert(!HasPoints<WdtLightsWod>);
static_assert(!HasLegacyPoints<WdtLightsNew>);
static_assert(HasPoints<WdtLightsNew>);
static_assert(HasPointsV3<WdtLightsNew>);

using WdlVanilla = formats::wdl::WDL<versions::Vanilla>;
using WdlTbc = formats::wdl::WDL<versions::Tbc>;
using WdlOld = formats::wdl::WDL<versions::Wotlk>;
using WdlNew = formats::wdl::WDL<versions::Shadowlands>;
using WdlTww = formats::wdl::WDL<versions::Tww>;
static_assert(!HasHoles<WdlVanilla>, "MAHO debuts in TBC, so vanilla carries none");
static_assert(HasHoles<WdlTbc>, "MAHO debuts in TBC (not WotLK, pace wowdev.wiki)");
static_assert(HasOccMeshes<WdlVanilla>);
static_assert(HasHoles<WdlOld>);
static_assert(!HasLodDoodads<WdlOld>);
static_assert(!HasOccMeshes<WdlNew>, "MAOC is pre-Legion");
static_assert(HasLodDoodads<WdlNew>);
static_assert(HasOceanMasks<WdlNew>);
static_assert(HasSkyScenes<WdlNew>);
static_assert(!HasSceneLiving<WdlNew>, "MSLD is TWW+");
static_assert(HasSceneLiving<WdlTww>);

// every version collapses onto its range representative; MAHO splits vanilla
// (MARE only) from TBC..WoD (MARE + hole masks), which share one instantiation
static_assert(!std::is_same_v<formats::wdl::WDL<versions::Vanilla>,
                              formats::wdl::WDL<versions::Tbc>>);
static_assert(std::is_same_v<formats::wdl::WDL<versions::Tbc>,
                             formats::wdl::WDL<versions::Wotlk>>);
static_assert(std::is_same_v<formats::wdl::WDL<versions::Wotlk>,
                             formats::wdl::WDL<versions::Wod>>);
static_assert(std::is_same_v<formats::wdt::WDT<versions::Vanilla>,
                             formats::wdt::WDT<versions::Mop>>);
static_assert(!std::is_same_v<formats::wdt::WDT<versions::Mop>,
                              formats::wdt::WDT<versions::Wod>>);
static_assert(std::is_same_v<formats::wdt::root::WDTRoot<versions::Vanilla>,
                             formats::wdt::root::WDTRoot<versions::Legion>>);
static_assert(std::is_same_v<formats::wdt::WDT<versions::Shadowlands>,
                             formats::wdt::WDT<versions::Dragonflight>>);
