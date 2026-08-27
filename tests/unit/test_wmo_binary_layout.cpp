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
constexpr bool AllTriviallyCopyable = (std::is_trivially_copyable_v<Ts> && ...);

static_assert(AllTriviallyCopyable<SMOHeader, SMOMaterial, SMOGroupInfo, SMOPortal,
                                     SMOPortalRef, SMOVisibleBlock, SMOLight, SMODoodadSet,
                                     SMODoodadDef, SMOFog, SMOPoly, CAaBspNode,
                                     SMOGroupHeader<versions::Wotlk>,
                                     SMOGroupHeader<versions::Shadowlands>,
                                     SMOBatch<versions::Wotlk>,
                                     SMOBatch<versions::Shadowlands>>);

static_assert(AllTriviallyCopyable<UVAnimation, GroupInfo2, PortalExtra, LightExtension,
                                     NewLight, FogExtra, AmbientVolume, AmbientBoxVolume,
                                     Poly2, RenderBatchOverride, ShadowBatch, PointLight,
                                     LightSet, PointLightAnim>);

// MLIQ grid records are bulk-memcpy'd like the other binary structs; the water
// and magma vertex readings share the same 8-byte layout (bit_cast reinterpret).
static_assert(AllTriviallyCopyable<SMOLVert, SMOLTile, SMOMVert>);
static_assert(sizeof(SMOLVert) == 8 && sizeof(SMOLTile) == 1 && sizeof(SMOMVert) == 8);

TEST_CASE("binary offsets match the wowdev layout", "[formats][wmo]")
{
  STATIC_CHECK(offsetof(SMOHeader, ambientColor) == 0x1C);
  STATIC_CHECK(offsetof(SMOHeader, boundingBox) == 0x24);
  STATIC_CHECK(offsetof(SMOHeader, flags) == 0x3C);

  STATIC_CHECK(offsetof(SMOMaterial, shader) == 0x04);
  STATIC_CHECK(offsetof(SMOMaterial, texture2) == 0x18);
  STATIC_CHECK(offsetof(SMOMaterial, runTimeData) == 0x30);

  STATIC_CHECK(offsetof(SMOLight, rotation) == 0x18);
  STATIC_CHECK(offsetof(SMODoodadDef, orientation) == 0x10);
  STATIC_CHECK(offsetof(SMOFog, fog) == 0x18);
  STATIC_CHECK(offsetof(SMOFog, underWaterFog) == 0x24);

  using OldHeader = SMOGroupHeader<versions::Wotlk>;
  using NewHeader = SMOGroupHeader<versions::Shadowlands>;
  STATIC_CHECK(offsetof(OldHeader, portalStart) == 0x24);
  STATIC_CHECK(offsetof(OldHeader, fogIds) == 0x30);
  STATIC_CHECK(offsetof(OldHeader, flags2) == 0x3C);
  STATIC_CHECK(offsetof(NewHeader, parentOrFirstChildSplitGroupIndex) == 0x40);
  STATIC_CHECK(offsetof(NewHeader, nextSplitChildGroupIndex) == 0x42);

  using OldBatch = SMOBatch<versions::Wotlk>;
  using NewBatch = SMOBatch<versions::Shadowlands>;
  STATIC_CHECK(offsetof(OldBatch, startIndex) == 0x0C);
  STATIC_CHECK(offsetof(NewBatch, materialIdLarge) == 0x0A);
  STATIC_CHECK(offsetof(NewBatch, materialId) == 0x17);

  // the later-expansion records, against the wowdev offset columns
  STATIC_CHECK(offsetof(AmbientVolume, color1) == 0x14);
  STATIC_CHECK(offsetof(AmbientVolume, doodadSetId) == 0x24);
  STATIC_CHECK(offsetof(AmbientBoxVolume, end) == 0x60);
  STATIC_CHECK(offsetof(AmbientBoxVolume, doodadSetId) == 0x74);
  STATIC_CHECK(offsetof(LightExtension, lightIndex) == 0x63);
  STATIC_CHECK(offsetof(NewLight, position) == 0x14);
  STATIC_CHECK(offsetof(NewLight, lightCookieFdid) == 0x64);
  STATIC_CHECK(offsetof(NewLight, falloff) == 0x7C);
  STATIC_CHECK(offsetof(NewLight, scaleHalf) == 0x88);
  STATIC_CHECK(offsetof(PointLightAnim, rotation) == 0x20);
  STATIC_CHECK(offsetof(PointLightAnim, lightTextureFdid) == 0x48);
  STATIC_CHECK(offsetof(ShadowBatch, start) == 0x0C);
  STATIC_CHECK(offsetof(ShadowBatch, materialId) == 0x17);
}

TEST_CASE("doodad name/flags packing splits correctly", "[formats][wmo]")
{
  SMODoodadDef def;
  def.nameAndFlags = 0x01ABCDEF;  // AcceptProjTex + offset 0xABCDEF
  CHECK(def.nameIndex() == 0xABCDEF);
  CHECK(hasFlag(def.nameAndFlags, DoodadFlags::AcceptProjTex));
  CHECK_FALSE(hasFlag(def.nameAndFlags, DoodadFlags::InteriorLighting));
}

namespace
{
  void putChunk(FileBuffer& b, const char (&cc)[5], const void* payload, std::size_t n)
  {
    const std::uint32_t fourcc = fourCc(cc);
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

  FileBuffer rootData;
  putChunk(rootData, "MVER", &mver, sizeof mver);
  SMOHeader header;
  // nGroups is a derived binary field, stamped from the MOGI table on write —
  // the handcrafted file must carry the matching MOGI record for the
  // round-trip to reproduce it
  header.nGroups = 1;
  header.nTextures = 1;
  putChunk(rootData, "MOHD", &header, sizeof header);
  SMOMaterial material;
  material.blendMode = 1;
  putChunk(rootData, "MOMT", &material, sizeof material);
  const SMOGroupInfo group_info{};
  putChunk(rootData, "MOGI", &group_info, sizeof group_info);

  FileBuffer groupData;
  putChunk(groupData, "MVER", &mver, sizeof mver);
  {
    FileBuffer body;
    SMOGroupHeader<versions::Wotlk> groupHeader;
    groupHeader.flags = std::to_underlying(GroupFlags::Exterior);
    body.insert(body.end(), reinterpret_cast<const std::byte*>(&groupHeader),
                reinterpret_cast<const std::byte*>(&groupHeader) + sizeof groupHeader);
    const C3Vector verts[3]{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    putChunk(body, "MOVT", verts, sizeof verts);
    const std::uint16_t idx[3]{0, 1, 2};
    putChunk(body, "MOVI", idx, sizeof idx);
    putChunk(groupData, "MOGP", body.data(), body.size());
  }

  const std::span<const std::byte> groupSpan{groupData};
  WMO<versions::Wotlk> assembled;
  REQUIRE(assembled.read(rootData, std::span{&groupSpan, 1}).has_value());
  CHECK(assembled.root.header.nGroups == 1);
  CHECK(assembled.root.materials.size() == 1);
  CHECK(assembled.root.materials[0].blendMode == 1);
  REQUIRE(assembled.groups.size() == 1);
  CHECK(hasFlag(assembled.groups[0].body.header.flags, GroupFlags::Exterior));
  CHECK(assembled.groups[0].body.vertices.size() == 3);
  CHECK(assembled.groups[0].body.indices.size() == 3);

  CHECK(*assembled.root.write() == rootData);
  CHECK(*assembled.groups[0].write() == groupData);
}

TEST_CASE("MLIQ liquid decodes its header-driven grid and round-trips",
          "[formats][wmo]")
{
  const std::uint32_t mver = 17;

  FileBuffer rootData;
  putChunk(rootData, "MVER", &mver, sizeof mver);
  SMOHeader header;
  header.nGroups = 1;
  putChunk(rootData, "MOHD", &header, sizeof header);

  // A 2x2 vertex grid over a single tile: 30-byte header + 4 verts + 1 tile.
  FileBuffer mliq;
  const std::int32_t vertsDim[2]{2, 2};
  const std::int32_t tilesDim[2]{1, 1};
  const float baseCoords[3]{10.0f, 20.0f, 0.5f};
  const std::uint16_t materialId = 7;
  const auto put = [&mliq](const void* p, std::size_t n) {
    const auto* b = reinterpret_cast<const std::byte*>(p);
    mliq.insert(mliq.end(), b, b + n);
  };
  put(vertsDim, sizeof vertsDim);
  put(tilesDim, sizeof tilesDim);
  put(baseCoords, sizeof baseCoords);
  put(&materialId, sizeof materialId);
  REQUIRE(mliq.size() == 30);   // the unpadded on-disk header
  struct RawVert { std::uint8_t a, b, c, d; float h; };
  const RawVert verts[4]{{1, 2, 3, 0, 1.0f}, {4, 5, 6, 0, 2.0f},
                         {7, 8, 9, 0, 3.0f}, {0, 0, 0, 0, 4.0f}};
  put(verts, sizeof verts);
  const std::uint8_t tile = 0xC1;   // shared | fishable | liquid type 1
  put(&tile, sizeof tile);
  REQUIRE(mliq.size() == 30 + 4 * 8 + 1);

  FileBuffer groupData;
  putChunk(groupData, "MVER", &mver, sizeof mver);
  {
    FileBuffer body;
    SMOGroupHeader<versions::Wotlk> groupHeader;
    groupHeader.flags = std::to_underlying(GroupFlags::HasLiquid);
    body.insert(body.end(), reinterpret_cast<const std::byte*>(&groupHeader),
                reinterpret_cast<const std::byte*>(&groupHeader) + sizeof groupHeader);
    putChunk(body, "MLIQ", mliq.data(), mliq.size());
    putChunk(groupData, "MOGP", body.data(), body.size());
  }

  const std::span<const std::byte> groupSpan{groupData};
  WMO<versions::Wotlk> assembled;
  REQUIRE(assembled.read(rootData, std::span{&groupSpan, 1}).has_value());
  REQUIRE(assembled.groups.size() == 1);

  const auto& liquid = assembled.groups[0].body.liquid;
  CHECK_FALSE(liquid.empty());
  CHECK(liquid.vertsDim.x == 2);
  CHECK(liquid.vertsDim.y == 2);
  CHECK(liquid.tilesDim.x == 1);
  CHECK(liquid.materialId == 7);
  CHECK(liquid.baseCoords.x == 10.0f);
  REQUIRE(liquid.vertices.size() == 4);
  CHECK(liquid.vertices[0].flow1 == 1);
  CHECK(liquid.vertices[3].height == 4.0f);
  REQUIRE(liquid.tiles.size() == 1);
  CHECK(liquid.tiles[0].flags == 0xC1);

  // The magma reading reinterprets the same bytes: vert 0 is {1, 2, 3, 0, ...},
  // so s = int16(1, 2) little-endian = 513, t = int16(3, 0) = 3, height shared.
  const auto magma = liquid.vertices[0].asMagma();
  CHECK(magma.s == std::int16_t(1 | (2 << 8)));
  CHECK(magma.t == 3);
  CHECK(magma.height == 1.0f);
  SMOLVert rebuilt{};
  rebuilt.setMagma(magma);
  CHECK(std::memcmp(&rebuilt, &liquid.vertices[0], sizeof rebuilt) == 0);

  // Byte-perfect round-trip of the whole group, MLIQ included.
  CHECK(*assembled.groups[0].write() == groupData);
}

namespace
{
  /** Whether a group body models MDAL — a named concept so the checks stay
      SFINAE-friendly inside Catch2's macros. */
  template <typename Body>
  concept ModelsMdal = requires(const Body& body) { body.ambientColorOverride; };
}

TEST_CASE("MDAL is a MoP chunk, not a WoD one", "[formats][wmo]")
{
  // wowdev dates MDAL to WoD but flags it "could have been added earlier";
  // a 5.4.8 corpus sweep found 421 of them, while 700 sampled Cata groups and
  // 400 WotLK groups carry none. The version gate follows the corpus.
  STATIC_REQUIRE(ModelsMdal<WMOGroupBody<versions::Mop>>);
  STATIC_REQUIRE(ModelsMdal<WMOGroupBody<versions::Wod>>);
  STATIC_REQUIRE_FALSE(ModelsMdal<WMOGroupBody<versions::Cata>>);
  STATIC_REQUIRE_FALSE(ModelsMdal<WMOGroupBody<versions::Wotlk>>);

  // MoP must also be its own instantiation: were it still folded onto the Cata
  // one, the slot above would be evaluated at Cata and the chunk would vanish.
  STATIC_REQUIRE_FALSE(std::is_same_v<WMOGroupBody<versions::Mop>,
                                      WMOGroupBody<versions::Cata>>);

  // and it parses into the member rather than landing in `unknown`
  FileBuffer body;
  SMOGroupHeader<versions::Mop> groupHeader;
  body.insert(body.end(), reinterpret_cast<const std::byte*>(&groupHeader),
              reinterpret_cast<const std::byte*>(&groupHeader) + sizeof groupHeader);
  const CArgb ambient{.r = 3, .g = 2, .b = 1, .a = 4};
  putChunk(body, "MDAL", &ambient, sizeof ambient);

  WMOGroupBody<versions::Mop> parsed;
  REQUIRE(parsed.read(body).has_value());
  CHECK(parsed.unknown.empty());
  REQUIRE(parsed.ambientColorOverride.size() == 1);
  CHECK(parsed.ambientColorOverride[0].r == 3);
  CHECK(*parsed.write() == body);
}
