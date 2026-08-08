/** @file
    The M2 round-trip driver: the assembly read (satellites baked in) followed
    by write -> parse -> write-again stability compares of the body, the skel
    blocks and every skin — offset formats carry no byte-perfect promise, so
    stability replaces the round-trip tests' reflection diff. The resplit
    logic (external sequences routed into per-index .anim buffers on write and
    resolved back on read) is ported from
    tests/integration/test_m2_roundtrip.cpp. */

#include <cstddef>
#include <format>
#include <map>
#include <span>
#include <string>
#include <type_traits>

#include <wowlib/audit/detail.hpp>
#include <wowlib/formats/m2/m2.hpp>
#include <wowlib/formats/m2/offset_block.hpp>

namespace
{
  using namespace wowlib;
  using namespace wowlib::audit;
  using namespace wowlib::formats;

  /** Compare two per-sequence .anim buffer captures.
      @param first  the buffers of the first write.
      @param second the buffers of the second write.
      @return a description of the first mismatch, or nullopt when equal. */
  std::optional<std::string> compare_anim_captures(
    const std::map<std::size_t, FileBuffer>& first,
    const std::map<std::size_t, FileBuffer>& second)
  {
    for (const auto& [index, buffer] : first)
    {
      const auto it = second.find(index);
      if (it == second.end())
        return std::format("anim {} missing from the rewrite", index);
      if (auto divergence = audit::detail::first_divergence(buffer, it->second))
        return std::format("anim {}: {}", index, *divergence);
    }
    for (const auto& [index, buffer] : second)
      if (!first.contains(index))
        return std::format("anim {} appeared only in the rewrite", index);
    return std::nullopt;
  }

  /** Semantic stability round-trip of one model, per the file comment.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical .m2 path.
      @return the outcome (chunk-stream and skeleton unknown chunks tallied
              where the era has them). */
  template <ClientVersion V>
  RoundtripReport roundtrip_m2(fs::FileSystem& fs, const std::string& path)
  {
    RoundtripReport report;

    // Pre-classify the body bytes: classic clients ship zero-byte placeholder
    // models, and Legion+ content can sit behind unknown TACT keys — both are
    // skips. The assembly read below re-reads the body; that is cheap.
    if (auto outcome =
          audit::detail::classify_read(report, "read", fs.read_file(FileKey{path})))
      return *outcome;

    m2::M2<V> model;
    if (auto r = model.read(fs, FileKey{path}); !r)
    {
      // Satellite files (.skin/.anim/.skel) can be individually encrypted.
      if (r.error().code == ErrorCode::EncryptedContent)
        return audit::detail::skipped("encrypted");
      return audit::detail::fail_with(report, "read", r.error().message);
    }

    // skel-based models keep the external-data gating sequences in the
    // skeleton, and their bone/attachment blocks round-trip separately
    const auto* gate_sequences = &model.root.sequences;
    bool has_skel = false;
    if constexpr (requires { model.chunks; })
    {
      // The M2 file family stores its chunk ids forward, unlike every other
      // chunk format — tally them unreversed (PFDC, not "CDFP").
      audit::detail::tally_unknown(report, model.chunks, formats::FourCCEndian::forward);
      has_skel =
        !model.chunks.skeleton_fdid.empty() && model.chunks.skeleton_fdid.front() != 0;
      if (has_skel)
      {
        gate_sequences = &model.skel.sequence_block.sequences;
        audit::detail::tally_unknown(report, model.skel, formats::FourCCEndian::forward);
      }
    }

    // write an offset entity splitting external sequences out per index, parse
    // it back resolving those buffers, write again with a fresh capture, and
    // byte-compare both the entity images and the captured .anim buffers
    const auto resplit_stability = [&](const auto& entity,
                                       const char* what) -> std::optional<RoundtripReport> {
      using Entity = std::remove_cvref_t<decltype(entity)>;

      const auto sink_into = [&](std::map<std::size_t, FileBuffer>& anim_out) {
        return [&anim_out, gate_sequences](std::size_t i) -> FileBuffer* {
          if (i >= gate_sequences->size())
            return nullptr;
          const auto& s = (*gate_sequences)[i];
          if (!s.owns_anim_file())
            return nullptr;
          return &anim_out[i];
        };
      };

      std::map<std::size_t, FileBuffer> anims_first;
      m2::OffsetWriteContext first_ctx;
      first_ctx.sequence_sink = sink_into(anims_first);
      const auto first = entity.write(first_ctx);
      if (!first)
        return audit::detail::fail_with(report, std::format("{} write", what),
                                 first.error().message);

      const std::span<const std::byte> first_span{*first};
      m2::OffsetReadContext read_ctx;
      read_ctx.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
        const auto it = anims_first.find(i);
        if (it == anims_first.end())
          return first_span;
        return std::span<const std::byte>{it->second};
      };
      Entity reparsed;
      if (auto r = reparsed.read(first_span, read_ctx); !r)
        return audit::detail::fail_with(report, std::format("{} reparse", what), r.error().message);

      std::map<std::size_t, FileBuffer> anims_second;
      m2::OffsetWriteContext second_ctx;
      second_ctx.sequence_sink = sink_into(anims_second);
      const auto second = reparsed.write(second_ctx);
      if (!second)
        return audit::detail::fail_with(report, std::format("{} rewrite", what),
                                 second.error().message);

      if (auto divergence = audit::detail::first_divergence(*first, *second))
        return audit::detail::fail_with(report, std::format("{} compare", what), *divergence);
      if (auto mismatch = compare_anim_captures(anims_first, anims_second))
        return audit::detail::fail_with(report, std::format("{} anim compare", what), *mismatch);
      return std::nullopt;
    };

    if (auto fail = resplit_stability(model.root, "body"))
      return *fail;

    if constexpr (requires { model.skel; })
      if (has_skel)
      {
        if (auto fail = resplit_stability(model.skel.bone_block, "SKB1"))
          return *fail;
        if (auto fail = resplit_stability(model.skel.attachment_block, "SKA1"))
          return *fail;
        if (auto fail = resplit_stability(model.skel.sequence_block, "SKS1"))
          return *fail;
      }

    // every external skin write-stabilizes too (pre-WotLK skins are embedded
    // in the root and already covered by the body round-trip above)
    if constexpr (requires { model.skins; })
      for (std::size_t i = 0; i < model.skins.size(); ++i)
      {
        using Skin = std::remove_cvref_t<decltype(model.skins[i])>;
        const auto first = model.skins[i].write();
        if (!first)
          return audit::detail::fail_with(report, std::format("skin {} write", i),
                                   first.error().message);
        Skin reparsed;
        if (auto r = reparsed.read(std::span<const std::byte>{*first}); !r)
          return audit::detail::fail_with(report, std::format("skin {} reparse", i),
                                   r.error().message);
        const auto second = reparsed.write();
        if (!second)
          return audit::detail::fail_with(report, std::format("skin {} rewrite", i),
                                   second.error().message);
        if (auto divergence = audit::detail::first_divergence(*first, *second))
          return audit::detail::fail_with(report, std::format("skin {} compare", i), *divergence);
      }

    return report;
  }
}

namespace wowlib::audit::detail
{
  RoundtripReport FormatDrivers::m2(fs::FileSystem& fs, const std::string& path,
                                    ClientVersion version)
  {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = with_version(version, [&]<ClientVersion V>() {
      if constexpr (version_supported(formats::m2::m2_versions, V))
        report = guarded([&] { return roundtrip_m2<V>(fs, path); });
    });
    if (!matched)
      return unrecognized_version(version);
    return report;
  }
}
