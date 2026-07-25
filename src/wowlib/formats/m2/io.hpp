#pragma once

/** @file
    The M2 family's filesystem I/O — the out-of-line definitions of
    M2<V>::read/write and Skeleton<V>::read/write plus M2's private I/O
    helpers (declared in m2.hpp). A separate header (rather than m2.hpp) so
    parse-only consumers do not pull the fs::FileSystem dependency, and so
    the definitions are visible for implicit instantiation: the library
    ships NO explicit instantiations — every consumer TU instantiates
    exactly the versions it uses (the language bindings instantiate the
    full version matrix in their own translation units, see
    bindings/python/instantiations/). Include this header (or the
    wowlib.hpp umbrella) wherever the fs-level verbs are called. */

#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

#include <wowlib/formats/common/offset_serializer.hpp>
#include <wowlib/formats/m2/detail/satellite_io.hpp>
#include <wowlib/formats/m2/m2.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::m2
{
  template <ClientVersion V>
  Result<void> detail::M2<V>::check_header(std::uint32_t magic, std::uint32_t format_version)
  {
    if (magic != md20_magic)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("not an MD20 model (magic {:#010x}; a Legion+ chunked "
                                    "file starts with MD21 instead)",
                                    magic));
    constexpr auto range = m2_wire_version_range(V);
    if (format_version < range.first || format_version > range.second)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("MD20 version {} is outside the requested client's "
                                    "era [{}, {}]",
                                    format_version, range.first, range.second));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::read_skin_into(fs::FileSystem& fs, const FileKey& key,
                                             std::string_view what, auto& out)
    requires (V >= m2_per_sequence_timelines)
  {
    const auto bytes = fs.read_file(key);
    if (!bytes)
      return make_error(bytes.error().code, std::format("{}: {}", what, bytes.error().message),
                        bytes.error().native_error);
    Skin<V> skin;
    if (auto r = skin.read(std::span<const std::byte>{*bytes}); !r)
      return make_error(r.error().code, std::format("{}: {}", what, r.error().message),
                        r.error().native_error);
    if (skin.magic != skin_magic)
      return make_error(ErrorCode::FormatVersionMismatch,
                        std::format("{} magic is {:#010x}, expected 'SKIN'", what, skin.magic));
    out.push_back(std::move(skin));
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::read_monolithic(fs::FileSystem& fs, const FileKey& key,
                                      std::span<const std::byte> main)
    requires (V >= m2_per_sequence_timelines)
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable,
                        "a monolithic M2's satellite files (.skin/.anim) need a "
                        "resolvable path for the model key");
    const std::string stem = detail::m2_stem(*resolved.path);

    // Low-priority sequence data lives in per-sequence .anim files; the
    // context resolves them lazily — the sequences table is populated
    // before any track member reads (wire order), so the flags are already
    // decoded when the first track consults us. A missing .anim file
    // leaves its sequences' tracks empty rather than failing.
    detail::AnimCache cache{[&](std::uint16_t id, std::uint16_t sub) {
      return fs.read_file(FileKey{detail::anim_path(stem, id, sub)});
    }};
    OffsetReadContext ctx;
    ctx.sequence_base =
      detail::make_sequence_base(this->root.sequences, cache, main, detail::afm2_magic);
    if (auto r = this->root.read(main, ctx); !r)
      return r;
    if (auto r = check_header(this->root.magic, this->root.format_version); !r)
      return r;

    this->skins.reserve(this->root.num_skin_profiles);
    for (std::uint32_t i = 0; i < this->root.num_skin_profiles; ++i)
      if (auto r = read_skin_into(fs, FileKey{detail::skin_path(stem, i)},
                                  std::format("skin {}", i), this->skins);
          !r)
        return r;
    return {};
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::read_chunked(fs::FileSystem& fs, const FileKey& key,
                                   std::span<const std::byte> main)
    requires (V >= m2_chunked_container)
  {
    if (auto r = this->chunks.read(main); !r)
      return r;

    const bool has_skel = skeleton_engaged(this->chunks.skeleton_fdid);
    if (has_skel)
      if (auto r = this->skel.read(fs, FileKey{FileDataID{this->chunks.skeleton_fdid.front()}});
          !r)
        return make_error(r.error().code, std::format(".skel: {}", r.error().message),
                          r.error().native_error);

    // name fallback for satellites without FileDataID entries
    std::optional<std::string> stem;
    if (const FileKey resolved = fs.resolve(key); resolved.path)
      stem = detail::m2_stem(*resolved.path);

    const std::span<const std::byte> md21{this->chunks.md21.bytes};

    // skel-based models keep their sequences (and thus the external-data
    // flags) in the skeleton; the body's own table is empty then
    const auto* sequences =
      has_skel ? &this->skel.sequence_block.sequences : &this->root.sequences;
    const auto& afids = has_skel ? this->skel.effective_anim_fdids() : this->chunks.anim_fdids;

    detail::AnimCache cache{[&, stem](std::uint16_t id, std::uint16_t sub) -> Result<FileBuffer> {
      const std::uint32_t fdid = detail::afid_lookup(afids, id, sub);
      if (fdid != 0)
        return fs.read_file(FileKey{FileDataID{fdid}});
      if (stem)
        return fs.read_file(FileKey{detail::anim_path(*stem, id, sub)});
      return make_error(ErrorCode::FileNotFound, "no AFID entry and no path");
    }};
    OffsetReadContext ctx;
    ctx.sequence_base = detail::make_sequence_base(*sequences, cache, md21, detail::afm2_magic);
    if (auto r = this->root.read(md21, ctx); !r)
      return r;
    if (auto r = check_header(this->root.magic, this->root.format_version); !r)
      return r;

    // skins: the first num_skin_profiles SFID entries are the views, the
    // rest the LOD bands (real files occasionally truncate the LOD tail)
    if (this->chunks.skin_fdids.size() < this->root.num_skin_profiles)
      return make_error(ErrorCode::InvalidEntityState,
                        std::format("SFID holds {} entries, the body declares {} views",
                                    this->chunks.skin_fdids.size(),
                                    this->root.num_skin_profiles));
    for (std::uint32_t i = 0; i < this->root.num_skin_profiles; ++i)
      if (auto r = read_skin_into(fs, FileKey{FileDataID{this->chunks.skin_fdids[i]}},
                                  std::format("skin {}", i), this->skins);
          !r)
        return r;
    for (std::size_t i = this->root.num_skin_profiles; i < this->chunks.skin_fdids.size(); ++i)
      if (this->chunks.skin_fdids[i] != 0)
        if (auto r = read_skin_into(fs, FileKey{FileDataID{this->chunks.skin_fdids[i]}},
                                    std::format("lod skin {}", i), this->lod_skins);
            !r)
          return r;

    // .bone files (the skeleton's when skel-based)
    const auto& bfids = has_skel ? this->skel.effective_bone_fdids() : this->chunks.bone_fdids;
    for (std::size_t i = 0; i < bfids.size(); ++i)
    {
      if (bfids[i] == 0)
        continue;
      const auto bytes = fs.read_file(FileKey{FileDataID{bfids[i]}});
      if (!bytes)
        return make_error(bytes.error().code,
                          std::format(".bone {}: {}", i, bytes.error().message),
                          bytes.error().native_error);
      BoneFile bone;
      if (auto r = bone.read(std::span<const std::byte>{*bytes}); !r)
        return make_error(r.error().code, std::format(".bone {}: {}", i, r.error().message),
                          r.error().native_error);
      this->bone_files.push_back(std::move(bone));
    }

    // physics: referenced file baked in verbatim (inline PFDC stays a chunk
    // on the stream); a missing file degrades to empty
    if (!this->chunks.phys_fdid.empty() && this->chunks.phys_fdid.front() != 0)
      if (auto bytes = fs.read_file(FileKey{FileDataID{this->chunks.phys_fdid.front()}}))
        this->phys.bytes = std::move(*bytes);

    // the MD21 transport blob is spent once the body is decoded — drop it
    // rather than keep a second whole-model image in memory; write()
    // re-encodes root into the stream (the md21 span above dies with it,
    // hence last)
    this->chunks.md21.bytes = {};
    return {};
  }

  template <ClientVersion V>
    requires (V >= m2_chunked_container)
  Result<void> detail::Skeleton<V>::read(fs::FileSystem& fs, const FileKey& key)
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
      ctx.sequence_base =
        detail::make_sequence_base(sequence_block.sequences, cache, inline_base, window);
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
  Result<void> detail::Skeleton<V>::write(fs::FileSystem& fs, const FileKey& key) const
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
    {
      auto encoded =
        bone_block.write(detail::make_sequence_sink(sequence_block.sequences, afsb_bufs));
      if (!encoded)
        return std::unexpected{encoded.error()};
      copy.skb1.bytes = std::move(*encoded);
      auto attachments =
        attachment_block.write(detail::make_sequence_sink(sequence_block.sequences, afsa_bufs));
      if (!attachments)
        return std::unexpected{attachments.error()};
      copy.ska1.bytes = std::move(*attachments);
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

  template <ClientVersion V>
  Result<void> detail::M2<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto main = fs.read_file(key);
    if (!main)
      return std::unexpected{main.error()};

    root = {};
    if constexpr (V >= m2_per_sequence_timelines)
      this->skins.clear();
    if constexpr (V >= m2_chunked_container)
    {
      this->chunks = {};
      this->lod_skins.clear();
      this->phys = {};
      this->skel = {};
      this->bone_files.clear();
    }

    if constexpr (V < m2_per_sequence_timelines)
    {
      if (auto r = root.read(std::span<const std::byte>{*main}); !r)
        return r;
      return check_header(root.magic, root.format_version);
    }
    else if constexpr (V < m2_chunked_container)
    {
      return read_monolithic(fs, key, std::span<const std::byte>{*main});
    }
    else
    {
      std::uint32_t lead = 0;
      if (main->size() >= 4)
        std::memcpy(&lead, main->data(), 4);
      if (lead == md20_magic)
      {
        // Legion clients still served leftover raw MD20 models; from BfA on
        // none exist, so a bare image under a BfA+ target is a mismatch, not
        // a fallback (m2_chunked_only).
        if constexpr (V < m2_chunked_only)
          return read_monolithic(fs, key, std::span<const std::byte>{*main});
        else
          return make_error(ErrorCode::FormatVersionMismatch,
                            "bare MD20 model under a BfA+ target — raw (unchunked) .m2 files "
                            "no longer exist from 8.0 on; if this is a Legion-era file, read "
                            "it with the legion target");
      }
      return read_chunked(fs, key, std::span<const std::byte>{*main});
    }
  }

  template <ClientVersion V>
  Result<void> detail::M2<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable, "saving an M2 needs a path for the key");
    const std::string stem = detail::m2_stem(*resolved.path);

    if constexpr (V < m2_per_sequence_timelines)
    {
      const auto bytes = root.write();
      if (!bytes)
        return std::unexpected{bytes.error()};
      if (auto r = fs.add_file(*resolved.path, *bytes); !r)
        return std::unexpected{r.error()};
      return {};
    }
    else
    {
      // whose sequence table drives the .anim split
      const auto* sequences = &root.sequences;
      bool has_skel = false;
      if constexpr (V >= m2_chunked_container)
      {
        // the SAME engagement predicate the read path uses — a stored SKID of
        // 0 must not flip a non-skel model into a skel-based write
        has_skel = skeleton_engaged(this->chunks.skeleton_fdid);
        if (has_skel)
          sequences = &this->skel.sequence_block.sequences;
      }

      // Low-priority sequences split back out: every external sequence gets
      // an .anim buffer, filled as the tracks route their per-sequence
      // blocks through the sinks (empty ones still write — the client
      // requests the file whenever the flags say so).
      std::map<std::uint32_t, FileBuffer> afm2_bufs;
      const auto make_sink = [sequences](std::map<std::uint32_t, FileBuffer>& bufs) {
        return detail::make_sequence_sink(*sequences, bufs);
      };
      auto bytes = root.write(make_sink(afm2_bufs));
      if (!bytes)
        return std::unexpected{bytes.error()};

      // stamp the derived skin count into the freshly written image — the
      // baked skins vector is the source of truth (num_skin_profiles is a
      // hidden wire field, see M2Root)
      {
        constexpr std::size_t count_at = wire_offset_of<M2Root<V>>("num_skin_profiles");
        const auto count = static_cast<std::uint32_t>(this->skins.size());
        std::memcpy(bytes->data() + count_at, &count, sizeof count);
      }

      if constexpr (V < m2_chunked_container)
      {
        // pre-Legion: raw .anim payloads under conventional names
        if (auto r = fs.add_file(*resolved.path, *bytes); !r)
          return std::unexpected{r.error()};
        for (const auto& [packed, buf] : afm2_bufs)
          if (auto r =
                fs.add_file(detail::anim_path(stem, packed >> 16, packed & 0xFFFFu), buf);
              !r)
            return make_error(r.error().code,
                              std::format("anim {:04}-{:02}: {}", packed >> 16,
                                          packed & 0xFFFFu, r.error().message),
                              r.error().native_error);
        for (std::size_t i = 0; i < this->skins.size(); ++i)
        {
          const auto skin_bytes = this->skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          if (auto r = fs.add_file(detail::skin_path(stem, static_cast<std::uint32_t>(i)),
                                   *skin_bytes);
              !r)
            return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                              r.error().native_error);
        }
        return {};
      }
      else
      {
        // rebuild the chunk stream around the re-encoded image: satellites
        // write first so their fresh FileDataIDs land in the reference chunks
        M2ChunkedFile<V> stream = this->chunks;
        stream.md21.bytes = std::move(*bytes);

        // .bone files (fdids land in the skeleton for skel-based models)
        std::vector<std::uint32_t> bone_fdids;
        for (std::size_t i = 0; i < this->bone_files.size(); ++i)
        {
          const auto bone_bytes = this->bone_files[i].write();
          if (!bone_bytes)
            return std::unexpected{bone_bytes.error()};
          const auto r = fs.add_file(detail::bone_path(stem, static_cast<std::uint32_t>(i)),
                                     *bone_bytes);
          if (!r)
            return make_error(r.error().code,
                              std::format(".bone {}: {}", i, r.error().message),
                              r.error().native_error);
          bone_fdids.push_back(r->value);
        }

        stream.anim_fdids.clear();
        if (!has_skel)
        {
          // .anim payloads, AFM2-wrapped when the model asks for chunked ones
          for (const auto& [packed, buf] : afm2_bufs)
          {
            FileBuffer file;
            if ((root.global_flags & 0x2000u) != 0)
              detail::append_chunk(file, detail::afm2_magic, buf);
            else
              file = buf;
            const auto r =
              fs.add_file(detail::anim_path(stem, packed >> 16, packed & 0xFFFFu), file);
            if (!r)
              return make_error(r.error().code,
                                std::format("anim {:04}-{:02}: {}", packed >> 16,
                                            packed & 0xFFFFu, r.error().message),
                                r.error().native_error);
            stream.anim_fdids.push_back({static_cast<std::uint16_t>(packed >> 16),
                                        static_cast<std::uint16_t>(packed & 0xFFFFu),
                                        r->value});
          }
          stream.bone_fdids = bone_fdids;
        }
        else
        {
          // skel-based: re-encode the skeleton blocks, then assemble the
          // shared .anim files as AFM2 (body events) + AFSA (attachments) +
          // AFSB (bones) and hang every satellite id off the skeleton
          // deduced: inside m2::detail the bare Skeleton names the RAW
          // template, but the member is the collapsed m2::Skeleton alias type
          auto skel_copy = this->skel;
          std::map<std::uint32_t, FileBuffer> afsa_bufs;
          std::map<std::uint32_t, FileBuffer> afsb_bufs;
          {
            auto encoded = this->skel.bone_block.write(make_sink(afsb_bufs));
            if (!encoded)
              return std::unexpected{encoded.error()};
            skel_copy.skb1.bytes = std::move(*encoded);
            auto attachments = this->skel.attachment_block.write(make_sink(afsa_bufs));
            if (!attachments)
              return std::unexpected{attachments.error()};
            skel_copy.ska1.bytes = std::move(*attachments);
          }

          skel_copy.anim_fdids.clear();
          std::set<std::uint32_t> keys;
          for (const auto& [k, buf] : afm2_bufs)
            keys.insert(k);
          for (const auto& [k, buf] : afsa_bufs)
            keys.insert(k);
          for (const auto& [k, buf] : afsb_bufs)
            keys.insert(k);
          for (const std::uint32_t packed : keys)
          {
            FileBuffer file;
            detail::append_chunk(file, detail::afm2_magic, afm2_bufs[packed]);
            detail::append_chunk(file, detail::afsa_magic, afsa_bufs[packed]);
            detail::append_chunk(file, detail::afsb_magic, afsb_bufs[packed]);
            const auto r =
              fs.add_file(detail::anim_path(stem, packed >> 16, packed & 0xFFFFu), file);
            if (!r)
              return make_error(r.error().code,
                                std::format("anim {:04}-{:02}: {}", packed >> 16,
                                            packed & 0xFFFFu, r.error().message),
                                r.error().native_error);
            skel_copy.anim_fdids.push_back({static_cast<std::uint16_t>(packed >> 16),
                                            static_cast<std::uint16_t>(packed & 0xFFFFu),
                                            r->value});
          }
          skel_copy.bone_fdids = bone_fdids;

          const auto skel_bytes = skel_copy.ChunkedFile<m2::Skeleton<V>>::write();
          if (!skel_bytes)
            return std::unexpected{skel_bytes.error()};
          const auto r = fs.add_file(detail::skel_path(stem), *skel_bytes);
          if (!r)
            return make_error(r.error().code, std::format(".skel: {}", r.error().message),
                              r.error().native_error);
          stream.skeleton_fdid.assign(1, r->value);
        }

        stream.skin_fdids.clear();
        for (std::size_t i = 0; i < this->skins.size(); ++i)
        {
          const auto skin_bytes = this->skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          const auto r = fs.add_file(detail::skin_path(stem, static_cast<std::uint32_t>(i)),
                                     *skin_bytes);
          if (!r)
            return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                              r.error().native_error);
          stream.skin_fdids.push_back(r->value);
        }
        for (std::size_t i = 0; i < this->lod_skins.size(); ++i)
        {
          const auto skin_bytes = this->lod_skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          const auto r = fs.add_file(
            detail::lod_skin_path(stem, static_cast<std::uint32_t>(i) + 1), *skin_bytes);
          if (!r)
            return make_error(r.error().code,
                              std::format("lod skin {}: {}", i, r.error().message),
                              r.error().native_error);
          stream.skin_fdids.push_back(r->value);
        }

        stream.phys_fdid.clear();
        if (!this->phys.bytes.empty())
        {
          const auto r = fs.add_file(stem + ".phys", this->phys.bytes);
          if (!r)
            return make_error(r.error().code, std::format(".phys: {}", r.error().message),
                              r.error().native_error);
          stream.phys_fdid.push_back(r->value);
        }

        const auto stream_bytes = stream.write();
        if (!stream_bytes)
          return std::unexpected{stream_bytes.error()};
        if (auto r = fs.add_file(*resolved.path, *stream_bytes); !r)
          return std::unexpected{r.error()};
        return {};
      }
    }
  }
}
