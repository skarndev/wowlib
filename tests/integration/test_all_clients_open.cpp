#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <filesystem>

#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;
namespace fsys = std::filesystem;

/** @file
    The whole-fleet smoke sweep: every canonical version directory present
    under WOWLIB_TEST_CLIENTS_DIR must open through the runtime facade and
    (where the storage supports it) serve a probe read. The deep per-format
    suites pin specific clients; this one is what "the CI server got a new
    client" buys immediately. */

namespace
{
  struct Install
  {
    const char* dir;         ///< canonical directory name under the clients dir
    ClientVersion version;   ///< the version the directory name promises
  };

  // Every release wowlib targets, by canonical install-directory name.
  constexpr std::array installs{
    Install{"1.12.1", versions::vanilla},
    Install{"1.12.2", versions::vanilla},
    Install{"2.4.3", versions::tbc},
    Install{"3.3.5a", versions::wotlk},
    Install{"4.3.4", versions::cata},
    Install{"5.4.8", versions::mop},
    Install{"6.2.3", versions::wod},
    Install{"6.2.4", versions::wod},
    Install{"7.3.5", versions::legion},
    Install{"8.3.7", versions::bfa},
    Install{"9.2.7", versions::shadowlands},
    Install{"10.2.7", versions::dragonflight},
    Install{"11.2.7", versions::tww},
  };

  fsys::path fresh_project(std::string_view name)
  {
    const auto root = fsys::temp_directory_path() / "wowlib-tests" / "sweep" / name;
    fsys::remove_all(root);
    return root;
  }
}

TEST_CASE("every installed client opens and serves a probe read",
          "[integration][sweep]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile = tests::env_path("WOWLIB_TEST_LISTFILE");

  for (const auto& install : installs)
  {
    const auto root = clients / install.dir;
    if (!fsys::is_directory(root))
      continue;

    DYNAMIC_SECTION(install.dir)
    {
      const bool mpq = install.version.storage_kind() == StorageKind::Mpq;
      const auto data = tests::data_dir(root);

      // CASC repacks carry locale-tagged content too; fall back to enUS when
      // the data dir exposes no locale subdirectory.
      const auto locale = tests::find_locale(data).value_or(Locale::enUS);

      // The listfile is what makes a modern CASC storage path-addressable; the
      // suite runs against a disposable copy since registrations append to it.
      std::optional<fsys::path> working_listfile{};
      if (!mpq && listfile)
      {
        working_listfile = fsys::temp_directory_path() / "wowlib-tests" /
                           "sweep" / (std::string{install.dir} + "-listfile.csv");
        fsys::create_directories(working_listfile->parent_path());
        fsys::copy_file(*listfile, *working_listfile,
                        fsys::copy_options::overwrite_existing);
      }

      auto fs = FileSystem::open({.client_path = root,
                                  .version = install.version,
                                  .locale = locale,
                                  .project_directory = fresh_project(install.dir),
                                  .listfile_csv = working_listfile});
      REQUIRE(fs.has_value());
      CHECK(fs->kind() == install.version.storage_kind());

      if (mpq)
      {
        // Present in every MPQ-era client, with a stable magic to assert on.
        const auto dbc = fs->read_file("DBFilesClient/Map.dbc");
        REQUIRE(dbc.has_value());
        REQUIRE(dbc->size() >= 4);
        CHECK(std::memcmp(dbc->data(), "WDBC", 4) == 0);
      }
      else if (install.version >= versions::legion && working_listfile)
      {
        // FileDataIDs only entered the CASC root manifest with Legion; the WoD
        // root is name-hash keyed, so 6.x stays an open-only smoke check.
        CHECK(fs->read_file("dbfilesclient/map.db2").has_value());
      }
    }
  }
}
