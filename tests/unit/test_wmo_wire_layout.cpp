#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>

#include <wowlib/formats/wmo/wmo.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::wmo;
using namespace wowlib::formats::wmo::root::chunks;
using namespace wowlib::formats::wmo::group;
using namespace wowlib::formats::wmo::group::chunks;

// Sizes are asserted in the header itself; here we lock triviality (bulk
// memcpy reads depend on it) and a few load-bearing offsets.

template <typename... Ts>
constexpr bool all_trivially_copyable = (std::is_trivially_copyable_v<Ts> && ...);

static_assert(all_trivially_copyable<SMOHeader, SMOMaterial, SMOGroupInfo, SMOPortal,
                                     SMOPortalRef, SMOVisibleBlock, SMOLight, SMODoodadSet,
                                     SMODoodadDef, SMOFog, SMOPoly, CAaBspNode,
                                     SMOGroupHeader<versions::wotlk>,
                                     SMOGroupHeader<versions::shadowlands>,
                                     SMOBatch<versions::wotlk>,
                                     SMOBatch<versions::shadowlands>>);

static_assert(all_trivially_copyable<UVAnimation, GroupInfo2, PortalExtra, LightExtension,
                                     NewLight, FogExtra, AmbientVolume, AmbientBoxVolume,
                                     Poly2, RenderBatchOverride, ShadowBatch, PointLight,
                                     LightSet, PointLightAnim>);

// MLIQ grid records are bulk-memcpy'd like the other wire structs; the water
// and magma vertex readings share the same 8-byte layout (bit_cast reinterpret).
static_assert(all_trivially_copyable<SMOLVert, SMOLTile, SMOMVert>);
static_assert(sizeof(SMOLVert) == 8 && sizeof(SMOLTile) == 1 && sizeof(SMOMVert) == 8);

TEST_CASE("wire offsets match the wowdev layout", "[formats][wmo]")
{
  STATIC_CHECK(offsetof(SMOHeader, ambient_color) == 0x1C);
  STATIC_CHECK(offsetof(SMOHeader, bounding_box) == 0x24);
  STATIC_CHECK(offsetof(SMOHeader, flags) == 0x3C);

  STATIC_CHECK(offsetof(SMOMaterial, shader) == 0x04);
  STATIC_CHECK(offsetof(SMOMaterial, texture_2) == 0x18);
  STATIC_CHECK(offsetof(SMOMaterial, run_time_data) == 0x30);

  STATIC_CHECK(offsetof(SMOLight, rotation) == 0x18);
  STATIC_CHECK(offsetof(SMODoodadDef, orientation) == 0x10);
  STATIC_CHECK(offsetof(SMOFog, fog) == 0x18);
  STATIC_CHECK(offsetof(SMOFog, under_water_fog) == 0x24);

  using OldHeader = SMOGroupHeader<versions::wotlk>;
  using NewHeader = SMOGroupHeader<versions::shadowlands>;
  STATIC_CHECK(offsetof(OldHeader, portal_start) == 0x24);
  STATIC_CHECK(offsetof(OldHeader, fog_ids) == 0x30);
  STATIC_CHECK(offsetof(OldHeader, flags2) == 0x3C);
  STATIC_CHECK(offsetof(NewHeader, parent_or_first_child_split_group_index) == 0x40);
  STATIC_CHECK(offsetof(NewHeader, next_split_child_group_index) == 0x42);

  using OldBatch = SMOBatch<versions::wotlk>;
  using NewBatch = SMOBatch<versions::shadowlands>;
  STATIC_CHECK(offsetof(OldBatch, start_index) == 0x0C);
  STATIC_CHECK(offsetof(NewBatch, material_id_large) == 0x0A);
  STATIC_CHECK(offsetof(NewBatch, material_id) == 0x17);

  // the later-expansion records, against the wowdev offset columns
  STATIC_CHECK(offsetof(AmbientVolume, color_1) == 0x14);
  STATIC_CHECK(offsetof(AmbientVolume, doodad_set_id) == 0x24);
  STATIC_CHECK(offsetof(AmbientBoxVolume, end) == 0x60);
  STATIC_CHECK(offsetof(AmbientBoxVolume, doodad_set_id) == 0x74);
  STATIC_CHECK(offsetof(LightExtension, light_index) == 0x63);
  STATIC_CHECK(offsetof(NewLight, position) == 0x14);
  STATIC_CHECK(offsetof(NewLight, light_cookie_fdid) == 0x64);
  STATIC_CHECK(offsetof(NewLight, falloff) == 0x7C);
  STATIC_CHECK(offsetof(NewLight, scale_half) == 0x88);
  STATIC_CHECK(offsetof(PointLightAnim, rotation) == 0x20);
  STATIC_CHECK(offsetof(PointLightAnim, light_texture_fdid) == 0x48);
  STATIC_CHECK(offsetof(ShadowBatch, start) == 0x0C);
  STATIC_CHECK(offsetof(ShadowBatch, material_id) == 0x17);
}

TEST_CASE("doodad name/flags packing splits correctly", "[formats][wmo]")
{
  SMODoodadDef def;
  def.name_and_flags = 0x01ABCDEF;  // accept_proj_tex + offset 0xABCDEF
  CHECK(def.name_index() == 0xABCDEF);
  CHECK(has_flag(def.name_and_flags, DoodadFlags::accept_proj_tex));
  CHECK_FALSE(has_flag(def.name_and_flags, DoodadFlags::interior_lighting));
}

namespace
{
  void put_chunk(FileBuffer& b, const char (&cc)[5], const void* payload, std::size_t n)
  {
    const std::uint32_t fourcc = four_cc(cc);
    const auto size = static_cast<std::uint32_t>(n);
    const auto* p = reinterpret_cast<const std::byte*>(payload);
    b.insert(b.end(), reinterpret_cast<const std::byte*>(&fourcc),
             reinterpret_cast<const std::byte*>(&fourcc) + 4);
    b.insert(b.end(), reinterpret_cast<const std::byte*>(&size),
             reinterpret_cast<const std::byte*>(&size) + 4);
    b.insert(b.end(), p, p + n);
  }
}

TEST_CASE("a handcrafted minimal WMO assembles and round-trips", "[formats][wmo]")
{
  const std::uint32_t mver = 17;

  FileBuffer root_data;
  put_chunk(root_data, "MVER", &mver, sizeof mver);
  SMOHeader header;
  header.n_groups = 1;
  header.n_textures = 1;
  put_chunk(root_data, "MOHD", &header, sizeof header);
  SMOMaterial material;
  material.blend_mode = 1;
  put_chunk(root_data, "MOMT", &material, sizeof material);

  FileBuffer group_data;
  put_chunk(group_data, "MVER", &mver, sizeof mver);
  {
    FileBuffer body;
    SMOGroupHeader<versions::wotlk> group_header;
    group_header.flags = std::to_underlying(GroupFlags::exterior);
    body.insert(body.end(), reinterpret_cast<const std::byte*>(&group_header),
                reinterpret_cast<const std::byte*>(&group_header) + sizeof group_header);
    const C3Vector verts[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    put_chunk(body, "MOVT", verts, sizeof verts);
    const std::uint16_t idx[3]{0, 1, 2};
    put_chunk(body, "MOVI", idx, sizeof idx);
    put_chunk(group_data, "MOGP", body.data(), body.size());
  }

  const std::span<const std::byte> group_span{group_data};
  WMO<versions::wotlk> assembled;
  REQUIRE(assembled.read(root_data, std::span{&group_span, 1}).has_value());
  CHECK(assembled.root.header.n_groups == 1);
  CHECK(assembled.root.materials.size() == 1);
  CHECK(assembled.root.materials[0].blend_mode == 1);
  REQUIRE(assembled.groups.size() == 1);
  CHECK(has_flag(assembled.groups[0].body.header.flags, GroupFlags::exterior));
  CHECK(assembled.groups[0].body.vertices.size() == 3);
  CHECK(assembled.groups[0].body.indices.size() == 3);

  CHECK(*assembled.root.write() == root_data);
  CHECK(*assembled.groups[0].write() == group_data);
}

TEST_CASE("MLIQ liquid decodes its header-driven grid and round-trips",
          "[formats][wmo]")
{
  const std::uint32_t mver = 17;

  FileBuffer root_data;
  put_chunk(root_data, "MVER", &mver, sizeof mver);
  SMOHeader header;
  header.n_groups = 1;
  put_chunk(root_data, "MOHD", &header, sizeof header);

  // A 2x2 vertex grid over a single tile: 30-byte header + 4 verts + 1 tile.
  FileBuffer mliq;
  const std::int32_t verts_dim[2]{2, 2};
  const std::int32_t tiles_dim[2]{1, 1};
  const float base_coords[3]{10.0f, 20.0f, 0.5f};
  const std::uint16_t material_id = 7;
  const auto put = [&mliq](const void* p, std::size_t n) {
    const auto* b = reinterpret_cast<const std::byte*>(p);
    mliq.insert(mliq.end(), b, b + n);
  };
  put(verts_dim, sizeof verts_dim);
  put(tiles_dim, sizeof tiles_dim);
  put(base_coords, sizeof base_coords);
  put(&material_id, sizeof material_id);
  REQUIRE(mliq.size() == 30);   // the unpadded on-disk header
  struct RawVert { std::uint8_t a, b, c, d; float h; };
  const RawVert verts[4]{{1, 2, 3, 0, 1.0f}, {4, 5, 6, 0, 2.0f},
                         {7, 8, 9, 0, 3.0f}, {0, 0, 0, 0, 4.0f}};
  put(verts, sizeof verts);
  const std::uint8_t tile = 0xC1;   // shared | fishable | liquid type 1
  put(&tile, sizeof tile);
  REQUIRE(mliq.size() == 30 + 4 * 8 + 1);

  FileBuffer group_data;
  put_chunk(group_data, "MVER", &mver, sizeof mver);
  {
    FileBuffer body;
    SMOGroupHeader<versions::wotlk> group_header;
    group_header.flags = std::to_underlying(GroupFlags::has_liquid);
    body.insert(body.end(), reinterpret_cast<const std::byte*>(&group_header),
                reinterpret_cast<const std::byte*>(&group_header) + sizeof group_header);
    put_chunk(body, "MLIQ", mliq.data(), mliq.size());
    put_chunk(group_data, "MOGP", body.data(), body.size());
  }

  const std::span<const std::byte> group_span{group_data};
  WMO<versions::wotlk> assembled;
  REQUIRE(assembled.read(root_data, std::span{&group_span, 1}).has_value());
  REQUIRE(assembled.groups.size() == 1);

  const auto& liquid = assembled.groups[0].body.liquid;
  CHECK_FALSE(liquid.empty());
  CHECK(liquid.verts_dim.x == 2);
  CHECK(liquid.verts_dim.y == 2);
  CHECK(liquid.tiles_dim.x == 1);
  CHECK(liquid.material_id == 7);
  CHECK(liquid.base_coords.x == 10.0f);
  REQUIRE(liquid.vertices.size() == 4);
  CHECK(liquid.vertices[0].flow1 == 1);
  CHECK(liquid.vertices[3].height == 4.0f);
  REQUIRE(liquid.tiles.size() == 1);
  CHECK(liquid.tiles[0].flags == 0xC1);

  // The magma reading reinterprets the same bytes: vert 0 is {1, 2, 3, 0, ...},
  // so s = int16(1, 2) little-endian = 513, t = int16(3, 0) = 3, height shared.
  const auto magma = liquid.vertices[0].as_magma();
  CHECK(magma.s == std::int16_t(1 | (2 << 8)));
  CHECK(magma.t == 3);
  CHECK(magma.height == 1.0f);
  SMOLVert rebuilt{};
  rebuilt.set_magma(magma);
  CHECK(std::memcmp(&rebuilt, &liquid.vertices[0], sizeof rebuilt) == 0);

  // Byte-perfect round-trip of the whole group, MLIQ included.
  CHECK(*assembled.groups[0].write() == group_data);
}
