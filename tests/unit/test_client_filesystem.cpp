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

  // In-memory backend: path-addressed like MPQ. Doubles as the compile-time proof
  // of the StorageBackend concept for a type outside the real backends.
  struct FakeStorage
  {
    std::map<std::string, FileBuffer> files;

    Result<void> open() { return {}; }
    void close() noexcept {}
    bool is_open() const { return true; }

    Result<FileBuffer> read_file(const FileKey& key)
    {
      if (!key.path)
        return make_error(ErrorCode::FdidNotResolvable, "fake is path-addressed");
      const auto it = files.find(*key.path);
      if (it == files.end())
        return make_error(ErrorCode::FileNotFound, *key.path);
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

  // Same fake, presenting as CASC so add_file exercises the listfile path.
  struct FakeCascStorage : FakeStorage
  {
    static constexpr StorageKind kind() { return StorageKind::Casc; }
  };

  fsys::path fresh_root(std::string_view name)
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

  auto project = ProjectDirectory::open(fresh_root("cfs-overlay"));
  REQUIRE(project.has_value());
  REQUIRE(project->write("Interface/GlueXML/GlueStrings.lua", bytes("from project"))
            .has_value());

  ClientFileSystem<FakeStorage> fs{std::move(storage), {}, std::move(*project)};

  CHECK(fs.read_file(FileKey::by_path("interface/gluexml/gluestrings.lua")).value() ==
        bytes("from project"));
  CHECK(fs.exists(FileKey::by_path("interface/gluexml/gluestrings.lua")));

  // backend-only files still resolve
  fs.backend().files["dbfilesclient\\map.dbc"] = bytes("archive only");
  CHECK(fs.read_file(FileKey::by_path("DBFilesClient/Map.dbc")).value() ==
        bytes("archive only"));
}

TEST_CASE("resolve fills the missing identity half from the listfile", "[client-fs]")
{
  auto listfile = CsvListfile::load(fsys::path{WOWLIB_TEST_DATA_DIR} /
                                    "sample-listfile.csv");
  REQUIRE(listfile.has_value());

  FakeCascStorage storage;
  storage.files["dbfilesclient\\map.db2"] = bytes("WDC3...");

  ClientFileSystem<FakeCascStorage, CsvListfile> fs{std::move(storage),
                                                    std::move(*listfile)};

  const auto by_id = fs.resolve(FileKey::by_fdid(FileDataID{1349477}));
  CHECK(by_id.path == "dbfilesclient\\map.db2");

  const auto by_path = fs.resolve(FileKey::by_path("DBFilesClient/Map.db2"));
  CHECK(by_path.fdid == FileDataID{1349477});

  // an id-only request reads through the resolved path on a path-addressed backend
  CHECK(fs.read_file(FileKey::by_fdid(FileDataID{1349477})).value() == bytes("WDC3..."));
}

TEST_CASE("add_file allocates a custom id on CASC and persists the sidecar",
          "[client-fs]")
{
  auto listfile = CsvListfile::load(fsys::path{WOWLIB_TEST_DATA_DIR} /
                                      "sample-listfile.csv",
                                    {.custom_fdid_start = FileDataID{1'000'000'000}});
  REQUIRE(listfile.has_value());

  auto project = ProjectDirectory::open(fresh_root("cfs-addfile"));
  REQUIRE(project.has_value());
  const auto sidecar = project->root() / ".wowlib/custom-listfile.csv";

  ClientFileSystem<FakeCascStorage, CsvListfile> fs{{}, std::move(*listfile),
                                                    std::move(*project)};
  fs.set_sidecar(sidecar);

  const auto id = fs.add_file("world/maps/custom/custom.wdt", bytes("MVER"));
  REQUIRE(id.has_value());
  CHECK(*id == FileDataID{1'000'000'000});
  CHECK(fs.read_file(FileKey::by_fdid(*id)).value() == bytes("MVER"));
  CHECK(fsys::is_regular_file(sidecar));

  // overwriting keeps the id stable
  CHECK(fs.add_file("world/maps/custom/custom.wdt", bytes("MVER v2")).value() == *id);

  // MPQ-era composition: files land in the overlay, id space does not exist
  auto mpq_project = ProjectDirectory::open(fresh_root("cfs-addfile-mpq"));
  REQUIRE(mpq_project.has_value());
  ClientFileSystem<FakeStorage> mpq_fs{{}, {}, std::move(*mpq_project)};
  CHECK(mpq_fs.add_file("interface/custom.lua", bytes("-- lua")).value() ==
        FileDataID{0});

  // without a project directory there is nowhere to add
  ClientFileSystem<FakeStorage> bare{{}};
  CHECK(bare.add_file("foo.blp", bytes("x")).error().code == ErrorCode::NotSupported);
}