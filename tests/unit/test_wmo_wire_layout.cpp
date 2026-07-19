#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>

#include <wowlib/formats/wmo/wmo.hpp>
#include <wowlib/formats/wmo/wmo_wire.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::wmo;

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
}

TEST_CASE("doodad name/flags packing splits correctly", "[formats][wmo]")
{
  SMODoodadDef def;
  def.name_and_flags = 0x01ABCDEF;  // accept_proj_tex + offset 0xABCDEF
  CHECK(def.name_index() == 0xABCDEF);
  CHECK((def.name_and_flags & doodad_flags::accept_proj_tex) != 0);
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
    group_header.flags = group_flags::exterior;
    body.insert(body.end(), reinterpret_cast<const std::byte*>(&group_header),
                reinterpret_cast<const std::byte*>(&group_header) + sizeof group_header);
    const C3Vector verts[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    put_chunk(body, "MOVT", verts, sizeof verts);
    const std::uint16_t idx[3]{0, 1, 2};
    put_chunk(body, "MOVI", idx, sizeof idx);
    put_chunk(group_data, "MOGP", body.data(), body.size());
  }

  const std::span<const std::byte> group_span{group_data};
  const auto wmo = Wmo<versions::wotlk>::parse(root_data, std::span{&group_span, 1});
  REQUIRE(wmo.has_value());
  CHECK(wmo->root.header.n_groups == 1);
  CHECK(wmo->root.materials.size() == 1);
  CHECK(wmo->root.materials[0].blend_mode == 1);
  REQUIRE(wmo->groups.size() == 1);
  CHECK(wmo->groups[0].body.header.flags == group_flags::exterior);
  CHECK(wmo->groups[0].body.vertices.size() == 3);
  CHECK(wmo->groups[0].body.indices.size() == 3);

  CHECK(*wmo->write_root() == root_data);
  CHECK(*wmo->write_group(0) == group_data);
}
