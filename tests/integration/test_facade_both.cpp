#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>

#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
namespace fsys = std::filesystem;

namespace
{
  fsys::path fresh_project(std::string_view name)
  {
    const auto root = fsys::temp_directory_path() / "wowlib-tests" / name;
    fsys::remove_all(root);
    return root;
  }

  FileBuffer bytes(std::string_view text)
  {
    FileBuffer out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
  }
}

TEST_CASE("both clients work through the runtime facade alone",
          "[integration][facade]")
{
  const auto clients = tests::require_clients_dir();

  SECTION("3.3.5a")
  {
    auto fs = FileSystem::open({.client_path = clients / tests::mpq_client_name,
                                .version = ClientVersion::wotlk(),
                                .project_directory = fresh_project("facade-mpq")});
    REQUIRE(fs.has_value());
    CHECK(fs->kind() == StorageKind::Mpq);
    CHECK(fs->mpq() != nullptr);
    CHECK(fs->casc() == nullptr);

    REQUIRE(fs->exists("DBFilesClient/Map.dbc"));
    CHECK(fs->read_file("DBFilesClient/Map.dbc").has_value());

    // the overlay overrides the archive
    const auto payload = bytes("-- override");
    REQUIRE(fs->add_file("Interface/GlueXML/GlueStrings.lua", payload).has_value());
    CHECK(fs->read_file("Interface/GlueXML/GlueStrings.lua").value() == payload);
  }

  SECTION("9.2.7")
  {
    const auto listfile_csv = tests::require_listfile();

    auto fs = FileSystem::open({.client_path = clients / tests::casc_client_name,
                                .version = ClientVersion::shadowlands(),
                                .project_directory = fresh_project("facade-casc"),
                                .listfile_csv = listfile_csv});
    REQUIRE(fs.has_value());
    CHECK(fs->kind() == StorageKind::Casc);

    // path reads route through the listfile to a FileDataID
    CHECK(fs->read_file("dbfilesclient/manifestinterfacedata.db2").has_value());
    CHECK(fs->read_file(FileDataID{1375801}).has_value());

    // adding a new file allocates a custom id and persists the sidecar
    const auto id = fs->add_file("world/maps/custom/custom.wdt", bytes("MVER"));
    REQUIRE(id.has_value());
    CHECK(id->value >= 1'000'000'000);
    CHECK(fs->read_file("world/maps/custom/custom.wdt").value() == bytes("MVER"));

    // a fresh open of the same project remembers the id (sidecar round-trip)
    auto again = FileSystem::open({.client_path = clients / tests::casc_client_name,
                                   .version = ClientVersion::shadowlands(),
                                   .project_directory =
                                     fsys::temp_directory_path() / "wowlib-tests" /
                                     "facade-casc",
                                   .listfile_csv = listfile_csv});
    REQUIRE(again.has_value());
    CHECK(again->add_file("world/maps/custom/custom.wdt", bytes("MVER v2")).value() ==
          *id);
  }
}