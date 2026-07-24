#include <wowlib/formats/m2/skel.hpp>

#include <format>
#include <map>
#include <set>
#include <span>
#include <string>

#include <wowlib/formats/common/offset_serializer.hpp>
#include <wowlib/formats/m2/m2.hpp>
#include <wowlib/formats/m2/satellite_io.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::m2
{
  template <ClientVersion V>
    requires (V >= m2_chunked_container)
  Result<void> Skeleton<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto bytes = fs.read_file(key);
    if (!bytes)
      return std::unexpected{bytes.error()};

    *this = Skeleton{};
    if (auto r = ChunkedFile<Skeleton<V>>::read(std::span<const std::byte>{*bytes}); !r)
      return r;

    // A child skeleton shares the parent's satellite ids (only the *FID
    // chunks are shared — the child keeps its own SK*1 data). One hop, per
    // every known example; a missing parent degrades to inline-only decode.
    if (!parent_link.empty() && parent_link.front().parent_skel_file_id != 0)
      if (auto pbytes = fs.read_file(FileKey{FileDataID{parent_link.front().parent_skel_file_id}}))
      {
        Skeleton<V> parent;
        if (parent.ChunkedFile<Skeleton<V>>::read(std::span<const std::byte>{*pbytes}))
        {
          parent_anim_fdids = parent.anim_fdids;
          parent_bone_fdids = parent.bone_fdids;
        }
      }

    // name fallback for satellites without FileDataID entries
    std::optional<std::string> stem;
    if (const FileKey resolved = fs.resolve(key); resolved.path)
      stem = detail::m2_stem(*resolved.path);

    detail::AnimCache cache{[&](std::uint16_t id, std::uint16_t sub) -> Result<FileBuffer> {
      const std::uint32_t fdid = detail::afid_lookup(effective_anim_fdids(), id, sub);
      if (fdid != 0)
        return fs.read_file(FileKey{FileDataID{fdid}});
      if (stem)
        return fs.read_file(FileKey{detail::anim_path(*stem, id, sub)});
      return make_error(ErrorCode::FileNotFound, "no AFID entry and no path");
    }};

    const auto make_ctx = [&](std::span<const std::byte> inline_base, std::uint32_t window) {
      OffsetReadContext ctx;
      ctx.sequence_base = [this, &cache, inline_base,
                           window](std::size_t i) -> std::span<const std::byte> {
        const auto& seqs = sequence_block.sequences;
        if (i >= seqs.size())
          return inline_base;
        const auto& s = seqs[i];
        if (!detail::owns_anim_file(s))
          return inline_base;
        return cache.window(s.id, s.variation_index, window);
      };
      return ctx;
    };

    if (!skb1.bytes.empty())
    {
      const std::span<const std::byte> base{skb1.bytes};
      if (auto r = bone_block.read(base, make_ctx(base, detail::afsb_magic)); !r)
        return make_error(r.error().code, std::format("SKB1: {}", r.error().message),
                          r.error().native_error);
    }
    if (!ska1.bytes.empty())
    {
      const std::span<const std::byte> base{ska1.bytes};
      if (auto r = attachment_block.read(base, make_ctx(base, detail::afsa_magic)); !r)
        return make_error(r.error().code, std::format("SKA1: {}", r.error().message),
                          r.error().native_error);
    }
    // the blobs stay as read: an untouched skeleton written at chunk level
    // remains byte-perfect; the fs write path re-encodes them from the
    // typed blocks.
    return {};
  }

  template <ClientVersion V>
    requires (V >= m2_chunked_container)
  Result<void> Skeleton<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "saving a skeleton needs a path for the key");
    const std::string stem = detail::m2_stem(*resolved.path);

    Skeleton<V> copy = *this;

    // re-encode the bone/attachment blocks, splitting external sequences
    // into per-sequence AFSB/AFSA buffers
    std::map<std::uint32_t, FileBuffer> afsa_bufs;
    std::map<std::uint32_t, FileBuffer> afsb_bufs;
    const auto make_wctx = [&](std::map<std::uint32_t, FileBuffer>& bufs) {
      OffsetWriteContext ctx;
      ctx.sequence_sink = [this, &bufs](std::size_t i) -> FileBuffer* {
        const auto& seqs = sequence_block.sequences;
        if (i >= seqs.size())
          return nullptr;
        const auto& s = seqs[i];
        if (!detail::owns_anim_file(s))
          return nullptr;
        return &bufs[detail::anim_key(s.id, s.variation_index)];
      };
      return ctx;
    };
    {
      const auto encoded = bone_block.write(make_wctx(afsb_bufs));
      if (!encoded)
        return std::unexpected{encoded.error()};
      copy.skb1.bytes = *encoded;
      const auto attachments = attachment_block.write(make_wctx(afsa_bufs));
      if (!attachments)
        return std::unexpected{attachments.error()};
      copy.ska1.bytes = *attachments;
    }

    // the skeleton's .anim files: AFSA + AFSB sections. NOTE: a paired
    // model's AFM2 (event) section is not represented here — the owning
    // M2's write is the full-fidelity save path.
    copy.anim_fdids.clear();
    std::set<std::uint32_t> keys;
    for (const auto& [k, buf] : afsa_bufs)
      keys.insert(k);
    for (const auto& [k, buf] : afsb_bufs)
      keys.insert(k);
    for (const std::uint32_t packed : keys)
    {
      FileBuffer file;
      detail::append_chunk(file, detail::afsa_magic, afsa_bufs[packed]);
      detail::append_chunk(file, detail::afsb_magic, afsb_bufs[packed]);
      const auto r =
        fs.add_file(detail::anim_path(stem, packed >> 16, packed & 0xFFFFu), file);
      if (!r)
        return make_error(r.error().code,
                          std::format("anim {:04}-{:02}: {}", packed >> 16, packed & 0xFFFFu,
                                      r.error().message),
                          r.error().native_error);
      copy.anim_fdids.push_back({static_cast<std::uint16_t>(packed >> 16),
                                 static_cast<std::uint16_t>(packed & 0xFFFFu), r->value});
    }

    const auto bytes = copy.ChunkedFile<Skeleton<V>>::write();
    if (!bytes)
      return std::unexpected{bytes.error()};
    if (auto r = fs.add_file(*resolved.path, *bytes); !r)
      return std::unexpected{r.error()};
    return {};
  }

#define WOWLIB_M2_INSTANTIATE_SKELETON(Suffix, version_)                                           \
  template struct Skeleton<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_INSTANTIATE_SKELETON)
#undef WOWLIB_M2_INSTANTIATE_SKELETON
}

namespace wowlib::formats
{
#define WOWLIB_M2_INSTANTIATE_SKELETON_SERIALIZER(Suffix, version_)                                \
  template struct ChunkedFile<m2::Skeleton<versions::version_>>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_INSTANTIATE_SKELETON_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_SKELETON_SERIALIZER

  template struct ChunkedFile<m2::BoneFile>;
}
