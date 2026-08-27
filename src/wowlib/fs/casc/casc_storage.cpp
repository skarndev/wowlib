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
    LPCSTR cascFdidName(std::uint32_t fdid) {
      return reinterpret_cast<LPCSTR>(static_cast<std::uintptr_t>(fdid));
    }

    struct BuildConfigCandidate {
      std::string key; // hex file name under Data/config/xx/yy/
      std::uint32_t build = 0; // parsed from "build-name = WOW-<build>..."
    };

    // Scans Data/config for "# Build Configuration" files; repacks carry several
    // (old builds, modified variants) and only trying them tells which one the
    // storage actually matches.
    std::vector<BuildConfigCandidate> scanBuildConfigs(const fsys::path& configDir) {
      std::vector<BuildConfigCandidate> found;

      std::error_code ec;
      for (fsys::recursive_directory_iterator it{configDir, ec}, end; !ec && it != end; it.increment(ec)) {
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
    Result<fsys::path> writeShim(const fsys::path& clientData,
                                  const std::string& buildKey,
                                  const std::string& product) {
      const auto shim = fsys::temp_directory_path() / "wowlib-casc-shim" / std::format(
        "{:016x}-{}", fsys::hash_value(clientData), buildKey);

      std::error_code ec;
      fsys::create_directories(shim, ec);
      if (ec)
        return makeError(ErrorCode::IoError,
                          std::format("cannot create CASC shim '{}': {}", shim.string(), ec.message()));

      fsys::remove(shim / "Data", ec);
      fsys::create_directory_symlink(clientData, shim / "Data", ec);
      if (ec)
        return makeError(ErrorCode::IoError,
                          std::format("cannot link '{}' into CASC shim: {}", clientData.string(), ec.message()));

      std::ofstream info{shim / ".build.info", std::ios::trunc};
      // the CDN key is required by the parser but unused for local storages; the
      // build key doubles as a syntactically valid stand-in
      info << "Active!DEC:1|Build Key!HEX:16|CDN Key!HEX:16|Product!STRING:0\n" << std::format(
        "1|{}|{}|{}\n", buildKey, buildKey, product);
      if (!info.flush())
        return makeError(ErrorCode::IoError, std::format("cannot write '{}'", (shim / ".build.info").string()));

      return shim;
    }

    // CASC stores paths with forward slashes; convert from wowlib canonical form.
    std::string cascName(const std::string& canonicalPath) {
      return toNativeRelative(canonicalPath);
    }

    Result<FileBuffer> readOpenFile(HANDLE file, const std::string& what) {
      ULONGLONG size = 0;
      if (!CascGetFileSize64(file, &size)) {
        const auto native = GetCascError();
        CascCloseFile(file);
        return makeError(ErrorCode::BackendError, std::format("CascGetFileSize64 failed for {}", what),
                          static_cast<std::uint32_t>(native));
      }

      FileBuffer buffer(size);
      DWORD read = 0;
      if (!buffer.empty() && !CascReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
        const auto native = GetCascError();
        CascCloseFile(file);
        const bool encrypted = native == ERROR_FILE_ENCRYPTED;
        return makeError(encrypted ? ErrorCode::EncryptedContent : ErrorCode::BackendError,
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
    if (auto opened = storage._openStorage(); !opened) return std::unexpected(opened.error());
    return storage;
  }

  Result<void> CascStorage::_openStorage() {
    std::scoped_lock lock{_mtx};

    const auto localeMask = cascLocaleFlag(_options.locale);

    const auto tryOpen = [&](const fsys::path& localPath) -> HANDLE {
      CASC_OPEN_STORAGE_ARGS args{};
      args.Size = sizeof(args);
      const std::string path = localPath.string();
      args.szLocalPath = path.c_str();
      args.szCodeName = _options.product.c_str();
      args.dwLocaleMask = localeMask;
      HANDLE handle = nullptr;
      return CascOpenStorageEx(nullptr, &args, false, &handle) ? handle : nullptr;
    };

    // (a) a proper install: .build.info at the root (or discoverable from Data/)
    for (const auto& root : {_options.clientRoot, _options.clientRoot / "Data"})
      if (HANDLE handle = tryOpen(root)) {
        _storage = handle;
        return {};
      }
    const std::uint32_t plainNative = GetCascError();

    // (b) a repack without .build.info: synthesize one per build config candidate
    // in a shim directory and try until the storage opens. Only opening tells
    // which config the local data actually matches — repacks carry stale and
    // modified configs side by side.
    auto candidates = scanBuildConfigs(_options.clientRoot / "Data" / "config");
    std::ranges::stable_sort(candidates, [&](const auto& a, const auto& b) {
      if (_options.build) {
        const bool aMatch = a.build == *_options.build;
        const bool bMatch = b.build == *_options.build;
        if (aMatch != bMatch) return aMatch;
      }
      return a.build > b.build;
    });

    for (const auto& candidate : candidates) {
      auto shim = writeShim(_options.clientRoot / "Data", candidate.key, _options.product);
      if (!shim) return std::unexpected(shim.error());

      if (HANDLE handle = tryOpen(*shim)) {
        _storage = handle;
        return {};
      }
    }

    return makeError(ErrorCode::StorageOpenFailed,
                      std::format(
                        "CascOpenStorage failed for '{}' (product '{}'; no .build.info and "
                        "{} build config candidate(s) tried)", _options.clientRoot.string(), _options.product,
                        candidates.size()), plainNative);
  }

  void CascStorage::_close() noexcept {
    std::scoped_lock lock{_mtx};
    if (_storage) {
      CascCloseStorage(_storage);
      _storage = nullptr;
    }
  }

  Result<FileBuffer> CascStorage::readFile(const FileKey& key) {
    std::scoped_lock lock{_mtx};
    if (!_storage) return makeError(ErrorCode::StorageNotOpen, "CASC storage is not open");

    HANDLE file = nullptr;

    if (key.fdid) {
      if (CascOpenFile(_storage, cascFdidName(key.fdid->value), cascLocaleFlag(_options.locale),
                       CASC_OPEN_BY_FILEID, &file))
        return readOpenFile(file, std::format("FileDataID {}", key.fdid->value));

      const std::uint32_t native = GetCascError();
      return makeError(ErrorCode::FileNotFound,
                        std::format("FileDataID {} was not found in the CASC storage", key.fdid->value), native);
    }

    if (key.path) {
      const std::string name = cascName(*key.path);
      if (CascOpenFile(_storage, name.c_str(), cascLocaleFlag(_options.locale), CASC_OPEN_BY_NAME, &file)) return
        readOpenFile(file, std::format("'{}'", name));

      const std::uint32_t native = GetCascError();
      return makeError(ErrorCode::PathNotResolvable,
                        std::format(
                          "'{}' could not be opened by name — this client's "
                          "root manifest likely has no name hashes; resolve " "the path through a listfile", name),
                        native);
    }

    return makeError(ErrorCode::InvalidPath, "empty FileKey");
  }

  Result<void> CascStorage::addEncryptionKey(std::uint64_t keyName, std::span<const std::byte, 16> key) {
    std::scoped_lock lock{_mtx};
    if (!_storage) return makeError(ErrorCode::StorageNotOpen, "CASC storage is not open");
    // CascLib takes the key as a mutable LPBYTE but only reads it.
    auto bytes = std::array<std::uint8_t, 16>{};
    std::memcpy(bytes.data(), key.data(), bytes.size());
    if (!CascAddEncryptionKey(_storage, keyName, bytes.data()))
      return makeError(ErrorCode::BackendError, std::format("CascLib rejected TACT key {:016X}", keyName),
                        GetCascError());
    return {};
  }

  Result<void> CascStorage::importKeys(std::string_view keyList) {
    std::scoped_lock lock{_mtx};
    if (!_storage) return makeError(ErrorCode::StorageNotOpen, "CASC storage is not open");
    const std::string list{keyList};
    if (!CascImportKeysFromString(_storage, list.c_str()))
      return makeError(ErrorCode::BackendError, "CascLib rejected the TACT key list", GetCascError());
    return {};
  }

  Result<std::vector<FileDataID>> CascStorage::enumerateFdids() {
    std::scoped_lock lock{_mtx};
    if (!_storage) return makeError(ErrorCode::StorageNotOpen, "CASC storage is not open");

    CASC_FIND_DATA found{};
    HANDLE find = CascFindFirstFile(_storage, "*", &found, nullptr);
    if (!find)
      return makeError(ErrorCode::BackendError, "CascFindFirstFile failed", GetCascError());

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
      ok = CascOpenFile(_storage, cascFdidName(key.fdid->value), cascLocaleFlag(_options.locale),
                        CASC_OPEN_BY_FILEID, &file);
    else if (key.path)
      ok = CascOpenFile(_storage, cascName(*key.path).c_str(), cascLocaleFlag(_options.locale), CASC_OPEN_BY_NAME,
                        &file);

    if (ok) CascCloseFile(file);
    return ok;
  }
}
