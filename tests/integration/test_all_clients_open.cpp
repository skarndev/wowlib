#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <filesystem>
#include <format>

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
  constexpr std::array Installs{
    Install{"1.12.1", versions::Vanilla},
    Install{"1.12.2", versions::Vanilla},
    Install{"2.4.3", versions::Tbc},
    Install{"3.3.5a", versions::Wotlk},
    Install{"4.3.4", versions::Cata},
    Install{"5.4.8", versions::Mop},
    Install{"6.2.3", versions::Wod},
    Install{"6.2.4", versions::Wod},
    Install{"7.3.5", versions::Legion},
    Install{"8.3.7", versions::Bfa},
    Install{"9.2.7", versions::Shadowlands},
    Install{"10.2.7", versions::Dragonflight},
    Install{"11.2.7", versions::Tww},
  };

  fsys::path freshProject(std::string_view name)
  {
    const auto root = fsys::temp_directory_path() / "wowlib-tests" / "sweep" / name;
    fsys::remove_all(root);
    return root;
  }
}

TEST_CASE("every installed client opens and serves a probe read",
          "[integration][sweep]")
{
  const auto clients = tests::requireClientsDir();
  const auto listfile = tests::envPath("WOWLIB_TEST_LISTFILE");

  for (const auto& install : Installs)
  {
    const auto root = clients / install.dir;
    if (!fsys::is_directory(root))
      continue;

    DYNAMIC_SECTION(install.dir)
    {
      const bool mpq = install.version.storageKind() == StorageKind::Mpq;
      const auto data = tests::dataDir(root);

      // CASC repacks carry locale-tagged content too; fall back to enUS when
      // the data dir exposes no locale subdirectory.
      const auto locale = tests::findLocale(data).value_or(Locale::enUS);

      // The listfile is what makes a modern CASC storage path-addressable; the
      // suite runs against a disposable copy since registrations append to it.
      std::optional<fsys::path> workingListfile{};
      if (!mpq && listfile)
      {
        workingListfile = fsys::temp_directory_path() / "wowlib-tests" /
                           "sweep" / (std::string{install.dir} + "-listfile.csv");
        fsys::create_directories(workingListfile->parent_path());
        fsys::copy_file(*listfile, *workingListfile,
                        fsys::copy_options::overwrite_existing);
      }

      auto fs = FileSystem::open({.clientPath = root,
                                  .version = install.version,
                                  .locale = locale,
                                  .projectDirectory = freshProject(install.dir),
                                  .listfileCsv = workingListfile});
      // UNSCOPED_INFO: a scoped INFO inside the if would die before the
      // assertion below ever fired.
      if (!fs.has_value())
        UNSCOPED_INFO(std::format("open failed: {} (native {:#x})",
                                  fs.error().message, fs.error().nativeError));
      REQUIRE(fs.has_value());
      CHECK(fs->kind() == install.version.storageKind());

      if (mpq)
      {
        // Present in every MPQ-era client, with a stable magic to assert on.
        const auto dbc = fs->readFile("DBFilesClient/Map.dbc");
        if (!dbc.has_value())
          UNSCOPED_INFO("probe read failed: " + dbc.error().message);
        REQUIRE(dbc.has_value());
        REQUIRE(dbc->size() >= 4);
        CHECK(std::memcmp(dbc->data(), "WDBC", 4) == 0);
      }
      else if (install.version >= versions::Legion && workingListfile)
      {
        // FileDataIDs only entered the CASC root manifest with Legion; the WoD
        // root is name-hash keyed, so 6.x stays an open-only smoke check.
        CHECK(fs->readFile("dbfilesclient/map.db2").has_value());
      }
    }
  }
}
