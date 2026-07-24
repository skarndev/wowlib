#include <wowlib/formats/m2/m2.hpp>

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

    /** "{stem}0N.skin" — the pre-Legion skin naming convention. */
    std::string skin_path(std::string_view stem, std::uint32_t index)
    {
      return std::format("{}{:02}.skin", stem, index);
    }

    /** "{stem}AAAA-SS.anim" — the pre-Legion external-sequence naming. */
    std::string anim_path(std::string_view stem, std::uint32_t anim_id, std::uint32_t sub_id)
    {
      return std::format("{}{:04}-{:02}.anim", stem, anim_id, sub_id);
    }

    /** The anim-cache key for a sequence: (id, variation) packed. */
    constexpr std::uint32_t anim_key(std::uint16_t id, std::uint16_t variation)
    {
      return (static_cast<std::uint32_t>(id) << 16) | variation;
    }

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
  }

  template <ClientVersion V>
  Result<void> M2<V>::read(fs::FileSystem& fs, const FileKey& key)
  {
    if constexpr (V >= m2_chunked_container)
    {
      (void)fs;
      (void)key;
      return make_error(ErrorCode::NotImplemented,
                        "Legion+ chunked M2 reading is not implemented yet");
    }
    else
    {
      const auto main = fs.read_file(key);
      if (!main)
        return std::unexpected{main.error()};

      data = {};

      if constexpr (V < m2_per_sequence_timelines)
      {
        if (auto r = data.read(std::span<const std::byte>{*main}); !r)
          return r;
        return check_header<V>(data.magic, data.format_version);
      }
      else
      {
        this->skins.clear();

        const FileKey resolved = fs.resolve(key);
        if (!resolved.path)
          return make_error(ErrorCode::PathNotResolvable,
                            "a pre-Legion M2's satellite files (.skin/.anim) need a "
                            "resolvable path for the model key");
        const std::string stem = m2_stem(*resolved.path);

        // Low-priority sequence data lives in per-sequence .anim files; the
        // context resolves them lazily — the sequences table is populated
        // before any track member reads (wire order), so the flags are
        // already decoded when the first track consults us. A missing .anim
        // file leaves its sequences' tracks empty rather than failing.
        const std::span<const std::byte> main_span{*main};
        std::map<std::uint32_t, std::optional<FileBuffer>> anim_cache;
        OffsetReadContext ctx;
        ctx.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
          if (i >= data.sequences.size())
            return main_span;
          const auto& s = data.sequences[i];
          if (!owns_anim_file(s))
            return main_span;
          auto [it, inserted] = anim_cache.try_emplace(anim_key(s.id, s.variation_index));
          if (inserted)
            if (auto file = fs.read_file(FileKey{anim_path(stem, s.id, s.variation_index)}))
              it->second = std::move(*file);
          if (!it->second)
            return {};
          return std::span<const std::byte>{*it->second};
        };
        if (auto r = data.read(main_span, ctx); !r)
          return r;
        if (auto r = check_header<V>(data.magic, data.format_version); !r)
          return r;

        this->skins.reserve(data.num_skin_profiles);
        for (std::uint32_t i = 0; i < data.num_skin_profiles; ++i)
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
          this->skins.push_back(std::move(skin));
        }
        return {};
      }
    }
  }

  template <ClientVersion V>
  Result<void> M2<V>::write(fs::FileSystem& fs, const FileKey& key) const
  {
    if constexpr (V >= m2_chunked_container)
    {
      (void)fs;
      (void)key;
      return make_error(ErrorCode::NotImplemented,
                        "Legion+ chunked M2 writing is not implemented yet");
    }
    else
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
        if (auto r = fs.add_file(*resolved.path, *bytes); !r)
          return std::unexpected{r.error()};

        for (const auto& [packed, buf] : anim_bufs)
          if (auto r = fs.add_file(anim_path(stem, packed >> 16, packed & 0xFFFFu), buf); !r)
            return make_error(r.error().code,
                              std::format("anim {:04}-{:02}: {}", packed >> 16, packed & 0xFFFFu,
                                          r.error().message),
                              r.error().native_error);

        for (std::size_t i = 0; i < this->skins.size(); ++i)
        {
          const auto skin_bytes = this->skins[i].write();
          if (!skin_bytes)
            return std::unexpected{skin_bytes.error()};
          if (auto r = fs.add_file(skin_path(stem, static_cast<std::uint32_t>(i)), *skin_bytes);
              !r)
            return make_error(r.error().code, std::format("skin {}: {}", i, r.error().message),
                              r.error().native_error);
        }
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
}
