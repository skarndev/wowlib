#include <wowlib/formats/m2/m2.hpp>

#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <wowlib/formats/common/offset_serializer.hpp>
#include <wowlib/fs/filesystem.hpp>

namespace wowlib::formats::m2
{
  namespace
  {
    /** The model path without its extension: "creature\\x\\x.m2" -> "creature\\x\\x"
        (.mdx and .mdl spellings appear in old data references).
        @param path the model file path.
        @return the stem satellite names derive from. */
    std::string m2_stem(std::string_view path)
    {
      for (std::string_view ext : {".m2", ".M2", ".mdx", ".MDX", ".mdl", ".MDL"})
        if (path.ends_with(ext))
          return std::string{path.substr(0, path.size() - ext.size())};
      return std::string{path};
    }

    /** "{stem}0N.skin" — the numbered view naming convention. */
    std::string skin_path(std::string_view stem, std::uint32_t index)
    {
      return std::format("{}{:02}.skin", stem, index);
    }

    /** "{stem}_lod0N.skin" — LOD bands number from 1. */
    std::string lod_skin_path(std::string_view stem, std::uint32_t band)
    {
      return std::format("{}_lod{:02}.skin", stem, band);
    }

    /** "{stem}AAAA-SS.anim" — the external-sequence naming. */
    std::string anim_path(std::string_view stem, std::uint32_t anim_id, std::uint32_t sub_id)
    {
      return std::format("{}{:04}-{:02}.anim", stem, anim_id, sub_id);
    }

    /** The anim-cache key for a sequence: (id, variation) packed. */
    constexpr std::uint32_t anim_key(std::uint16_t id, std::uint16_t variation)
    {
      return (static_cast<std::uint32_t>(id) << 16) | variation;
    }

    /** The AFM2 fourcc as scanned from disk (forward, like all M2 chunks). */
    inline constexpr std::uint32_t afm2_magic = four_cc("AFM2", FourCCEndian::forward);

    /** Verify the MD20 magic and that the file's format version belongs to
        @a V's era.
        @tparam V the entity version the caller parsed with.
        @param magic          the leading magic read from the file.
        @param format_version the version field read from the file.
        @return nothing, or FormatVersionMismatch. */
    template <ClientVersion V>
    Result<void> check_header(std::uint32_t magic, std::uint32_t format_version)
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

    /** Whether sequence @a s owns an external .anim file: its data is
        low-priority (0x130 clear) and it is no alias (aliases resolve to
        another sequence's data). */
    template <typename Sequence>
    bool owns_anim_file(const Sequence& s)
    {
      return records::sequence_data_external(s.flags) && (s.flags & 0x40u) == 0;
    }

    /** A loaded .anim file plus the window track offsets resolve against —
        the AFM2 payload for chunked files, the whole file otherwise. */
    struct AnimSlot
    {
      std::optional<FileBuffer> file;
      std::size_t offset = 0;
      std::size_t length = 0;
    };

    /** Locate the resolution window of a loaded .anim file: chunked files
        (leading AFM2) expose that chunk's payload, raw files their whole
        content.
        @param slot the slot whose file just loaded; offset/length set. */
    void locate_anim_window(AnimSlot& slot)
    {
      const FileBuffer& buf = *slot.file;
      slot.offset = 0;
      slot.length = buf.size();
      if (buf.size() < 8)
        return;
      std::uint32_t lead = 0;
      std::memcpy(&lead, buf.data(), 4);
      if (lead != afm2_magic)
        return;
      // chunked: scan for AFM2 (first in practice, but chunks may reorder)
      for (std::size_t pos = 0; pos + 8 <= buf.size();)
      {
        std::uint32_t fourcc = 0;
        std::uint32_t size = 0;
        std::memcpy(&fourcc, buf.data() + pos, 4);
        std::memcpy(&size, buf.data() + pos + 4, 4);
        if (size > buf.size() - pos - 8)
          break;
        if (fourcc == afm2_magic)
        {
          slot.offset = pos + 8;
          slot.length = size;
          return;
        }
        pos += 8 + size;
      }
      slot.length = 0;  // chunked but no usable AFM2: treat as missing
    }

    /** The read path shared by every monolithic-with-satellites era (WotLK
        through pre-Legion, and Legion+ files still shipping raw MD20):
        decode the body with name-based .anim resolution, then load the
        numbered .skin views.
        @param self the assembly being filled.
        @param fs   the filesystem gateway.
        @param key  the model identity.
        @param main the model file bytes.
        @return nothing, or the first error. */
    template <ClientVersion V>
    Result<void> read_monolithic(M2<V>& self, fs::FileSystem& fs, const FileKey& key,
                                 std::span<const std::byte> main)
    {
      const FileKey resolved = fs.resolve(key);
      if (!resolved.path)
        return make_error(ErrorCode::PathNotResolvable,
                          "a monolithic M2's satellite files (.skin/.anim) need a "
                          "resolvable path for the model key");
      const std::string stem = m2_stem(*resolved.path);

      // Low-priority sequence data lives in per-sequence .anim files; the
      // context resolves them lazily — the sequences table is populated
      // before any track member reads (wire order), so the flags are already
      // decoded when the first track consults us. A missing .anim file
      // leaves its sequences' tracks empty rather than failing.
      std::map<std::uint32_t, AnimSlot> anim_cache;
      OffsetReadContext ctx;
      ctx.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
        if (i >= self.data.sequences.size())
          return main;
        const auto& s = self.data.sequences[i];
        if (!owns_anim_file(s))
          return main;
        auto [it, inserted] = anim_cache.try_emplace(anim_key(s.id, s.variation_index));
        if (inserted)
          if (auto file = fs.read_file(FileKey{anim_path(stem, s.id, s.variation_index)}))
          {
            it->second.file = std::move(*file);
            locate_anim_window(it->second);
          }
        if (!it->second.file || it->second.length == 0)
          return {};
        return std::span<const std::byte>{*it->second.file}.subspan(it->second.offset,
                                                                    it->second.length);
      };
      if (auto r = self.data.read(main, ctx); !r)
        return r;
      if (auto r = check_header<V>(self.data.magic, self.data.format_version); !r)
        return r;

      self.skins.reserve(self.data.num_skin_profiles);
      for (std::uint32_t i = 0; i < self.data.num_skin_profiles; ++i)
      {
        const auto skin_data = fs.read_file(FileKey{skin_path(stem, i)});
        if (!skin_data)
          return make_error(skin_data.error().code,
                            std::format("skin {}: {}", i, skin_data.error().message),
                            skin_data.error().native_error);
        Skin<V> skin;
        if (auto r = skin.read(std::span<const std::byte>{*skin_data}); !r)
          return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                            r.error().native_error);
        if (skin.magic != skin_magic)
          return make_error(ErrorCode::FormatVersionMismatch,
                            std::format("skin {} magic is {:#010x}, expected 'SKIN'", i,
                                        skin.magic));
        self.skins.push_back(std::move(skin));
      }
      return {};
    }

    /** The Legion+ chunked read path: parse the shell, then decode the MD21
        image with FileDataID-based satellite resolution.
        @param self the assembly being filled.
        @param fs   the filesystem gateway.
        @param key  the model identity.
        @param main the model file bytes.
        @return nothing, or the first error. */
    template <ClientVersion V>
    Result<void> read_chunked(M2<V>& self, fs::FileSystem& fs, const FileKey& key,
                              std::span<const std::byte> main)
    {
      if (auto r = self.shell.read(main); !r)
        return r;
      if (!self.shell.skeleton_fdid.empty() && self.shell.skeleton_fdid.front() != 0)
        return make_error(ErrorCode::NotImplemented,
                          ".skel-based models are not implemented yet (stage 3b)");

      // name fallback for satellites without FileDataID entries
      std::optional<std::string> stem;
      if (const FileKey resolved = fs.resolve(key); resolved.path)
        stem = m2_stem(*resolved.path);

      const std::span<const std::byte> md21{self.shell.md21.bytes};

      const auto afid_lookup = [&](std::uint16_t id, std::uint16_t sub) -> std::uint32_t {
        for (const AnimFileEntry& e : self.shell.anim_fdids)
          if (e.anim_id == id && e.sub_anim_id == sub)
            return e.file_id;
        return 0;
      };

      std::map<std::uint32_t, AnimSlot> anim_cache;
      OffsetReadContext ctx;
      ctx.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
        if (i >= self.data.sequences.size())
          return md21;
        const auto& s = self.data.sequences[i];
        if (!owns_anim_file(s))
          return md21;
        auto [it, inserted] = anim_cache.try_emplace(anim_key(s.id, s.variation_index));
        if (inserted)
        {
          const std::uint32_t fdid = afid_lookup(s.id, s.variation_index);
          auto file = fdid != 0 ? fs.read_file(FileKey{FileDataID{fdid}})
                      : stem    ? fs.read_file(FileKey{anim_path(*stem, s.id, s.variation_index)})
                                : Result<FileBuffer>{std::unexpected{
                                    Error{ErrorCode::FileNotFound, "no AFID entry and no path"}}};
          if (file)
          {
            it->second.file = std::move(*file);
            locate_anim_window(it->second);
          }
        }
        if (!it->second.file || it->second.length == 0)
          return {};
        return std::span<const std::byte>{*it->second.file}.subspan(it->second.offset,
                                                                    it->second.length);
      };
      if (auto r = self.data.read(md21, ctx); !r)
        return r;
      if (auto r = check_header<V>(self.data.magic, self.data.format_version); !r)
        return r;

      // skins: the first num_skin_profiles SFID entries are the views, the
      // rest the LOD bands (real files occasionally truncate the LOD tail)
      const auto read_skin = [&](std::uint32_t fdid, std::string_view what,
                                 std::vector<Skin<V>>& out) -> Result<void> {
        const auto bytes = fs.read_file(FileKey{FileDataID{fdid}});
        if (!bytes)
          return make_error(bytes.error().code,
                            std::format("{}: {}", what, bytes.error().message),
                            bytes.error().native_error);
        Skin<V> skin;
        if (auto r = skin.read(std::span<const std::byte>{*bytes}); !r)
          return make_error(r.error().code, std::format("{}: {}", what, r.error().message),
                            r.error().native_error);
        if (skin.magic != skin_magic)
          return make_error(ErrorCode::FormatVersionMismatch,
                            std::format("{} magic is {:#010x}, expected 'SKIN'", what,
                                        skin.magic));
        out.push_back(std::move(skin));
        return {};
      };
      if (self.shell.skin_fdids.size() < self.data.num_skin_profiles)
        return make_error(ErrorCode::InvalidEntityState,
                          std::format("SFID holds {} entries, the body declares {} views",
                                      self.shell.skin_fdids.size(),
                                      self.data.num_skin_profiles));
      for (std::uint32_t i = 0; i < self.data.num_skin_profiles; ++i)
        if (auto r = read_skin(self.shell.skin_fdids[i], std::format("skin {}", i), self.skins);
            !r)
          return r;
      for (std::size_t i = self.data.num_skin_profiles; i < self.shell.skin_fdids.size(); ++i)
        if (self.shell.skin_fdids[i] != 0)
          if (auto r = read_skin(self.shell.skin_fdids[i], std::format("lod skin {}", i),
                                 self.lod_skins);
              !r)
            return r;

      // physics: referenced file baked in verbatim (inline PFDC stays on the
      // shell); a missing file degrades to empty
      if (!self.shell.phys_fdid.empty() && self.shell.phys_fdid.front() != 0)
        if (auto bytes = fs.read_file(FileKey{FileDataID{self.shell.phys_fdid.front()}}))
          self.phys.bytes = std::move(*bytes);
      return {};
    }
  }

  template <ClientVersion V>
  Result<void> M2<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    const auto main = fs.read_file(key);
    if (!main)
      return std::unexpected{main.error()};

    data = {};
    if constexpr (V >= m2_per_sequence_timelines)
      this->skins.clear();
    if constexpr (V >= m2_chunked_container)
    {
      this->shell = {};
      this->lod_skins.clear();
      this->phys = {};
    }

    if constexpr (V < m2_per_sequence_timelines)
    {
      if (auto r = data.read(std::span<const std::byte>{*main}); !r)
        return r;
      return check_header<V>(data.magic, data.format_version);
    }
    else if constexpr (V < m2_chunked_container)
    {
      return read_monolithic(*this, fs, key, std::span<const std::byte>{*main});
    }
    else
    {
      // Legion+ clients may still ship raw MD20 files — dispatch on the magic
      std::uint32_t lead = 0;
      if (main->size() >= 4)
        std::memcpy(&lead, main->data(), 4);
      if (lead == md20_magic)
        return read_monolithic(*this, fs, key, std::span<const std::byte>{*main});
      return read_chunked(*this, fs, key, std::span<const std::byte>{*main});
    }
  }

  template <ClientVersion V>
  Result<void> M2<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    const FileKey resolved = fs.resolve(key);
    if (!resolved.path)
      return make_error(ErrorCode::PathNotResolvable, "saving an M2 needs a path for the key");
    const std::string stem = m2_stem(*resolved.path);

    if constexpr (V < m2_per_sequence_timelines)
    {
      const auto bytes = data.write();
      if (!bytes)
        return std::unexpected{bytes.error()};
      if (auto r = fs.add_file(*resolved.path, *bytes); !r)
        return std::unexpected{r.error()};
      return {};
    }
    else
    {
      if (data.num_skin_profiles != this->skins.size())
        return make_error(ErrorCode::InvalidEntityState,
                          std::format("num_skin_profiles is {} but {} skins are baked in",
                                      data.num_skin_profiles, this->skins.size()));

      // Low-priority sequences split back out: every external sequence gets
      // an .anim buffer, filled as the tracks route their per-sequence
      // blocks through the sink (empty ones still write — the client
      // requests the file whenever the flags say so).
      std::map<std::uint32_t, FileBuffer> anim_bufs;
      OffsetWriteContext ctx;
      ctx.sequence_sink = [&](std::size_t i) -> FileBuffer* {
        if (i >= data.sequences.size())
          return nullptr;
        const auto& s = data.sequences[i];
        if (!owns_anim_file(s))
          return nullptr;
        return &anim_bufs[anim_key(s.id, s.variation_index)];
      };
      const auto bytes = data.write(ctx);
      if (!bytes)
        return std::unexpected{bytes.error()};

      // the .anim payloads, chunked (AFM2) when the model asks for it
      const auto anim_file_bytes = [&](const FileBuffer& buf) -> FileBuffer {
        if ((data.global_flags & 0x2000u) == 0)
          return buf;
        FileBuffer out;
        const std::uint32_t size = static_cast<std::uint32_t>(buf.size());
        out.resize(8);
        std::memcpy(out.data(), &afm2_magic, 4);
        std::memcpy(out.data() + 4, &size, 4);
        out.insert(out.end(), buf.begin(), buf.end());
        return out;
      };

      if constexpr (V < m2_chunked_container)
      {
        if (auto r = fs.add_file(*resolved.path, *bytes); !r)
          return std::unexpected{r.error()};
        for (const auto& [packed, buf] : anim_bufs)
          if (auto r = fs.add_file(anim_path(stem, packed >> 16, packed & 0xFFFFu),
                                   anim_file_bytes(buf));
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
          if (auto r =
                fs.add_file(skin_path(stem, static_cast<std::uint32_t>(i)), *skin_bytes);
              !r)
            return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                              r.error().native_error);
        }
        return {};
      }
      else
      {
        // rebuild the shell around the re-encoded image: satellites write
        // first so their fresh FileDataIDs land in the reference chunks
        M2File<V> shell = this->shell;
        shell.md21.bytes = *bytes;

        shell.anim_fdids.clear();
        for (const auto& [packed, buf] : anim_bufs)
        {
          const auto r = fs.add_file(anim_path(stem, packed >> 16, packed & 0xFFFFu),
                                     anim_file_bytes(buf));
          if (!r)
            return make_error(r.error().code,
                              std::format("anim {:04}-{:02}: {}", packed >> 16,
                                          packed & 0xFFFFu, r.error().message),
                              r.error().native_error);
          shell.anim_fdids.push_back({static_cast<std::uint16_t>(packed >> 16),
                                      static_cast<std::uint16_t>(packed & 0xFFFFu), r->value});
        }

        shell.skin_fdids.clear();
        for (std::size_t i = 0; i < this->skins.size(); ++i)
        {
          const auto skin_bytes = this->skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          const auto r =
            fs.add_file(skin_path(stem, static_cast<std::uint32_t>(i)), *skin_bytes);
          if (!r)
            return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                              r.error().native_error);
          shell.skin_fdids.push_back(r->value);
        }
        for (std::size_t i = 0; i < this->lod_skins.size(); ++i)
        {
          const auto skin_bytes = this->lod_skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          const auto r =
            fs.add_file(lod_skin_path(stem, static_cast<std::uint32_t>(i) + 1), *skin_bytes);
          if (!r)
            return make_error(r.error().code,
                              std::format("lod skin {}: {}", i, r.error().message),
                              r.error().native_error);
          shell.skin_fdids.push_back(r->value);
        }

        shell.phys_fdid.clear();
        if (!this->phys.bytes.empty())
        {
          const auto r = fs.add_file(stem + ".phys", this->phys.bytes);
          if (!r)
            return make_error(r.error().code, std::format(".phys: {}", r.error().message),
                              r.error().native_error);
          shell.phys_fdid.push_back(r->value);
        }

        const auto shell_bytes = shell.write();
        if (!shell_bytes)
          return std::unexpected{shell_bytes.error()};
        if (auto r = fs.add_file(*resolved.path, *shell_bytes); !r)
          return std::unexpected{r.error()};
        return {};
      }
    }
  }

#define WOWLIB_M2_INSTANTIATE(Suffix, version_)                                                    \
  template struct M2Data<versions::version_>;                                                      \
  template struct M2<versions::version_>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_INSTANTIATE)
#undef WOWLIB_M2_INSTANTIATE

#define WOWLIB_M2_INSTANTIATE_SKIN(Suffix, version_) template struct Skin<versions::version_>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_INSTANTIATE_SKIN)
#undef WOWLIB_M2_INSTANTIATE_SKIN

#define WOWLIB_M2_INSTANTIATE_FILE(Suffix, version_) template struct M2File<versions::version_>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_INSTANTIATE_FILE)
#undef WOWLIB_M2_INSTANTIATE_FILE
}

namespace wowlib::formats
{
#define WOWLIB_M2_INSTANTIATE_SERIALIZER(Suffix, version_)                                         \
  template struct OffsetFile<m2::M2Data<versions::version_>>;
  WOWLIB_M2_FOR_EACH_VERSION(WOWLIB_M2_INSTANTIATE_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_SERIALIZER

#define WOWLIB_M2_INSTANTIATE_SKIN_SERIALIZER(Suffix, version_)                                    \
  template struct OffsetFile<m2::Skin<versions::version_>>;
  WOWLIB_M2_FOR_EACH_SKIN_VERSION(WOWLIB_M2_INSTANTIATE_SKIN_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_SKIN_SERIALIZER

#define WOWLIB_M2_INSTANTIATE_FILE_SERIALIZER(Suffix, version_)                                    \
  template struct ChunkedFile<m2::M2File<versions::version_>>;
  WOWLIB_M2_FOR_EACH_CHUNKED_VERSION(WOWLIB_M2_INSTANTIATE_FILE_SERIALIZER)
#undef WOWLIB_M2_INSTANTIATE_FILE_SERIALIZER
}
