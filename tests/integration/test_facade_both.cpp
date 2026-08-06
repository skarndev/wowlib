#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>

#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;
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

  SECTION("3.3.5a")
  {
    auto fs = FileSystem::open({.client_path = tests::mpq_client(),
                                .version = versions::wotlk,
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
    // the supplied CSV is the working database registrations append to, so run
    // against a disposable copy of the community listfile
    const auto listfile_csv = fsys::temp_directory_path() / "wowlib-tests" /
                              "facade-listfile.csv";
    fsys::create_directories(listfile_csv.parent_path());
    fsys::copy_file(tests::require_listfile(), listfile_csv,
                    fsys::copy_options::overwrite_existing);

    auto fs = FileSystem::open({.client_path = tests::casc_client(),
                                .version = versions::shadowlands,
                                .project_directory = fresh_project("facade-casc"),
                                .listfile_csv = listfile_csv});
    REQUIRE(fs.has_value());
    CHECK(fs->kind() == StorageKind::Casc);

    // path reads route through the listfile to a FileDataID
    CHECK(fs->read_file("dbfilesclient/manifestinterfacedata.db2").has_value());
    CHECK(fs->read_file(FileDataID{1375801}).has_value());

    // adding a new file allocates a custom id, persisted in the working listfile
    const auto id = fs->add_file("world/maps/custom/custom.wdt", bytes("MVER"));
    REQUIRE(id.has_value());
    CHECK(id->value >= 1'000'000'000);
    CHECK(fs->read_file("world/maps/custom/custom.wdt").value() == bytes("MVER"));

    // a fresh open of the same project + listfile remembers the id
    auto again = FileSystem::open({.client_path = tests::casc_client(),
                                   .version = versions::shadowlands,
                                   .project_directory =
                                     fsys::temp_directory_path() / "wowlib-tests" /
                                     "facade-casc",
                                   .listfile_csv = listfile_csv});
    REQUIRE(again.has_value());
    CHECK(again->add_file("world/maps/custom/custom.wdt", bytes("MVER v2")).value() ==
          *id);
  }
}