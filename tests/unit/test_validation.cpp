#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

#include <wowlib/formats/adt/adt.hpp>
#include <wowlib/formats/m2/m2.hpp>
#include <wowlib/formats/wmo/wmo.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::wmo;

namespace
{
  /** A minimal internally consistent group: one triangle, one batch. */
  WMOGroup<versions::Wotlk> smallGroup()
  {
    WMOGroup<versions::Wotlk> group;
    auto& body = group.body;
    body.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    body.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    body.indices = {0, 1, 2};
    body.polys = {{.flags = 0x20, .materialId = 0}};
    auto& batch = body.batches.emplace_back();
    batch.startIndex = 0;
    batch.count = 3;
    batch.minIndex = 0;
    batch.maxIndex = 2;
    batch.materialId = 0;
    body.header.extBatchCount = 1;
    return group;
  }

  /** Whether any finding of @a report anchors at @a path and mentions
      @a needle in its message. */
  bool reports(const ValidationReport& report, std::string_view path, std::string_view needle,
               ValidationSeverity severity = ValidationSeverity::Error)
  {
    return std::ranges::any_of(report.issues(), [&](const ValidationIssue& issue) {
      return issue.severity == severity && issue.path == path
             && issue.message.find(needle) != std::string::npos;
    });
  }
}

TEST_CASE("validate: a consistent fresh group reports nothing", "[formats][wmo][validation]")
{
  const auto group = smallGroup();
  const ValidationReport report = group.validate();
  CHECK(report.ok());
  CHECK(report.size() == 0);
  CHECK(group.ensureValid().has_value());
}

TEST_CASE("validate: companion-count contracts (count_matches)", "[formats][wmo][validation]")
{
  auto group = smallGroup();
  group.body.normals.pop_back();
  const auto report = group.validate();
  CHECK(!report.ok());
  CHECK(reports(report, "body.normals", "count 2 != vertices count 3"));

  // a disengaged (empty) companion is fine - the chunk is simply not written
  group.body.normals.clear();
  CHECK(group.validate().ok());

  // Repeated<> slots check per filled slot, scaled members via their factor
  auto texcoords = smallGroup();
  texcoords.body.texcoords.push()->resize(2);
  CHECK(reports(texcoords.validate(), "body.texcoords[0]", "count 2 != vertices count 3"));

  auto polys = smallGroup();
  polys.body.polys.emplace_back();
  CHECK(reports(polys.validate(), "body.polys", "count 2 x 3 != the 3 active triangle indices"));
}

TEST_CASE("validate: index contracts (indexes, count_multiple_of)",
          "[formats][wmo][validation]")
{
  auto group = smallGroup();
  group.body.indices = {0, 1, 7};
  const auto report = group.validate();
  CHECK(reports(report, "body.indices[2]", "index 7 out of range: vertices holds 3"));

  group.body.indices = {0, 1, 2, 0};
  CHECK(reports(group.validate(), "body.indices", "count 4 is not a multiple of 3"));
}

TEST_CASE("validate: expected_value on the format version", "[formats][wmo][validation]")
{
  auto group = smallGroup();
  group.mver = 16;
  CHECK(reports(group.validate(), "mver", "value 16 != required 17"));
}

TEST_CASE("validate: group hook - batches, BSP, flags", "[formats][wmo][validation]")
{
  auto batches = smallGroup();
  batches.body.batches[0].count = 6;
  CHECK(reports(batches.validate(), "body.batches[0]", "overruns the 3 indices"));

  auto partition = smallGroup();
  partition.body.header.extBatchCount = 2;
  CHECK(reports(partition.validate(), "body.header", "batch counts 0+0+2 != 1 batches"));

  auto bsp = smallGroup();
  bsp.body.bspFaceIndices = {0};
  auto& node = bsp.body.bspNodes.emplace_back();
  node.flags = 0x4;
  node.faceStart = 0;
  node.nFaces = 2;
  CHECK(reports(bsp.validate(), "body.bsp_nodes[0]", "overruns the 1 face indices"));

  auto flags = smallGroup();
  flags.body.header.flags = std::to_underlying(group::chunks::GroupFlags::HasVertexColors);
  CHECK(reports(flags.validate(), "body.vertexColors", "flag"));

  auto surplus = smallGroup();
  surplus.body.vertexColors.push()->resize(3);
  CHECK(reports(surplus.validate(), "body.vertexColors",
                "has_vertex_colors group flag is clear", ValidationSeverity::Warning));
  CHECK(surplus.validate().ok());  // warnings do not fail a report
}

TEST_CASE("validate: root referential contracts", "[formats][wmo][validation]")
{
  WMORoot<versions::Wotlk> root;

  auto& ref = root.portalRefs.emplace_back();
  ref.portalIndex = 1;
  ref.groupIndex = 0;
  auto& set = root.doodadSets.emplace_back();
  set.startIndex = 0;
  set.count = 2;
  auto& info = root.groupInfos.emplace_back();
  info.nameOffset = 12;

  const auto report = root.validate();
  CHECK(reports(report, "portal_refs[0]", "portal_index 1 out of range: 0 portals"));
  CHECK(reports(report, "doodad_sets[0]", "overruns the 0 doodad placements"));
  CHECK(reports(report, "groupInfos[0]", "name offset 12 out of range: 0 blob bytes"));

  // doodad name resolution: no engaged source, then a resolvable MODN offset
  auto& def = root.doodadDefs.emplace_back();
  def.nameAndFlags = 5;
  CHECK(reports(root.validate(), "doodadDefs[0]", "does not resolve"));
  root.doodadNames.add("world/doodad.m2");  // offsets 0..15 valid
  CHECK(!reports(root.validate(), "doodadDefs[0]", "does not resolve"));
}

TEST_CASE("validate: assembly cross-entity contracts", "[formats][wmo][validation]")
{
  WMO<versions::Wotlk> wmo;
  wmo.root.materials.emplace_back();
  wmo.groups.push_back(smallGroup());
  auto& info = wmo.root.groupInfos.emplace_back();
  info.nameOffset = -1;
  CHECK(wmo.ensureValid().has_value());

  SECTION("the MOGI table must match the groups held")
  {
    wmo.root.groupInfos.clear();
    CHECK(reports(wmo.validate(), "root.groupInfos", "count 0 != 1 group files held"));
  }

  SECTION("group references into the root's arrays (indexes_in_root)")
  {
    wmo.groups[0].body.doodadRefs = {0};
    CHECK(reports(wmo.validate(), "groups[0].body.doodadRefs[0]",
                  "index 0 out of range: doodadDefs holds 0"));
  }

  SECTION("render materials must resolve in MOMT; 0xFF stays a collision face")
  {
    wmo.groups[0].body.polys[0].materialId = 5;
    CHECK(reports(wmo.validate(), "groups[0].body.polys[0]",
                  "material 5 out of range: 1 materials"));
    wmo.groups[0].body.polys[0].materialId = 0xFF;
    CHECK(wmo.validate().ok());

    wmo.groups[0].body.batches[0].materialId = 3;
    CHECK(reports(wmo.validate(), "groups[0].body.batches[0]",
                  "material 3 out of range: 1 materials"));
  }

  SECTION("the group header's portal slice references MOPR")
  {
    wmo.groups[0].body.header.portalCount = 1;
    CHECK(reports(wmo.validate(), "groups[0].body.header",
                  "overruns the 0 portal references"));
  }

  SECTION("ensure_valid folds the findings into one InvalidEntityState error")
  {
    wmo.root.groupInfos.clear();
    const auto result = wmo.ensureValid();
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidEntityState);
    CHECK(result.error().message.find("root.groupInfos") != std::string::npos);
  }
}

// --- M2: the offset-entity side of the walker --------------------------------

namespace
{
  namespace m2n = wowlib::formats::m2;

  /** A minimal internally consistent model body: two bones in hierarchy order,
      one sequence, one keyframed translation track. */
  m2n::M2Root<versions::Wotlk> smallRoot()
  {
    m2n::M2Root<versions::Wotlk> root;
    root.sequences.emplace_back();
    auto& parent = root.bones.emplace_back();
    parent.parentBone = -1;
    auto& child = root.bones.emplace_back();
    child.parentBone = 0;
    // WotLK+ tracks hold one timestamp array and one value array per sequence
    child.translation.timestamps.push_back({0, 100});
    child.translation.values.push_back({{0, 0, 0}, {1, 0, 0}});
    return root;
  }
}

TEST_CASE("validate: M2 track arrays pair per sequence", "[formats][m2][validation]")
{
  auto root = smallRoot();
  CHECK(root.validate().ok());

  SECTION("the outer per-sequence counts must match")
  {
    root.bones[1].translation.values.emplace_back();
    CHECK(reports(root.validate(), "bones[1].translation.values",
                  "count 2 != timestamps count 1"));
  }

  SECTION("and so must each sequence's inner arrays")
  {
    root.bones[1].translation.values[0].pop_back();
    CHECK(reports(root.validate(), "bones[1].translation.values[0]",
                  "count 1 != timestamps[0] count 2"));
  }
}

TEST_CASE("validate: M2 lookup tables tolerate the none sentinel",
          "[formats][m2][validation]")
{
  auto root = smallRoot();

  SECTION("-1 and 0xFFFF reference nothing and are legal")
  {
    root.keyBoneLookup = {-1, 1};
    root.textureLookupTable = {0xFFFF};
    CHECK(root.validate().ok());
  }

  SECTION("a real out-of-range index is still an error")
  {
    root.replacableTextureLookup = {7};
    CHECK(reports(root.validate(), "replacableTextureLookup[0]",
                  "index 7 out of range: textures holds 0"));
  }
}

TEST_CASE("validate: M2 bone hierarchy and alias chains", "[formats][m2][validation]")
{
  SECTION("a parent must exist and precede its child")
  {
    auto root = smallRoot();
    root.bones[0].parentBone = 1;
    CHECK(reports(root.validate(), "bones[0]", "does not precede the child"));

    root.bones[0].parentBone = 9;
    CHECK(reports(root.validate(), "bones[0]", "out of range: 2 bones"));
  }

  SECTION("an alias chain must reach a sequence that owns data")
  {
    auto root = smallRoot();
    root.sequences[0].flags = 0x40;  // alias
    root.sequences[0].aliasNext = 0;
    CHECK(reports(root.validate(), "sequences[0]", "points at itself"));

    root.sequences[0].aliasNext = 5;
    CHECK(reports(root.validate(), "sequences[0]", "alias_next 5 out of range"));
  }
}

TEST_CASE("validate: M2 collision hull", "[formats][m2][validation]")
{
  auto root = smallRoot();
  root.collisionVertices.assign(3, {});
  root.collisionTriangles = {0, 1, 2};
  root.collisionNormals.assign(1, {});
  CHECK(root.validate().ok());

  SECTION("indices are whole triangles into the hull vertices")
  {
    root.collisionTriangles = {0, 1, 9};
    const auto report = root.validate();
    CHECK(reports(report, "collisionTriangles[2]",
                  "index 9 out of range: collisionVertices holds 3"));

    root.collisionTriangles = {0, 1};
    CHECK(reports(root.validate(), "collisionTriangles", "not a multiple of 3"));
  }

  SECTION("one normal per face")
  {
    root.collisionNormals.emplace_back();
    CHECK(reports(root.validate(), "collisionNormals",
                  "count 2 x 3 != collisionTriangles count 3"));
  }
}

// --- ADT: the bespoke-entity side of the walker ------------------------------

namespace
{
  namespace adtn = wowlib::formats::adt;

  /** A minimal internally consistent WotLK tile: a full chunk grid, one
      texture, and one chunk carrying a single layer. */
  adtn::ADT<versions::Wotlk> smallTile()
  {
    adtn::ADT<versions::Wotlk> tile;
    tile.textures.add("tileset/generic/black.blp");
    tile.chunks.assign(adtn::ChunksPerTile, adtn::MapChunk<versions::Wotlk>{});
    auto& chunk = tile.chunks[0];
    chunk.heights.assign(145, 0.0f);
    chunk.normals.assign(145, {});
    chunk.layers.emplace_back();
    chunk.alphaMaps.emplace_back();  // layer 0 is opaque: no alpha surface
    return tile;
  }
}

TEST_CASE("validate: ADT chunk grids are fixed size", "[formats][adt][validation]")
{
  auto tile = smallTile();
  CHECK(tile.validate().ok());

  SECTION("heights and normals hold the full 145-vertex grid")
  {
    tile.chunks[0].heights.pop_back();
    CHECK(reports(tile.validate(), "chunks[0].heights", "count 144 != required 145"));
  }

  SECTION("a decoded shadow map covers all 4096 texels")
  {
    tile.chunks[0].shadowMap.assign(512, 0);  // the on-disk 1-bit size, not decoded
    CHECK(reports(tile.validate(), "chunks[0].shadowMap", "count 512 != required 4096"));
  }

  SECTION("the tile is a full 16x16 chunk grid")
  {
    tile.chunks.pop_back();
    CHECK(reports(tile.validate(), "chunks", "count 255 != the 256 chunks"));
  }
}

TEST_CASE("validate: ADT alpha maps align with layers", "[formats][adt][validation]")
{
  auto tile = smallTile();

  SECTION("one alpha map per layer")
  {
    tile.chunks[0].layers.emplace_back();
    CHECK(reports(tile.validate(), "chunks[0].alphaMaps", "count 1 != layers count 2"));
  }

  SECTION("an engaged map is the decoded 64x64 surface")
  {
    tile.chunks[0].layers.emplace_back();
    tile.chunks[0].alphaMaps.emplace_back().assign(2048, 0);
    CHECK(reports(tile.validate(), "chunks[0].alphaMaps[1]", "holds 2048 texels, not 4096"));
  }
}

TEST_CASE("validate: ADT tile-wide references", "[formats][adt][validation]")
{
  auto tile = smallTile();

  SECTION("layer texture ids resolve in the tile's texture table")
  {
    tile.chunks[0].layers[0].textureId = 3;
    CHECK(reports(tile.validate(), "chunks[0].layers[0]",
                  "texture_id 3 out of range: 1 textures"));
  }

  SECTION("chunk references land in the tile's placement tables")
  {
    tile.chunks[0].doodadRefs = {0};
    CHECK(reports(tile.validate(), "chunks[0].doodadRefs[0]",
                  "index 0 out of range: doodadPlacements holds 0"));
  }

  SECTION("a placement resolves its model through the name-offset table")
  {
    auto& placement = tile.doodadPlacements.emplace_back();
    placement.nameId = 2;
    CHECK(reports(tile.validate(), "doodadPlacements[0]",
                  "name_id 2 out of range: modelNameOffsets holds 0"));

    // ... unless the flag makes it a FileDataID the client loads directly
    placement.flags = std::to_underlying(wowlib::formats::common::DoodadDefFlags::EntryIsFdid);
    CHECK(tile.validate().ok());
  }
}
