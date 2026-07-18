#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <fstream>

#include <wowlib/fs/project_directory.hpp>

using namespace wowlib;
namespace fsys = std::filesystem;

namespace
{
  FileBuffer bytes(std::string_view text)
  {
    FileBuffer out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
  }

  fsys::path fresh_root(std::string_view name)
  {
    const auto root = fsys::temp_directory_path() / "wowlib-tests" / name;
    fsys::remove_all(root);
    return root;
  }
}

TEST_CASE("write/read round-trip with case-insensitive lookups", "[project-dir]")
{
  auto project = ProjectDirectory::open(fresh_root("proj-roundtrip"));
  REQUIRE(project.has_value());

  const auto content = bytes("MVER content");
  REQUIRE(project->write("World/Maps/MyMap/MyMap.wdt", content).has_value());

  CHECK(project->exists("world/maps/mymap/mymap.wdt"));
  CHECK(project->exists("WORLD\\MAPS\\MYMAP\\MYMAP.WDT"));
  CHECK(project->read("World\\Maps\\MyMap\\MyMap.wdt").value() == content);
  CHECK(project->size() == 1);

  CHECK_FALSE(project->exists("world/maps/other.wdt"));
  CHECK(project->read("world/maps/other.wdt").error().code == ErrorCode::FileNotFound);
}

TEST_CASE("opening an existing tree indexes it, skipping .wowlib metadata",
          "[project-dir]")
{
  const auto root = fresh_root("proj-index");
  fsys::create_directories(root / "Interface/GlueXML");
  std::ofstream{root / "Interface/GlueXML/GlueStrings.lua"} << "-- override";
  fsys::create_directories(root / ".wowlib");
  std::ofstream{root / ".wowlib/custom-listfile.csv"} << "1000000000;foo.blp\n";

  auto project = ProjectDirectory::open(root);
  REQUIRE(project.has_value());
  CHECK(project->size() == 1);
  CHECK(project->exists("interface/gluexml/gluestrings.lua"));
  CHECK_FALSE(project->exists(".wowlib/custom-listfile.csv"));
}

TEST_CASE("rescan picks up files created behind wowlib's back", "[project-dir]")
{
  const auto root = fresh_root("proj-rescan");
  auto project = ProjectDirectory::open(root);
  REQUIRE(project.has_value());
  CHECK(project->size() == 0);

  fsys::create_directories(root / "textures");
  std::ofstream{root / "textures/new.blp"} << "BLP2";
  CHECK_FALSE(project->exists("textures/new.blp"));

  project->rescan();
  CHECK(project->exists("textures/new.blp"));
}