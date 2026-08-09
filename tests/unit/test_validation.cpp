#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>

#include <wowlib/formats/wmo/wmo.hpp>

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::wmo;

namespace
{
  /** A minimal internally consistent group: one triangle, one batch. */
  WMOGroup<versions::wotlk> small_group()
  {
    WMOGroup<versions::wotlk> group;
    auto& body = group.body;
    body.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    body.normals = {{0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    body.indices = {0, 1, 2};
    body.polys = {{.flags = 0x20, .material_id = 0}};
    auto& batch = body.batches.emplace_back();
    batch.start_index = 0;
    batch.count = 3;
    batch.min_index = 0;
    batch.max_index = 2;
    batch.material_id = 0;
    body.header.ext_batch_count = 1;
    return group;
  }

  /** Whether any finding of @a report anchors at @a path and mentions
      @a needle in its message. */
  bool reports(const ValidationReport& report, std::string_view path, std::string_view needle,
               ValidationSeverity severity = ValidationSeverity::error)
  {
    return std::ranges::any_of(report.issues(), [&](const ValidationIssue& issue) {
      return issue.severity == severity && issue.path == path
             && issue.message.find(needle) != std::string::npos;
    });
  }
}

TEST_CASE("validate: a consistent fresh group reports nothing", "[formats][wmo][validation]")
{
  const auto group = small_group();
  const ValidationReport report = group.validate();
  CHECK(report.ok());
  CHECK(report.size() == 0);
  CHECK(group.ensure_valid().has_value());
}

TEST_CASE("validate: companion-count contracts (count_matches)", "[formats][wmo][validation]")
{
  auto group = small_group();
  group.body.normals.pop_back();
  const auto report = group.validate();
  CHECK(!report.ok());
  CHECK(reports(report, "body.normals", "count 2 != vertices count 3"));

  // a disengaged (empty) companion is fine - the chunk is simply not written
  group.body.normals.clear();
  CHECK(group.validate().ok());

  // Repeated<> slots check per filled slot, scaled members via their factor
  auto texcoords = small_group();
  texcoords.body.texcoords.push()->resize(2);
  CHECK(reports(texcoords.validate(), "body.texcoords[0]", "count 2 != vertices count 3"));

  auto polys = small_group();
  polys.body.polys.emplace_back();
  CHECK(reports(polys.validate(), "body.polys", "count 2 x 3 != indices count 3"));
}

TEST_CASE("validate: index contracts (indexes, count_multiple_of)",
          "[formats][wmo][validation]")
{
  auto group = small_group();
  group.body.indices = {0, 1, 7};
  const auto report = group.validate();
  CHECK(reports(report, "body.indices[2]", "index 7 out of range: vertices holds 3"));

  group.body.indices = {0, 1, 2, 0};
  CHECK(reports(group.validate(), "body.indices", "count 4 is not a multiple of 3"));
}

TEST_CASE("validate: expected_value on the format version", "[formats][wmo][validation]")
{
  auto group = small_group();
  group.mver = 16;
  CHECK(reports(group.validate(), "mver", "value 16 != required 17"));
}

TEST_CASE("validate: group hook - batches, BSP, flags", "[formats][wmo][validation]")
{
  auto batches = small_group();
  batches.body.batches[0].count = 6;
  CHECK(reports(batches.validate(), "body.batches[0]", "overruns the 3 indices"));

  auto partition = small_group();
  partition.body.header.ext_batch_count = 2;
  CHECK(reports(partition.validate(), "body.header", "batch counts 0+0+2 != 1 batches"));

  auto bsp = small_group();
  bsp.body.bsp_face_indices = {0};
  auto& node = bsp.body.bsp_nodes.emplace_back();
  node.flags = 0x4;
  node.face_start = 0;
  node.n_faces = 2;
  CHECK(reports(bsp.validate(), "body.bsp_nodes[0]", "overruns the 1 face indices"));

  auto flags = small_group();
  flags.body.header.flags = std::to_underlying(group::chunks::GroupFlags::has_vertex_colors);
  CHECK(reports(flags.validate(), "body.vertex_colors", "flag"));

  auto surplus = small_group();
  surplus.body.vertex_colors.push()->resize(3);
  CHECK(reports(surplus.validate(), "body.vertex_colors",
                "has_vertex_colors group flag is clear", ValidationSeverity::warning));
  CHECK(surplus.validate().ok());  // warnings do not fail a report
}

TEST_CASE("validate: root referential contracts", "[formats][wmo][validation]")
{
  WMORoot<versions::wotlk> root;

  auto& ref = root.portal_refs.emplace_back();
  ref.portal_index = 1;
  ref.group_index = 0;
  auto& set = root.doodad_sets.emplace_back();
  set.start_index = 0;
  set.count = 2;
  auto& info = root.group_infos.emplace_back();
  info.name_offset = 12;

  const auto report = root.validate();
  CHECK(reports(report, "portal_refs[0]", "portal_index 1 out of range: 0 portals"));
  CHECK(reports(report, "doodad_sets[0]", "overruns the 0 doodad placements"));
  CHECK(reports(report, "group_infos[0]", "name offset 12 out of range: 0 blob bytes"));

  // doodad name resolution: no engaged source, then a resolvable MODN offset
  auto& def = root.doodad_defs.emplace_back();
  def.name_and_flags = 5;
  CHECK(reports(root.validate(), "doodad_defs[0]", "does not resolve"));
  root.doodad_names.add("world/doodad.m2");  // offsets 0..15 valid
  CHECK(!reports(root.validate(), "doodad_defs[0]", "does not resolve"));
}

TEST_CASE("validate: assembly cross-entity contracts", "[formats][wmo][validation]")
{
  WMO<versions::wotlk> wmo;
  wmo.root.materials.emplace_back();
  wmo.groups.push_back(small_group());
  auto& info = wmo.root.group_infos.emplace_back();
  info.name_offset = -1;
  CHECK(wmo.ensure_valid().has_value());

  SECTION("the MOGI table must match the groups held")
  {
    wmo.root.group_infos.clear();
    CHECK(reports(wmo.validate(), "root.group_infos", "count 0 != 1 group files held"));
  }

  SECTION("group references into the root's arrays (indexes_in_root)")
  {
    wmo.groups[0].body.doodad_refs = {0};
    CHECK(reports(wmo.validate(), "groups[0].body.doodad_refs[0]",
                  "index 0 out of range: doodad_defs holds 0"));
  }

  SECTION("render materials must resolve in MOMT; 0xFF stays a collision face")
  {
    wmo.groups[0].body.polys[0].material_id = 5;
    CHECK(reports(wmo.validate(), "groups[0].body.polys[0]",
                  "material 5 out of range: 1 materials"));
    wmo.groups[0].body.polys[0].material_id = 0xFF;
    CHECK(wmo.validate().ok());

    wmo.groups[0].body.batches[0].material_id = 3;
    CHECK(reports(wmo.validate(), "groups[0].body.batches[0]",
                  "material 3 out of range: 1 materials"));
  }

  SECTION("the group header's portal slice references MOPR")
  {
    wmo.groups[0].body.header.portal_count = 1;
    CHECK(reports(wmo.validate(), "groups[0].body.header",
                  "overruns the 0 portal references"));
  }

  SECTION("ensure_valid folds the findings into one InvalidEntityState error")
  {
    wmo.root.group_infos.clear();
    const auto result = wmo.ensure_valid();
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::InvalidEntityState);
    CHECK(result.error().message.find("root.group_infos") != std::string::npos);
  }
}
