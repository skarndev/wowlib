#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <span>
#include <vector>

#include <wowlib/core/client_version.hpp>
#include <wowlib/formats/common/offset_serializer.hpp>
#include <wowlib/formats/m2/data.hpp>
#include <wowlib/formats/m2/skin.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::m2;
using formats::detail::wire_size;

namespace
{
  inline constexpr ClientVersion va = versions::vanilla;
  inline constexpr ClientVersion tb = versions::tbc;
  inline constexpr ClientVersion wk = versions::wotlk;
  inline constexpr ClientVersion ca = versions::cata;
  inline constexpr ClientVersion wo = versions::wod;
}

// --- track and record wire layouts, per era (wowdev.wiki/M2) -----------------

static_assert(wire_size<M2Track<C3Vector, va>, va>() == 28);
static_assert(wire_size<M2Track<C3Vector, wk>, wk>() == 20);
static_assert(wire_size<M2TrackBase<va>, va>() == 20);
static_assert(wire_size<M2TrackBase<wk>, wk>() == 12);
static_assert(wire_size<FBlock<C3Vector>, wk>() == 16);

static_assert(wire_size<M2CompBone<va>, va>() == 108);
static_assert(wire_size<M2CompBone<tb>, tb>() == 112);
static_assert(wire_size<M2CompBone<wk>, wk>() == 88);  // 0x58

static_assert(wire_size<M2Texture, wk>() == 16);
static_assert(wire_size<M2Color<wk>, wk>() == 40);
static_assert(wire_size<M2TextureWeight<wk>, wk>() == 20);
static_assert(wire_size<M2TextureTransform<wk>, wk>() == 60);

static_assert(wire_size<M2Attachment<wk>, wk>() == 40);   // 0x28
static_assert(wire_size<M2Event<wk>, wk>() == 36);
static_assert(wire_size<M2Light<wk>, wk>() == 156);       // 0x9C
static_assert(wire_size<M2Camera<va>, va>() == 124);
static_assert(wire_size<M2Camera<wk>, wk>() == 100);
static_assert(wire_size<M2Camera<ca>, ca>() == 116);

static_assert(wire_size<M2Ribbon<va>, va>() == 220);
static_assert(wire_size<M2Ribbon<wk>, wk>() == 176);      // 0xB0
static_assert(wire_size<M2Particle<va>, va>() == 504);
static_assert(wire_size<M2Particle<wk>, wk>() == 476);
static_assert(wire_size<M2Particle<ca>, ca>() == 492);

static_assert(wire_size<M2SkinProfile<wk>, wk>() == 44);
static_assert(wire_size<M2SkinProfile<ca>, ca>() == 52);

static_assert(OffsetEntity<M2Data<wk>>);
static_assert(OffsetEntity<Skin<wk>>);
static_assert(SelfSerializing<M2Data<wk>>);  // the Legion MD21 payload path

TEST_CASE("MD20 header image sizes match the client eras", "[formats][m2]")
{
  // wowdev header layouts: vanilla/TBC 0x144 (324), WotLK+ 0x130 (304).
  CHECK(formats::detail::entity_image_size(M2Data<va>{}) == 324);
  CHECK(formats::detail::entity_image_size(M2Data<tb>{}) == 324);
  CHECK(formats::detail::entity_image_size(M2Data<wk>{}) == 304);
  CHECK(formats::detail::entity_image_size(M2Data<ca>{}) == 304);
  CHECK(formats::detail::entity_image_size(M2Data<wo>{}) == 304);

  M2Data<tb> gated;
  gated.global_flags = 0x8;  // engages texture_combiner_combos
  CHECK(formats::detail::entity_image_size(gated) == 332);
}

TEST_CASE("skin files carry the magic ahead of the flattened profile", "[formats][m2]")
{
  Skin<wk> skin;
  skin.profile.vertices = {0, 1, 2};
  skin.profile.indices = {0, 1, 2};
  skin.profile.bones = {{0, 0, 0, 0}, {1, 0, 0, 0}, {2, 0, 0, 0}};
  M2SkinSection<wk> section;
  section.vertex_count = 3;
  section.index_count = 3;
  section.bone_count = 1;
  skin.profile.submeshes = {section};
  M2Batch batch;
  batch.texture_count = 1;
  skin.profile.batches = {batch};
  skin.profile.bone_count_max = 64;

  auto bytes = skin.write();
  REQUIRE(bytes.has_value());
  std::uint32_t magic = 0;
  std::memcpy(&magic, bytes->data(), 4);
  CHECK(magic == skin_magic);

  Skin<wk> back;
  REQUIRE(back.read(*bytes).has_value());
  CHECK(back == skin);
}

TEST_CASE("a synthetic WotLK body round-trips semantically", "[formats][m2]")
{
  M2Data<wk> model;
  model.name = "unit_test_model";
  model.global_flags = 0;
  model.global_loops = {{1500}};
  M2Sequence<wk> stand;
  stand.id = 0;
  stand.duration = 2000;
  stand.flags = 0x20;  // data in .m2
  model.sequences = {stand};
  model.sequence_lookups = {0};

  M2CompBone<wk> bone;
  bone.key_bone_id = -1;
  bone.translation.timestamps = {{0, 1000, 2000}};
  bone.translation.values = {{{0, 0, 0}, {0, 1, 0}, {0, 0, 0}}};
  bone.rotation.timestamps = {{0}};
  bone.rotation.values = {{{}}};
  model.bones = {bone};
  model.key_bone_lookup = {-1};

  model.vertices.resize(3);
  model.textures = {{0, 0x3, "textures/unit_test.blp"}};
  model.materials = {{0x10, 1}};
  model.bone_lookup_table = {0};
  model.texture_lookup_table = {0};
  model.transparency_lookup_table = {0};
  M2TextureWeight<wk> weight;
  weight.weight.timestamps = {{0}};
  weight.weight.values = {{fixed16{0x7FFF}}};
  model.texture_weights = {weight};
  model.num_skin_profiles = 1;

  M2Ribbon<wk> ribbon;
  ribbon.texture_indices = {0};
  ribbon.material_indices = {0};
  ribbon.priority_plane = 2;
  model.ribbon_emitters = {ribbon};

  M2Particle<wk> particle;
  particle.geometry_model_filename = "spells/unit_test.mdx";
  particle.color_track.timestamps = {0, 16384, 32767};
  particle.color_track.keys = {{1, 1, 1}, {0.5f, 0.5f, 0.5f}, {0, 0, 0}};
  model.particle_emitters = {particle};

  auto bytes = model.write();
  REQUIRE(bytes.has_value());
  M2Data<wk> back;
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
  CHECK(magic == md20_magic);
  CHECK(version == 264);
}

TEST_CASE("a synthetic vanilla body embeds its skin profiles", "[formats][m2]")
{
  M2Data<va> model;
  model.name = "vanilla_test";
  M2Sequence<va> stand;
  stand.start_timestamp = 0;
  stand.end_timestamp = 1000;
  model.sequences = {stand};

  M2CompBone<va> bone;
  bone.rotation.interpolation_ranges = {{0, 1}};
  bone.rotation.timestamps = {0, 1000};
  bone.rotation.values = {{}, {}};
  model.bones = {bone};

  model.vertices.resize(4);
  M2SkinProfile<va> profile;
  profile.vertices = {0, 1, 2, 3};
  profile.indices = {0, 1, 2, 2, 1, 3};
  profile.bones = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
  M2SkinSection<va> section;
  section.vertex_count = 4;
  section.index_count = 6;
  profile.submeshes = {section};
  profile.batches = {{}};
  model.skin_profiles = {profile};

  auto bytes = model.write();
  REQUIRE(bytes.has_value());
  M2Data<va> back;
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
