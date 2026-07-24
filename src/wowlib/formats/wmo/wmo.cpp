#include <wowlib/formats/wmo/wmo.hpp>

#include <format>
#include <string>
#include <string_view>

#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::wmo
{
  namespace
  {
    /** Derive a group file path from its root: "world\wmo\thing.wmo" ->
        "world\wmo\thing_007.wmo".
        @param root_path the root file path.
        @param index     the zero-based group index.
        @return the derived group path. */
    std::string group_path(std::string_view root_path, std::size_t index)
    {
      std::string_view stem = root_path;
      if (stem.ends_with(".wmo"))
        stem.remove_suffix(4);
      return std::format("{}_{:03}.wmo", stem, index);
    }

    /** Verify an MVER payload against the v17 the supported clients share.
        @param mver  the version value read from the file.
        @param which which file carried it, for the diagnostic ("root",
                     "group 3", ...).
        @return nothing, or FormatVersionMismatch. */
    Result<void> check_mver(std::uint32_t mver, std::string_view which)
    {
      if (mver != wmo_version_v17)
        return make_error(ErrorCode::FormatVersionMismatch,
                          std::format("{} MVER is {}, expected {}", which, mver, wmo_version_v17));
      return {};
    }
  }

  template <ClientVersion V>
  Result<void> WMO<V>::read(std::span<const std::byte> root_data,
                            std::span<const std::span<const std::byte>> group_datas)
  {
    root = {};
    groups.clear();

    if (auto r = root.read(root_data); !r)
      return std::unexpected{r.error()};
    if (auto r = check_mver(root.mver, "root"); !r)
      return std::unexpected{r.error()};

    groups.reserve(group_datas.size());
    for (std::size_t i = 0; i < group_datas.size(); ++i)
    {
      WMOGroup<V> group;
      if (auto r = group.read(group_datas[i]); !r)
        return make_error(r.error().code,
                          std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
      if (auto r = check_mver(group.mver, std::format("group {}", i)); !r)
        return std::unexpected{r.error()};
      groups.push_back(std::move(group));
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> WMO<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto root_data = fs.read_file(key);
    if (!root_data)
      return std::unexpected{root_data.error()};

    root = {};
    groups.clear();

    if (auto r = root.read(*root_data); !r)
      return std::unexpected{r.error()};
    if (auto r = check_mver(root.mver, "root"); !r)
      return std::unexpected{r.error()};

    const std::size_t n_groups = root.header.n_groups;
    // GFID (group FileDataIDs) is Legion+; pre-Legion roots have no such member
    // (it lives in a version trait that version does not inherit), so they always
    // locate groups by the "{root}_NNN.wmo" path convention.
    bool by_fdid = false;
    if constexpr (requires { root.group_fdids; })
      by_fdid = root.group_fdids.size() >= n_groups;

    std::string root_path;
    if (!by_fdid)
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return make_error(ErrorCode::PathNotResolvable,
                          "group files need the root path (no GFID chunk and the root "
                          "key has no resolvable path)");
      root_path = *resolved.path;
    }

    groups.reserve(n_groups);
    for (std::size_t i = 0; i < n_groups; ++i)
    {
      const FileKey group_key = [&]() -> FileKey {
        if constexpr (requires { root.group_fdids; })
          if (by_fdid)
            return FileKey{FileDataID{root.group_fdids[i]}};
        return FileKey{group_path(root_path, i)};
      }();
      const auto group_data = fs.read_file(group_key);
      if (!group_data)
        return make_error(group_data.error().code,
                          std::format("group {}: {}", i, group_data.error().message),
                          group_data.error().native_error);

      WMOGroup<V> group;
      if (auto r = group.read(*group_data); !r)
        return make_error(r.error().code, std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
      if (auto r = check_mver(group.mver, std::format("group {}", i)); !r)
        return std::unexpected{r.error()};
      groups.push_back(std::move(group));
    }
    return {};
  }

  template <ClientVersion V>
  Result<void> WMO<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "saving a WMO needs a path for the root key");

    const auto root_data = root.write();
    if (!root_data)
      return std::unexpected{root_data.error()};
    if (auto r = fs.add_file(*resolved.path, *root_data); !r)
      return std::unexpected{r.error()};

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      const auto group_data = groups[i].write();
      if (!group_data)
        return std::unexpected{group_data.error()};
      if (auto r = fs.add_file(group_path(*resolved.path, i), *group_data); !r)
        return make_error(r.error().code, std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
    }
    return {};
  }

#define WOWLIB_WMO_INSTANTIATE(Suffix, version_)                                                   \
  template struct root::WMORoot<versions::version_>;                                               \
  template struct group::WMOGroupBody<versions::version_>;                                         \
  template struct group::WMOGroup<versions::version_>;                                             \
  template struct WMO<versions::version_>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_INSTANTIATE)
#undef WOWLIB_WMO_INSTANTIATE
}

namespace wowlib::formats
{
#define WOWLIB_WMO_INSTANTIATE_SERIALIZER(Suffix, version_)                                        \
  template struct ChunkedFile<wmo::root::WMORoot<versions::version_>>;                             \
  template struct ChunkedFile<wmo::group::WMOGroupBody<versions::version_>>;                       \
  template struct ChunkedFile<wmo::group::WMOGroup<versions::version_>>;

  WOWLIB_WMO_FOR_EACH_VERSION(WOWLIB_WMO_INSTANTIATE_SERIALIZER)
#undef WOWLIB_WMO_INSTANTIATE_SERIALIZER
}
