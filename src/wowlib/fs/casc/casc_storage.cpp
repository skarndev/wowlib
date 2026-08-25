#include <wowlib/fs/casc/casc_storage.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <format>
#include <fstream>
#include <vector>

#include <CascLib.h>

#include <wowlib/core/path.hpp>

namespace wowlib::fs {
  namespace {
    namespace fsys = std::filesystem;

    /** CascOpenFile's by-FileDataID calling convention smuggles the id through
        the name pointer — CascLib's CASC_FILE_DATA_ID macro, respelled without
        its C-style casts. */
    LPCSTR casc_fdid_name(std::uint32_t fdid) {
      return reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(fdid));
    }

    struct BuildConfigCandidate {
      std::string key; // hex file name under Data/config/xx/yy/
      std::uint32_t build = 0; // parsed from "build-name = WOW-<build>..."
    };

    // Scans Data/config for "# Build Configuration" files; repacks carry several
    // (old builds, modified variants) and only trying them tells which one the
    // storage actually matches.
    std::vector<BuildConfigCandidate> scan_build_configs(const fsys::path& config_dir) {
      std::vector<BuildConfigCandidate> found;

      std::error_code ec;
      for (fsys::recursive_directory_iterator it{config_dir, ec}, end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;

        std::ifstream file{it->path()};
        std::string line;
        if (!std::getline(file, line) || !line.starts_with("# Build Configuration")) continue;

        BuildConfigCandidate candidate{.key = it->path().filename().string()};
        while (std::getline(file, line))
          if (const auto pos = line.find("build-name"); pos != std::string::npos) {
            if (const auto wow = line.find("WOW-"); wow != std::string::npos)
              std::from_chars(line.data() + wow + 4, line.data() + line.size(), candidate.build);
            break;
          }
        found.push_back(std::move(candidate));
      }
      return found;
    }

    // Synthesizes the minimal .build.info CascLib needs (Active, Build Key, CDN
    // Key, Product) in a shim directory whose Data symlink points back into the
    // client, leaving the client untouched.
    Result<fsys::path> write_shim(const fsys::path& client_data,
                                  const std::string& build_key,
                                  const std::string& product) {
      const auto shim = fsys::temp_directory_path() / "wowlib-casc-shim" / std::format(
        "{:016x}-{}", fsys::hash_value(client_data), build_key);

      std::error_code ec;
      fsys::create_directories(shim, ec);
      if (ec)
        return make_error(ErrorCode::IoError,
                          std::format("cannot create CASC shim '{}': {}", shim.string(), ec.message()));

      fsys::remove(shim / "Data", ec);
      fsys::create_directory_symlink(client_data, shim / "Data", ec);
      if (ec)
        return make_error(ErrorCode::IoError,
                          std::format("cannot link '{}' into CASC shim: {}", client_data.string(), ec.message()));

      std::ofstream info{shim / ".build.info", std::ios::trunc};
      // the CDN key is required by the parser but unused for local storages; the
      // build key doubles as a syntactically valid stand-in
      info << "Active!DEC:1|Build Key!HEX:16|CDN Key!HEX:16|Product!STRING:0\n" << std::format(
        "1|{}|{}|{}\n", build_key, build_key, product);
      if (!info.flush())
        return make_error(ErrorCode::IoError, std::format("cannot write '{}'", (shim / ".build.info").string()));

      return shim;
    }

    // CASC stores paths with forward slashes; convert from wowlib canonical form.
    std::string casc_name(const std::string& canonical_path) {
      return to_native_relative(canonical_path);
    }

    Result<FileBuffer> read_open_file(HANDLE file, const std::string& what) {
      ULONGLONG size = 0;
      if (!CascGetFileSize64(file, &size)) {
        const auto native = GetCascError();
        CascCloseFile(file);
        return make_error(ErrorCode::BackendError, std::format("CascGetFileSize64 failed for {}", what),
                          static_cast<std::uint32_t>(native));
      }

      FileBuffer buffer(size);
      DWORD read = 0;
      if (!buffer.empty() && !CascReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
        const auto native = GetCascError();
        CascCloseFile(file);
        const bool encrypted = native == ERROR_FILE_ENCRYPTED;
        return make_error(encrypted ? ErrorCode::EncryptedContent : ErrorCode::BackendError,
                          encrypted
                            ? std::format("{} is behind an unknown TACT key", what)
                            : std::format("CascReadFile failed for {}", what), static_cast<std::uint32_t>(native));
      }

      CascCloseFile(file);
      return buffer;
    }
  }

  Result<CascStorage> CascStorage::open(Options options) {
    CascStorage storage{std::move(options)};
    if (auto opened = storage.open_storage(); !opened) return std::unexpected(opened.error());
    return storage;
  }

  Result<void> CascStorage::open_storage() {
    std::scoped_lock lock{_mtx};

    const auto locale_mask = casc_locale_flag(_options.locale);

    const auto try_open = [&](const fsys::path& local_path) -> HANDLE {
      CASC_OPEN_STORAGE_ARGS args{};
      args.Size = sizeof(args);
      const std::string path = local_path.string();
      args.szLocalPath = path.c_str();
      args.szCodeName = _options.product.c_str();
      args.dwLocaleMask = locale_mask;
      HANDLE handle = nullptr;
      return CascOpenStorageEx(nullptr, &args, false, &handle) ? handle : nullptr;
    };

    // (a) a proper install: .build.info at the root (or discoverable from Data/)
    for (const auto& root : {_options.client_root, _options.client_root / "Data"})
      if (HANDLE handle = try_open(root)) {
        _storage = handle;
        return {};
      }
    const std::uint32_t plain_native = GetCascError();

    // (b) a repack without .build.info: synthesize one per build config candidate
    // in a shim directory and try until the storage opens. Only opening tells
    // which config the local data actually matches — repacks carry stale and
    // modified configs side by side.
    auto candidates = scan_build_configs(_options.client_root / "Data" / "config");
    std::ranges::stable_sort(candidates, [&](const auto& a, const auto& b) {
      if (_options.build) {
        const bool a_match = a.build == *_options.build;
        const bool b_match = b.build == *_options.build;
        if (a_match != b_match) return a_match;
      }
      return a.build > b.build;
    });

    for (const auto& candidate : candidates) {
      auto shim = write_shim(_options.client_root / "Data", candidate.key, _options.product);
      if (!shim) return std::unexpected(shim.error());

      if (HANDLE handle = try_open(*shim)) {
        _storage = handle;
        return {};
      }
    }

    return make_error(ErrorCode::StorageOpenFailed,
                      std::format(
                        "CascOpenStorage failed for '{}' (product '{}'; no .build.info and "
                        "{} build config candidate(s) tried)", _options.client_root.string(), _options.product,
                        candidates.size()), plain_native);
  }

  void CascStorage::close() noexcept {
    std::scoped_lock lock{_mtx};
    if (_storage) {
      CascCloseStorage(_storage);
      _storage = nullptr;
    }
  }

  Result<FileBuffer> CascStorage::read_file(const FileKey& key) {
    std::scoped_lock lock{_mtx};
    if (!_storage) return make_error(ErrorCode::StorageNotOpen, "CASC storage is not open");

    HANDLE file = nullptr;

    if (key.fdid) {
      if (CascOpenFile(_storage, casc_fdid_name(key.fdid->value), casc_locale_flag(_options.locale),
                       CASC_OPEN_BY_FILEID, &file))
        return read_open_file(file, std::format("FileDataID {}", key.fdid->value));

      const std::uint32_t native = GetCascError();
      return make_error(ErrorCode::FileNotFound,
                        std::format("FileDataID {} was not found in the CASC storage", key.fdid->value), native);
    }

    if (key.path) {
      const std::string name = casc_name(*key.path);
      if (CascOpenFile(_storage, name.c_str(), casc_locale_flag(_options.locale), CASC_OPEN_BY_NAME, &file)) return
        read_open_file(file, std::format("'{}'", name));

      const std::uint32_t native = GetCascError();
      return make_error(ErrorCode::PathNotResolvable,
                        std::format(
                          "'{}' could not be opened by name — this client's "
                          "root manifest likely has no name hashes; resolve " "the path through a listfile", name),
                        native);
    }

    return make_error(ErrorCode::InvalidPath, "empty FileKey");
  }

  Result<void> CascStorage::add_encryption_key(std::uint64_t key_name, std::span<const std::byte, 16> key) {
    std::scoped_lock lock{_mtx};
    if (!_storage) return make_error(ErrorCode::StorageNotOpen, "CASC storage is not open");
    // CascLib takes the key as a mutable LPBYTE but only reads it.
    auto bytes = std::array<std::uint8_t, 16>{};
    std::memcpy(bytes.data(), key.data(), bytes.size());
    if (!CascAddEncryptionKey(_storage, key_name, bytes.data()))
      return make_error(ErrorCode::BackendError, std::format("CascLib rejected TACT key {:016X}", key_name),
                        GetCascError());
    return {};
  }

  Result<void> CascStorage::import_keys(std::string_view key_list) {
    std::scoped_lock lock{_mtx};
    if (!_storage) return make_error(ErrorCode::StorageNotOpen, "CASC storage is not open");
    const std::string list{key_list};
    if (!CascImportKeysFromString(_storage, list.c_str()))
      return make_error(ErrorCode::BackendError, "CascLib rejected the TACT key list", GetCascError());
    return {};
  }

  Result<std::vector<FileDataID>> CascStorage::enumerate_fdids() {
    std::scoped_lock lock{_mtx};
    if (!_storage) return make_error(ErrorCode::StorageNotOpen, "CASC storage is not open");

    CASC_FIND_DATA found{};
    HANDLE find = CascFindFirstFile(_storage, "*", &found, nullptr);
    if (!find)
      return make_error(ErrorCode::BackendError, "CascFindFirstFile failed", GetCascError());

    std::vector<FileDataID> fdids;
    do {
      // Only content that is actually present locally and addressable by id;
      // WoD-era roots are name-hash keyed and report CASC_INVALID_ID.
      if (found.bFileAvailable && found.dwFileDataId != CASC_INVALID_ID) fdids.
        push_back(FileDataID{found.dwFileDataId});
    }
    while (CascFindNextFile(find, &found));
    CascFindClose(find);

    std::ranges::sort(fdids);
    const auto duplicates = std::ranges::unique(fdids);
    fdids.erase(duplicates.begin(), duplicates.end());
    return fdids;
  }

  bool CascStorage::exists(const FileKey& key) {
    std::scoped_lock lock{_mtx};
    if (!_storage) return false;

    HANDLE file = nullptr;
    bool ok = false;

    if (key.fdid)
      ok = CascOpenFile(_storage, casc_fdid_name(key.fdid->value), casc_locale_flag(_options.locale),
                        CASC_OPEN_BY_FILEID, &file);
    else if (key.path)
      ok = CascOpenFile(_storage, casc_name(*key.path).c_str(), casc_locale_flag(_options.locale), CASC_OPEN_BY_NAME,
                        &file);

    if (ok) CascCloseFile(file);
    return ok;
  }
}
