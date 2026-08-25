#pragma once

/** @file
    The M2 family's satellite-file vocabulary (namespace wowlib::formats::m2),
    shared by the fs-level read/write definitions in m2.hpp and skeleton.hpp:

    - SequenceKey — the (animation id, variation) identity every .anim naming
      convention and AFID entry keys on;
    - SatellitePaths — the companion-file naming conventions around a model
      path;
    - AnimCache — the lazy .anim window cache with the shared per-sequence
      base resolver for reads;
    - AnimBuffers — the per-sequence external buffers with the shared sink
      for writes, and the chunk assembly of .anim files.

    Pure C++ plumbing entities: nothing here welds to the bindings — the
    language surfaces speak through M2/Skeleton read() and write(). */

#include <cstdint>
#include <cstring>
#include <format>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wowlib/core/buffer.hpp>
#include <wowlib/core/error.hpp>
#include <wowlib/formats/common/fourcc.hpp>
#include <wowlib/formats/m2/offset_block.hpp>
#include <wowlib/formats/m2/chunked/records.hpp>

namespace wowlib::formats::m2 {
  /** A sequence's satellite identity: the (animation id, variation) pair the
      .anim naming convention ("{stem}AAAA-SS.anim") and the AFID entries key
      on. Ordered so it can key the per-sequence buffer and cache maps. */
  struct SequenceKey {
    std::uint16_t id = 0; /**< Animation id (AnimationData.dbc). */
    std::uint16_t variation = 0; /**< Variation index within the id. */

    constexpr auto operator<=>(const SequenceKey&) const = default;
  };

  /** The satellite-file naming conventions around one model path: every
      companion file of "creature\\x\\x.m2" derives from the same stem. */
  class SatellitePaths {
  public:
    /** @param model_path the model's resolved client path; a known model
        extension (.m2, .mdx/.mdl in old references, .skel for skeletons) is
        stripped, the remainder is the stem. */
    explicit SatellitePaths(std::string_view model_path)
      : stem_{model_path} {
      for (std::string_view ext : {".m2", ".M2", ".mdx", ".MDX", ".mdl", ".MDL", ".skel", ".SKEL"})
        if (model_path.ends_with(ext)) {
          stem_ = std::string{model_path.substr(0, model_path.size() - ext.size())};
          break;
        }
    }

    /** The model path without its extension. */
    const std::string& stem() const { return stem_; }

    /** "{stem}0N.skin" — the numbered view naming convention. */
    std::string skin(std::uint32_t index) const {
      return std::format("{}{:02}.skin", stem_, index);
    }

    /** "{stem}_lod0N.skin" — LOD bands number from 1. */
    std::string lod_skin(std::uint32_t band) const {
      return std::format("{}_lod{:02}.skin", stem_, band);
    }

    /** "{stem}AAAA-SS.anim" — the external-sequence naming. */
    std::string anim(SequenceKey key) const {
      return std::format("{}{:04}-{:02}.anim", stem_, key.id, key.variation);
    }

    /** "{stem}_NN.bone" — one per FacePose variant. */
    std::string bone(std::uint32_t variant) const {
      return std::format("{}_{:02}.bone", stem_, variant);
    }

    /** "{stem}.skel". */
    std::string skel() const { return stem_ + ".skel"; }

    /** "{stem}.phys". */
    std::string phys() const { return stem_ + ".phys"; }

  private:
    std::string stem_;
  };

  /** A lazy per-sequence .anim loader handing out resolution windows: for a
      chunked file (leading AFM2) the requested chunk's payload, for a raw
      file the whole content when AFM2 is asked for (raw files predate the
      split sections). Missing files and absent sections resolve to empty
      spans — the offset engine leaves those tracks empty. */
  class AnimCache {
  public:
    // .anim chunk ids, forward like all M2-family fourccs.
    static constexpr std::uint32_t afm2_magic = four_cc("AFM2", FourCCEndian::forward);
    static constexpr std::uint32_t afsa_magic = four_cc("AFSA", FourCCEndian::forward);
    static constexpr std::uint32_t afsb_magic = four_cc("AFSB", FourCCEndian::forward);

    /** The AFID FileDataID for sequence @a key, 0 when unlisted. */
    static std::uint32_t afid_lookup(std::span<const chunked::record::AnimFileEntry> entries, SequenceKey key) {
      for (const chunked::record::AnimFileEntry& e : entries)
        if (e.anim_id == key.id && e.sub_anim_id == key.variation) return e.file_id;
      return 0;
    }

    /** @param loader fetches the .anim bytes for a sequence key. */
    explicit AnimCache(std::function<Result<FileBuffer>(SequenceKey)> loader)
      : loader_{std::move(loader)} {}

    /** The resolution window of sequence @a key's file for chunk @a target
        (afm2/afsa/afsb_magic); empty when unavailable. */
    std::span<const std::byte> window(SequenceKey key, std::uint32_t target) {
      auto [it, inserted] = files_.try_emplace(key);
      if (inserted)
        if (auto file = loader_(key)) it->second = std::move(*file);
      if (!it->second) return {};
      const FileBuffer& buf = *it->second;
      std::uint32_t lead = 0;
      if (buf.size() >= 8) std::memcpy(&lead, buf.data(), 4);
      const bool chunked = lead == afm2_magic;
      if (!chunked)
        return target == afm2_magic ? std::span<const std::byte>{buf} : std::span<const std::byte>{};
      for (std::size_t pos = 0; pos + 8 <= buf.size();) {
        std::uint32_t fourcc = 0;
        std::uint32_t size = 0;
        std::memcpy(&fourcc, buf.data() + pos, 4);
        std::memcpy(&size, buf.data() + pos + 4, 4);
        if (size > buf.size() - pos - 8) break;
        if (fourcc == target) return std::span<const std::byte>{buf}.subspan(pos + 8, size);
        pos += 8 + size;
      }
      return {};
    }

    /** The per-sequence base resolver every M2-family read path shares:
        sequence @a i's inner arrays resolve against @a inline_base when its
        data is inline, the EMPTY span when it is an alias (stale records are
        never chased — see M2Sequence::is_alias), or the matching .anim
        window otherwise. The alias-datablock policy lives here ONCE for the
        monolithic, chunked and skeleton decode paths alike.
        @param sequences   the sequence table driving the flags; taken by
                           reference — it may still be mid-decode when the
                           context is built, the resolver reads it lazily.
        @param inline_base the buffer inline sequence data resolves against.
        @param target      the .anim chunk the tracks live in (afm2/afsa/afsb).
        @return the resolver to install as OffsetReadContext::sequence_base. */
    template <typename Sequence>
    auto sequence_base(const std::vector<Sequence>& sequences,
                       std::span<const std::byte> inline_base,
                       std::uint32_t target) {
      return [this, &sequences, inline_base, target](std::size_t i) -> std::span<const std::byte> {
        if (i >= sequences.size()) return inline_base;
        const Sequence& s = sequences[i];
        if (s.is_alias()) return {}; // aliases own no data; their stored records are stale
        if (!s.owns_anim_file()) return inline_base;
        return window(SequenceKey{s.id, s.variation_index}, target);
      };
    }

  private:
    std::function<Result<FileBuffer>(SequenceKey)> loader_;
    std::map<SequenceKey, std::optional<FileBuffer>> files_;
  };

  /** The per-sequence external write buffers every M2-family write path
      shares: an external sequence's data lands in its own per-key buffer as
      the tracks route their blocks through sink(); inline sequences (and
      aliases, whose flags never mark them external) stay in the entity
      image. The buffers then assemble into .anim files chunk by chunk. */
  class AnimBuffers {
  public:
    /** The write context routing external sequences into this buffer set.
        @param sequences the sequence table driving the flags; taken by
                         reference and read lazily, like the read resolver.
        @return the context with OffsetWriteContext::sequence_sink installed. */
    template <typename Sequence>
    OffsetWriteContext sink(const std::vector<Sequence>& sequences) {
      OffsetWriteContext ctx;
      ctx.sequence_sink = [this, &sequences](std::size_t i) -> FileBuffer* {
        if (i >= sequences.size()) return nullptr;
        const Sequence& s = sequences[i];
        if (!s.owns_anim_file()) return nullptr;
        return &bufs_[SequenceKey{s.id, s.variation_index}];
      };
      return ctx;
    }

    /** The filled buffers, keyed and ordered by sequence key. */
    const std::map<SequenceKey, FileBuffer>& entries() const { return bufs_; }

    /** Append this set's buffer for @a key to @a out as one
        fourcc+size+payload chunk (assembling .anim files). A key this set
        never filled still appends an EMPTY section — the client requests
        the file and expects the chunk whenever the flags say so. */
    void append_chunk_to(FileBuffer& out, std::uint32_t fourcc, SequenceKey key) const {
      const auto it = bufs_.find(key);
      append_chunk(out, fourcc,
                   it == bufs_.end() ? std::span<const std::byte>{} : std::span<const std::byte>{it->second});
    }

    /** The union of every set's keys — one .anim file exists per key across
        all sections (AFM2 + AFSA + AFSB). */
    static std::set<SequenceKey> merged_keys(std::initializer_list<const AnimBuffers*> sets) {
      std::set<SequenceKey> keys;
      for (const AnimBuffers* set : sets)
        for (const auto& [key, buf] : set->bufs_) keys.insert(key);
      return keys;
    }

  private:
    static void append_chunk(FileBuffer& out, std::uint32_t fourcc, std::span<const std::byte> payload) {
      const auto size = static_cast<std::uint32_t>(payload.size());
      const std::size_t at = out.size();
      out.resize(at + 8);
      std::memcpy(out.data() + at, &fourcc, 4);
      std::memcpy(out.data() + at + 4, &size, 4);
      out.insert(out.end(), payload.begin(), payload.end());
    }

    std::map<SequenceKey, FileBuffer> bufs_;
  };
}
