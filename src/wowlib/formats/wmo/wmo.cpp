#include <wowlib/formats/wmo/wmo.hpp>

#include <format>
#include <string>
#include <string_view>

#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::wmo
{
  namespace
  {
    /** "world\wmo\thing.wmo" -> "world\wmo\thing_007.wmo". */
    std::string group_path(std::string_view root_path, std::size_t index)
    {
      std::string_view stem = root_path;
      if (stem.ends_with(".wmo"))
        stem.remove_suffix(4);
      return std::format("{}_{:03}.wmo", stem, index);
    }

    Result<void> check_mver(std::uint32_t mver, std::string_view which)
    {
      if (mver != wmo_version_v17)
        return make_error(ErrorCode::FormatVersionMismatch,
                          std::format("{} MVER is {}, expected {}", which, mver, wmo_version_v17));
      return {};
    }
  }

  template <ClientVersion V>
  Result<Wmo<V>> Wmo<V>::parse(std::span<const std::byte> root_data,
                               std::span<const std::span<const std::byte>> group_datas)
  {
    Wmo<V> wmo;
    if (auto r = read_entity(wmo.root, root_data); !r)
      return std::unexpected{r.error()};
    if (auto r = check_mver(wmo.root.mver, "root"); !r)
      return std::unexpected{r.error()};

    wmo.groups.reserve(group_datas.size());
    for (std::size_t i = 0; i < group_datas.size(); ++i)
    {
      WmoGroup<V> group;
      if (auto r = read_entity(group, group_datas[i]); !r)
        return make_error(r.error().code,
                          std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
      if (auto r = check_mver(group.mver, std::format("group {}", i)); !r)
        return std::unexpected{r.error()};
      wmo.groups.push_back(std::move(group));
    }
    return wmo;
  }

  template <ClientVersion V>
  Result<Wmo<V>> Wmo<V>::load(fs::FileSystem& fs, const FileKey& key)
  {
    const auto root_data = fs.read_file(key);
    if (!root_data)
      return std::unexpected{root_data.error()};

    Wmo<V> wmo;
    if (auto r = read_entity(wmo.root, *root_data); !r)
      return std::unexpected{r.error()};
    if (auto r = check_mver(wmo.root.mver, "root"); !r)
      return std::unexpected{r.error()};

    const std::size_t n_groups = wmo.root.header.n_groups;
    const bool by_fdid = wmo.root.group_fdids.size() >= n_groups;

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

    wmo.groups.reserve(n_groups);
    for (std::size_t i = 0; i < n_groups; ++i)
    {
      const FileKey group_key = by_fdid ? FileKey{FileDataID{wmo.root.group_fdids[i]}}
                                        : FileKey{group_path(root_path, i)};
      const auto group_data = fs.read_file(group_key);
      if (!group_data)
        return make_error(group_data.error().code,
                          std::format("group {}: {}", i, group_data.error().message),
                          group_data.error().native_error);

      WmoGroup<V> group;
      if (auto r = read_entity(group, *group_data); !r)
        return make_error(r.error().code, std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
      if (auto r = check_mver(group.mver, std::format("group {}", i)); !r)
        return std::unexpected{r.error()};
      wmo.groups.push_back(std::move(group));
    }
    return wmo;
  }

  template <ClientVersion V>
  Result<void> Wmo<V>::save(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "saving a WMO needs a path for the root key");

    const auto root_data = write_root();
    if (!root_data)
      return std::unexpected{root_data.error()};
    if (auto r = fs.add_file(*resolved.path, *root_data); !r)
      return std::unexpected{r.error()};

    for (std::size_t i = 0; i < groups.size(); ++i)
    {
      const auto group_data = write_group(i);
      if (!group_data)
        return std::unexpected{group_data.error()};
      if (auto r = fs.add_file(group_path(*resolved.path, i), *group_data); !r)
        return make_error(r.error().code, std::format("group {}: {}", i, r.error().message),
                          r.error().native_error);
    }
    return {};
  }

  template <ClientVersion V>
  Result<FileBuffer> Wmo<V>::write_root() const
  {
    return write(root);
  }

  template <ClientVersion V>
  Result<FileBuffer> Wmo<V>::write_group(std::size_t index) const
  {
    if (index >= groups.size())
      return make_error(ErrorCode::FileNotFound,
                        std::format("group index {} out of range ({} groups)", index,
                                    groups.size()));
    return write(groups[index]);
  }

  template struct WmoRoot<versions::wotlk>;
  template struct WmoRoot<versions::shadowlands>;
  template struct WmoGroupBody<versions::wotlk>;
  template struct WmoGroupBody<versions::shadowlands>;
  template struct WmoGroup<versions::wotlk>;
  template struct WmoGroup<versions::shadowlands>;
  template struct Wmo<versions::wotlk>;
  template struct Wmo<versions::shadowlands>;
}
