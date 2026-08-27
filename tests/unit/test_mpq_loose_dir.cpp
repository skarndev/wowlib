#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <wowlib/fs/mpq/mpq_storage.hpp>

using namespace wowlib;
using namespace wowlib::fs;
namespace fsys = std::filesystem;

namespace
{
  fsys::path freshRoot(std::string_view name)
  {
    const auto root = fsys::temp_directory_path() / "wowlib-tests" / name;
    fsys::remove_all(root);
    return root;
  }

  void writeFile(const fsys::path& path, std::string_view content)
  {
    fsys::create_directories(path.parent_path());
    std::ofstream out{path, std::ios::binary};
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
  }

  std::string text(const FileBuffer& buffer)
  {
    return std::string{reinterpret_cast<const char*>(buffer.data()), buffer.size()};
  }
}

// A Data/ tree built entirely from loose-file folders (no real MPQ), so the whole
// chain resolves through the directory backend. Base and locale markers are empty
// folders; the locale is passed explicitly since detection wants a real
// locale-*.MPQ file.
TEST_CASE("loose directories serve files with override and case-insensitivity",
          "[mpq][loose]")
{
  const auto data = freshRoot("mpq-loose");
  fsys::create_directories(data / "common.MPQ");
  fsys::create_directories(data / "enUS" / "locale-enUS.MPQ");

  // Same path in two base patches: the higher-sorted (patch-4) overrides patch-2.
  writeFile(data / "patch-2.MPQ" / "World" / "Test.txt", "from patch-2");
  writeFile(data / "patch-4.MPQ" / "World" / "Test.txt", "from patch-4");
  // A locale patch sorts last of all, so it wins outright.
  writeFile(data / "enUS" / "patch-enUS.MPQ" / "World" / "Test.txt", "from locale");
  // A file only present in the lowest patch still resolves.
  writeFile(data / "patch-2.MPQ" / "DBFilesClient" / "Map.dbc", "WDBC");

  auto opened = MpqStorage::open(
    {.dataDir = data, .version = versions::Wotlk, .locale = Locale::enUS});
  REQUIRE(opened.has_value());
  MpqStorage& storage = *opened;

  SECTION("last-loaded loose member wins, resolved case-insensitively")
  {
    const auto winner = storage.readFile(FileKey{"World/Test.txt"});
    REQUIRE(winner.has_value());
    CHECK(text(*winner) == "from locale");
  }

  SECTION("a file only in the lowest patch still resolves")
  {
    const auto dbc = storage.readFile(FileKey{"DBFilesClient\\Map.dbc"});
    REQUIRE(dbc.has_value());
    CHECK(text(*dbc) == "WDBC");
  }

  SECTION("exists honors case-insensitive canonical lookup")
  {
    CHECK(storage.exists(FileKey{"WORLD/TEST.TXT"}));
    CHECK_FALSE(storage.exists(FileKey{"no/such/file.blp"}));
  }

  SECTION("a miss is FileNotFound")
  {
    const auto missing = storage.readFile(FileKey{"no/such/file.blp"});
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().code == ErrorCode::FileNotFound);
  }
}