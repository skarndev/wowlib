#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <span>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/m2/offset_block.hpp>
#include <wowlib/formats/m2/root/root.hpp>
#include <wowlib/formats/m2/skin/skin.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::m2;
// NOT `using namespace ...m2::root` — the raw root::M2Root template would
// make every unqualified M2Root ambiguous against the m2::M2Root alias
using namespace wowlib::formats::m2::root::record;
using namespace wowlib::formats::m2::skin;
using wowlib::formats::m2::root::Md20Magic;

namespace
{
  inline constexpr ClientVersion Va = versions::Vanilla;
  inline constexpr ClientVersion Tb = versions::Tbc;
  inline constexpr ClientVersion Wk = versions::Wotlk;
  inline constexpr ClientVersion Ca = versions::Cata;
  inline constexpr ClientVersion Wo = versions::Wod;
}

// --- track and record offset layouts, per era (wowdev.wiki/M2) -----------------

static_assert(layoutSize<M2Track<C3Vector, Va>, Va>() == 28);
static_assert(layoutSize<M2Track<C3Vector, Wk>, Wk>() == 20);
static_assert(layoutSize<M2TrackBase<Va>, Va>() == 20);
static_assert(layoutSize<M2TrackBase<Wk>, Wk>() == 12);
static_assert(layoutSize<FBlock<C3Vector>, Wk>() == 16);

static_assert(layoutSize<M2CompBone<Va>, Va>() == 108);
static_assert(layoutSize<M2CompBone<Tb>, Tb>() == 112);
static_assert(layoutSize<M2CompBone<Wk>, Wk>() == 88);  // 0x58

static_assert(layoutSize<M2Texture, Wk>() == 16);
static_assert(layoutSize<M2Color<Wk>, Wk>() == 40);
static_assert(layoutSize<M2TextureWeight<Wk>, Wk>() == 20);
static_assert(layoutSize<M2TextureTransform<Wk>, Wk>() == 60);

static_assert(layoutSize<M2Attachment<Wk>, Wk>() == 40);   // 0x28
static_assert(layoutSize<M2Event<Wk>, Wk>() == 36);
static_assert(layoutSize<M2Light<Wk>, Wk>() == 156);       // 0x9C
static_assert(layoutSize<M2Camera<Va>, Va>() == 124);
static_assert(layoutSize<M2Camera<Wk>, Wk>() == 100);
static_assert(layoutSize<M2Camera<Ca>, Ca>() == 116);

static_assert(layoutSize<M2Ribbon<Va>, Va>() == 220);
static_assert(layoutSize<M2Ribbon<Wk>, Wk>() == 176);      // 0xB0
static_assert(layoutSize<M2Particle<Va>, Va>() == 504);
static_assert(layoutSize<M2Particle<Wk>, Wk>() == 476);
static_assert(layoutSize<M2Particle<Ca>, Ca>() == 492);

static_assert(layoutSize<M2SkinProfile<Wk>, Wk>() == 44);
static_assert(layoutSize<M2SkinProfile<Ca>, Ca>() == 52);

static_assert(OffsetEntity<M2Root<Wk>>);
static_assert(OffsetEntity<Skin<Wk>>);
static_assert(SelfSerializing<M2Root<Wk>>);  // the Legion MD21 payload path

TEST_CASE("MD20 header image sizes match the client eras", "[formats][m2]")
{
  // wowdev header layouts: vanilla/TBC 0x144 (324), WotLK+ 0x130 (304).
  CHECK(M2Root<Va>{}.imageSize() == 324);
  CHECK(M2Root<Tb>{}.imageSize() == 324);
  CHECK(M2Root<Wk>{}.imageSize() == 304);
  CHECK(M2Root<Ca>{}.imageSize() == 304);
  CHECK(M2Root<Wo>{}.imageSize() == 304);

  M2Root<Tb> gated;
  gated.globalFlags = 0x8;  // engages textureCombinerCombos
  CHECK(gated.imageSize() == 332);
}

TEST_CASE("skin files carry the magic ahead of the flattened profile", "[formats][m2]")
{
  Skin<Wk> skin;
  skin.profile.vertices = {0, 1, 2};
  skin.profile.indices = {0, 1, 2};
  skin.profile.bones = {{0, 0, 0, 0}, {1, 0, 0, 0}, {2, 0, 0, 0}};
  M2SkinSection<Wk> section;
  section.vertexCount = 3;
  section.indexCount = 3;
  section.boneCount = 1;
  skin.profile.submeshes = {section};
  M2Batch batch;
  batch.textureCount = 1;
  skin.profile.batches = {batch};
  skin.profile.boneCountMax = 64;

  auto bytes = skin.write();
  REQUIRE(bytes.has_value());
  std::uint32_t magic = 0;
  std::memcpy(&magic, bytes->data(), 4);
  CHECK(magic == SkinMagic);

  Skin<Wk> back;
  REQUIRE(back.read(*bytes).has_value());
  CHECK(back == skin);
}

TEST_CASE("a synthetic WotLK body round-trips semantically", "[formats][m2]")
{
  M2Root<Wk> model;
  model.name = "unit_test_model";
  model.globalFlags = 0;
  model.globalLoops = {{1500}};
  M2Sequence<Wk> stand;
  stand.id = 0;
  stand.duration = 2000;
  stand.flags = 0x20;  // data in .m2
  model.sequences = {stand};
  model.sequenceLookups = {0};

  M2CompBone<Wk> bone;
  bone.keyBoneId = -1;
  bone.translation.timestamps = {{0, 1000, 2000}};
  bone.translation.values = {{{0, 0, 0}, {0, 1, 0}, {0, 0, 0}}};
  bone.rotation.timestamps = {{0}};
  bone.rotation.values = {{{}}};
  model.bones = {bone};
  model.keyBoneLookup = {-1};

  model.vertices.resize(3);
  model.textures = {{0, 0x3, "textures/unit_test.blp"}};
  model.materials = {{0x10, 1}};
  model.boneLookupTable = {0};
  model.textureLookupTable = {0};
  model.transparencyLookupTable = {0};
  M2TextureWeight<Wk> weight;
  weight.weight.timestamps = {{0}};
  weight.weight.values = {{fixed16{0x7FFF}}};
  model.textureWeights = {weight};
  model.numSkinProfiles = 1;

  M2Ribbon<Wk> ribbon;
  ribbon.textureIndices = {0};
  ribbon.materialIndices = {0};
  ribbon.priorityPlane = 2;
  model.ribbonEmitters = {ribbon};

  M2Particle<Wk> particle;
  particle.geometryModelFilename = "spells/unit_test.mdx";
  particle.colorTrack.timestamps = {0, 16384, 32767};
  particle.colorTrack.keys = {{1, 1, 1}, {0.5f, 0.5f, 0.5f}, {0, 0, 0}};
  model.particleEmitters = {particle};

  auto bytes = model.write();
  REQUIRE(bytes.has_value());
  M2Root<Wk> back;
  {
    auto r = back.read(*bytes);
    INFO((r ? std::string{} : r.error().message));
    REQUIRE(r.has_value());
  }
  CHECK(back == model);
  std::uint32_t magic = 0;
  std::uint32_t version = 0;
  std::memcpy(&magic, bytes->data(), 4);
  std::memcpy(&version, bytes->data() + 4, 4);
  CHECK(magic == Md20Magic);
  CHECK(version == 264);
}

TEST_CASE("a synthetic vanilla body embeds its skin profiles", "[formats][m2]")
{
  M2Root<Va> model;
  model.name = "vanilla_test";
  M2Sequence<Va> stand;
  stand.startTimestamp = 0;
  stand.endTimestamp = 1000;
  model.sequences = {stand};

  M2CompBone<Va> bone;
  bone.rotation.interpolationRanges = {{0, 1}};
  bone.rotation.timestamps = {0, 1000};
  bone.rotation.values = {{}, {}};
  model.bones = {bone};

  model.vertices.resize(4);
  M2SkinProfile<Va> profile;
  profile.vertices = {0, 1, 2, 3};
  profile.indices = {0, 1, 2, 2, 1, 3};
  profile.bones = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
  M2SkinSection<Va> section;
  section.vertexCount = 4;
  section.indexCount = 6;
  profile.submeshes = {section};
  profile.batches = {{}};
  model.skinProfiles = {profile};

  auto bytes = model.write();
  REQUIRE(bytes.has_value());
  M2Root<Va> back;
  {
    auto r = back.read(*bytes);
    INFO((r ? std::string{} : r.error().message));
    REQUIRE(r.has_value());
  }
  CHECK(back == model);
  std::uint32_t version = 0;
  std::memcpy(&version, bytes->data() + 4, 4);
  CHECK(version == 256);
}
