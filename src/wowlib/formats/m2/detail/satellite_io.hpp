#pragma once

/** @file
    Internal satellite-I/O helpers shared by the M2 family's fs-level
    read/write definitions (formats/m2/io.hpp): naming conventions, the
    .anim window cache, the sequence-ownership predicates and the shared
    per-sequence resolution factories. Not part of the public surface. */

#include <cstring>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/common/offsets.hpp>
#include <wowlib/formats/m2/body/records/shell.hpp>
#include <wowlib/formats/m2/body/records/sequence.hpp>

namespace wowlib::formats::m2::detail
{
  /** The model path without its extension: "creature\\x\\x.m2" -> "creature\\x\\x"
      (.mdx/.mdl spellings appear in old references, .skel for skeletons). */
  inline std::string m2_stem(std::string_view path)
  {
    for (std::string_view ext : {".m2", ".M2", ".mdx", ".MDX", ".mdl", ".MDL", ".skel", ".SKEL"})
      if (path.ends_with(ext))
        return std::string{path.substr(0, path.size() - ext.size())};
    return std::string{path};
  }

  /** "{stem}0N.skin" — the numbered view naming convention. */
  inline std::string skin_path(std::string_view stem, std::uint32_t index)
  {
    return std::format("{}{:02}.skin", stem, index);
  }

  /** "{stem}_lod0N.skin" — LOD bands number from 1. */
  inline std::string lod_skin_path(std::string_view stem, std::uint32_t band)
  {
    return std::format("{}_lod{:02}.skin", stem, band);
  }

  /** "{stem}AAAA-SS.anim" — the external-sequence naming. */
  inline std::string anim_path(std::string_view stem, std::uint32_t anim_id, std::uint32_t sub_id)
  {
    return std::format("{}{:04}-{:02}.anim", stem, anim_id, sub_id);
  }

  /** "{stem}_NN.bone" — one per FacePose variant. */
  inline std::string bone_path(std::string_view stem, std::uint32_t variant)
  {
    return std::format("{}_{:02}.bone", stem, variant);
  }

  /** "{stem}.skel". */
  inline std::string skel_path(std::string_view stem)
  {
    return std::format("{}.skel", stem);
  }

  /** The anim-cache key for a sequence: (id, variation) packed. */
  constexpr std::uint32_t anim_key(std::uint16_t id, std::uint16_t variation)
  {
    return (static_cast<std::uint32_t>(id) << 16) | variation;
  }

  // .anim chunk ids, forward like all M2-family fourccs.
  inline constexpr std::uint32_t afm2_magic = four_cc("AFM2", FourCCEndian::forward);
  inline constexpr std::uint32_t afsa_magic = four_cc("AFSA", FourCCEndian::forward);
  inline constexpr std::uint32_t afsb_magic = four_cc("AFSB", FourCCEndian::forward);

  /** Whether @a flags mark an alias sequence (0x40): it owns NO track data —
      the client resolves it by following alias_next. Files still carry stale
      array records for aliases (dead offsets, junk values, potentially out of
      bounds), so readers must never chase them and writers emit empty
      records. */
  constexpr bool sequence_is_alias(std::uint32_t flags)
  {
    return (flags & 0x40u) != 0;
  }

  /** Whether sequence @a s owns an external .anim file: its data is
      low-priority (0x130 clear) and it is no alias. */
  template <typename Sequence>
  bool owns_anim_file(const Sequence& s)
  {
    return body::records::sequence_data_external(s.flags) && !sequence_is_alias(s.flags);
  }

  /** The AFID FileDataID for a sequence, 0 when unlisted. */
  inline std::uint32_t afid_lookup(std::span<const body::records::AnimFileEntry> entries,
                                   std::uint16_t id, std::uint16_t sub)
  {
    for (const body::records::AnimFileEntry& e : entries)
      if (e.anim_id == id && e.sub_anim_id == sub)
        return e.file_id;
    return 0;
  }

  /** A lazy per-sequence .anim loader handing out resolution windows: for a
      chunked file (leading AFM2) the requested chunk's payload, for a raw
      file the whole content when AFM2 is asked for (raw files predate the
      split sections). Missing files and absent sections resolve to empty
      spans — the offset engine leaves those tracks empty. */
  class AnimCache
  {
  public:
    /** @param loader fetches the .anim bytes for an (id, variation) pair. */
    explicit AnimCache(std::function<Result<FileBuffer>(std::uint16_t, std::uint16_t)> loader)
      : loader_{std::move(loader)}
    {
    }

    /** The resolution window of sequence (@a id, @a sub)'s file for chunk
        @a target (afm2/afsa/afsb_magic); empty when unavailable. */
    std::span<const std::byte> window(std::uint16_t id, std::uint16_t sub, std::uint32_t target)
    {
      auto [it, inserted] = files_.try_emplace(anim_key(id, sub));
      if (inserted)
        if (auto file = loader_(id, sub))
          it->second = std::move(*file);
      if (!it->second)
        return {};
      const FileBuffer& buf = *it->second;
      std::uint32_t lead = 0;
      if (buf.size() >= 8)
        std::memcpy(&lead, buf.data(), 4);
      const bool chunked = lead == afm2_magic;
      if (!chunked)
        return target == afm2_magic ? std::span<const std::byte>{buf}
                                    : std::span<const std::byte>{};
      for (std::size_t pos = 0; pos + 8 <= buf.size();)
      {
        std::uint32_t fourcc = 0;
        std::uint32_t size = 0;
        std::memcpy(&fourcc, buf.data() + pos, 4);
        std::memcpy(&size, buf.data() + pos + 4, 4);
        if (size > buf.size() - pos - 8)
          break;
        if (fourcc == target)
          return std::span<const std::byte>{buf}.subspan(pos + 8, size);
        pos += 8 + size;
      }
      return {};
    }

  private:
    std::function<Result<FileBuffer>(std::uint16_t, std::uint16_t)> loader_;
    std::map<std::uint32_t, std::optional<FileBuffer>> files_;
  };

  /** The per-sequence base resolver every M2-family read path shares:
      sequence @a i's inner arrays resolve against @a inline_base when its
      data is inline, the EMPTY span when it is an alias (stale records are
      never chased — see sequence_is_alias), or the matching .anim window
      otherwise. The alias-datablock policy lives here ONCE for the
      monolithic, chunked and skeleton decode paths alike.
      @param sequences   the sequence table driving the flags; taken by
                         reference — it may still be mid-decode when the
                         context is built, the resolver reads it lazily.
      @param cache       the .anim window cache.
      @param inline_base the buffer inline sequence data resolves against.
      @param window      the .anim chunk the tracks live in (afm2/afsa/afsb).
      @return the resolver to install as OffsetReadContext::sequence_base. */
  template <typename Sequence>
  auto make_sequence_base(const std::vector<Sequence>& sequences, AnimCache& cache,
                          std::span<const std::byte> inline_base, std::uint32_t window)
  {
    return [&sequences, &cache, inline_base,
            window](std::size_t i) -> std::span<const std::byte> {
      if (i >= sequences.size())
        return inline_base;
      const Sequence& s = sequences[i];
      if (sequence_is_alias(s.flags))
        return {};  // aliases own no data; their stored records are stale
      if (!owns_anim_file(s))
        return inline_base;
      return cache.window(s.id, s.variation_index, window);
    };
  }

  /** The per-sequence sink resolver every M2-family write path shares: an
      external sequence @a i's data lands in its own per-(id, variation)
      buffer in @a bufs; inline sequences (and aliases, whose flags never
      mark them external) stay in the entity image.
      @param sequences the sequence table driving the flags.
      @param bufs      the per-sequence buffers, keyed by anim_key().
      @return a write context with the sink installed. */
  template <typename Sequence>
  OffsetWriteContext make_sequence_sink(const std::vector<Sequence>& sequences,
                                        std::map<std::uint32_t, FileBuffer>& bufs)
  {
    OffsetWriteContext ctx;
    ctx.sequence_sink = [&sequences, &bufs](std::size_t i) -> FileBuffer* {
      if (i >= sequences.size())
        return nullptr;
      const Sequence& s = sequences[i];
      if (!owns_anim_file(s))
        return nullptr;
      return &bufs[anim_key(s.id, s.variation_index)];
    };
    return ctx;
  }

  /** Append one fourcc+size+payload chunk to @a out (assembling .anim
      files). */
  inline void append_chunk(FileBuffer& out, std::uint32_t fourcc,
                           std::span<const std::byte> payload)
  {
    const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
    const std::size_t at = out.size();
    out.resize(at + 8);
    std::memcpy(out.data() + at, &fourcc, 4);
    std::memcpy(out.data() + at + 4, &size, 4);
    out.insert(out.end(), payload.begin(), payload.end());
  }
}
