#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <filesystem>
#include <map>
#include <string>

#include <wowlib/fs/casc/casc_storage.hpp>
#include <wowlib/fs/client_filesystem.hpp>
#include <wowlib/fs/csv_listfile.hpp>
#include <wowlib/fs/mpq/mpq_storage.hpp>
#include <wowlib/fs/storage_backend.hpp>

#include "unit_env.hpp"

using namespace wowlib;
using namespace wowlib::fs;
namespace fsys = std::filesystem;

namespace
{
  FileBuffer bytes(std::string_view text)
  {
    FileBuffer out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
  }

  // In-memory backend: path-addressed like MPQ. Doubles as the compile-time proof
  // of the StorageBackend concept for a type outside the real backends.
  struct FakeStorage
  {
    std::map<std::string, FileBuffer> files;

    Result<FileBuffer> readFile(const FileKey& key)
    {
      if (!key.path)
        return makeError(ErrorCode::FdidNotResolvable, "fake is path-addressed");
      const auto it = files.find(*key.path);
      if (it == files.end())
        return makeError(ErrorCode::FileNotFound, *key.path);
      return it->second;
    }

    bool exists(const FileKey& key)
    {
      return key.path && files.contains(*key.path);
    }

    static constexpr StorageKind kind() { return StorageKind::Mpq; }
  };

  static_assert(StorageBackend<FakeStorage>);
  static_assert(StorageBackend<MpqStorage>);
  static_assert(StorageBackend<CascStorage>);

  // Same fake, presenting as CASC so addFile exercises the listfile path.
  struct FakeCascStorage : FakeStorage
  {
    static constexpr StorageKind kind() { return StorageKind::Casc; }
  };

  fsys::path freshRoot(std::string_view name)
  {
    const auto root = fsys::temp_directory_path() / "wowlib-tests" / name;
    fsys::remove_all(root);
    return root;
  }
}

TEST_CASE("the project overlay wins over the backend", "[client-fs]")
{
  FakeStorage storage;
  storage.files["interface\\gluexml\\gluestrings.lua"] = bytes("from archive");

  auto project = ProjectDirectory::open(freshRoot("cfs-overlay"));
  REQUIRE(project.has_value());
  REQUIRE(project->write("Interface/GlueXML/GlueStrings.lua", bytes("from project"))
            .has_value());

  ClientFileSystem<FakeStorage> fs{std::move(storage), {}, std::move(*project)};

  CHECK(fs.readFile(FileKey{"interface/gluexml/gluestrings.lua"}).value() ==
        bytes("from project"));
  CHECK(fs.exists(FileKey{"interface/gluexml/gluestrings.lua"}));

  // backend-only files still resolve
  fs.backend().files["dbfilesclient\\map.dbc"] = bytes("archive only");
  CHECK(fs.readFile(FileKey{"DBFilesClient/Map.dbc"}).value() ==
        bytes("archive only"));
}

TEST_CASE("resolve fills the missing identity half from the listfile", "[client-fs]")
{
  auto listfile = CsvListfile::load(tests::dataRoot() /
                                    "sample-listfile.csv");
  REQUIRE(listfile.has_value());

  FakeCascStorage storage;
  storage.files["dbfilesclient\\map.db2"] = bytes("WDC3...");

  ClientFileSystem<FakeCascStorage, CsvListfile> fs{std::move(storage),
                                                    std::move(*listfile)};

  const auto byId = fs.resolve(FileKey{FileDataID{1349477}});
  CHECK(byId.path == "dbfilesclient\\map.db2");

  const auto byPath = fs.resolve(FileKey{"DBFilesClient/Map.db2"});
  CHECK(byPath.fdid == FileDataID{1349477});

  // an id-only request reads through the resolved path on a path-addressed backend
  CHECK(fs.readFile(FileKey{FileDataID{1349477}}).value() == bytes("WDC3..."));
}

TEST_CASE("add_file allocates a custom id on CASC and persists it in the working "
          "listfile",
          "[client-fs]")
{
  // the loaded CSV is the working database, so operate on a disposable copy
  const auto csv = fsys::temp_directory_path() / "wowlib-tests" / "cfs-addfile.csv";
  fsys::create_directories(csv.parent_path());
  fsys::copy_file(tests::dataRoot() / "sample-listfile.csv", csv,
                  fsys::copy_options::overwrite_existing);

  auto listfile = CsvListfile::load(csv,
                                    {.customFdidStart = FileDataID{1'000'000'000}});
  REQUIRE(listfile.has_value());

  auto project = ProjectDirectory::open(freshRoot("cfs-addfile"));
  REQUIRE(project.has_value());

  ClientFileSystem<FakeCascStorage, CsvListfile> fs{{}, std::move(*listfile),
                                                    std::move(*project)};

  const auto id = fs.addFile("world/maps/custom/custom.wdt", bytes("MVER"));
  REQUIRE(id.has_value());
  CHECK(*id == FileDataID{1'000'000'000});
  CHECK(fs.readFile(FileKey{*id}).value() == bytes("MVER"));

  // the registration reached the working file
  auto reloaded = CsvListfile::load(csv);
  REQUIRE(reloaded.has_value());
  CHECK(reloaded->pathToFdid("world/maps/custom/custom.wdt") == *id);

  // overwriting keeps the id stable
  CHECK(fs.addFile("world/maps/custom/custom.wdt", bytes("MVER v2")).value() == *id);

  // MPQ-era composition: files land in the overlay, id space does not exist
  auto mpqProject = ProjectDirectory::open(freshRoot("cfs-addfile-mpq"));
  REQUIRE(mpqProject.has_value());
  ClientFileSystem<FakeStorage> mpqFs{{}, {}, std::move(*mpqProject)};
  CHECK(mpqFs.addFile("interface/custom.lua", bytes("-- lua")).value() ==
        FileDataID{0});

  // without a project directory there is nowhere to add
  ClientFileSystem<FakeStorage> bare{{}};
  CHECK(bare.addFile("foo.blp", bytes("x")).error().code == ErrorCode::NotSupported);
}