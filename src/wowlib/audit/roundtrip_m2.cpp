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

namespace {
  using namespace wowlib;
  using namespace wowlib::audit;
  using namespace wowlib::formats;

  /** Compare two per-sequence .anim buffer captures.
      @param first  the buffers of the first write.
      @param second the buffers of the second write.
      @return a description of the first mismatch, or nullopt when equal. */
  std::optional<std::string> compareAnimCaptures(const std::map<std::size_t, FileBuffer>& first,
                                                   const std::map<std::size_t, FileBuffer>& second) {
    for (const auto& [index, buffer] : first) {
      const auto it = second.find(index);
      if (it == second.end()) return std::format("anim {} missing from the rewrite", index);
      if (auto divergence = audit::detail::firstDivergence(buffer, it->second)) return std::format(
        "anim {}: {}", index, *divergence);
    }
    for (const auto& [index, buffer] : second)
      if (!first.contains(index)) return std::format("anim {} appeared only in the rewrite", index);
    return std::nullopt;
  }

  /** Semantic stability round-trip of one model, per the file comment.
      @tparam V the client version.
      @param fs   the client filesystem.
      @param path the canonical .m2 path.
      @return the outcome (chunk-stream and skeleton unknown chunks tallied
              where the era has them). */
  template <ClientVersion V>
  RoundtripReport roundtripM2(fs::FileSystem& fs, const std::string& path) {
    RoundtripReport report;

    // Pre-classify the body bytes: classic clients ship zero-byte placeholder
    // models, and Legion+ content can sit behind unknown TACT keys — both are
    // skips. The assembly read below re-reads the body; that is cheap.
    if (auto outcome = audit::detail::classifyRead(report, "read", fs.readFile(FileKey{path}))) return *outcome;

    m2::M2 < V > model;
    if (auto r = model.read(fs, FileKey{path}); !r) {
      // Satellite files (.skin/.anim/.skel) can be individually encrypted.
      if (r.error().code == ErrorCode::EncryptedContent) return audit::detail::skipped("encrypted");
      return audit::detail::failWith(report, "read", r.error().message);
    }

    // skel-based models keep the external-data gating sequences in the
    // skeleton, and their bone/attachment blocks round-trip separately
    const auto* gateSequences = &model.root.sequences;
    bool hasSkel = false;
    if constexpr (requires { model.chunks; }) {
      // The M2 file family stores its chunk ids forward, unlike every other
      // chunk format — tally them unreversed (PFDC, not "CDFP").
      audit::detail::tallyUnknown(report, model.chunks, formats::FourCCEndian::Forward);
      hasSkel = !model.chunks.skeletonFdid.empty() && model.chunks.skeletonFdid.front() != 0;
      if (hasSkel) {
        gateSequences = &model.skel.sequenceBlock.sequences;
        audit::detail::tallyUnknown(report, model.skel, formats::FourCCEndian::Forward);
      }
    }

    // write an offset entity splitting external sequences out per index, parse
    // it back resolving those buffers, write again with a fresh capture, and
    // byte-compare both the entity images and the captured .anim buffers
    const auto resplitStability = [&](const auto& entity, const char* what) -> std::optional<RoundtripReport> {
      using Entity = std::remove_cvref_t<decltype(entity)>;

      const auto sinkInto = [&](std::map<std::size_t, FileBuffer>& animOut) {
        return [&animOut, gateSequences](std::size_t i) -> FileBuffer* {
          if (i >= gateSequences->size()) return nullptr;
          const auto& s = (*gateSequences)[i];
          if (!s.ownsAnimFile()) return nullptr;
          return &animOut[i];
        };
      };

      std::map<std::size_t, FileBuffer> animsFirst;
      m2::OffsetWriteContext firstCtx;
      firstCtx.sequenceSink = sinkInto(animsFirst);
      const auto first = entity.write(firstCtx);
      if (!first)
        return audit::detail::failWith(report, std::format("{} write", what), first.error().message);

      const std::span<const std::byte> firstSpan{*first};
      m2::OffsetReadContext readCtx;
      readCtx.sequenceBase = [& ](std::size_t i) -> std::span<const std::byte> {
        const auto it = animsFirst.find(i);
        if (it == animsFirst.end()) return firstSpan;
        return std::span<const std::byte>{it->second};
      };
      Entity reparsed;
      if (auto r = reparsed.read(firstSpan, readCtx); !r)
        return audit::detail::failWith(report, std::format("{} reparse", what), r.error().message);

      std::map<std::size_t, FileBuffer> animsSecond;
      m2::OffsetWriteContext secondCtx;
      secondCtx.sequenceSink = sinkInto(animsSecond);
      const auto second = reparsed.write(secondCtx);
      if (!second)
        return audit::detail::failWith(report, std::format("{} rewrite", what), second.error().message);

      if (auto divergence = audit::detail::firstDivergence(*first, *second))
        return audit::detail::failWith(report, std::format("{} compare", what), *divergence);
      if (auto mismatch = compareAnimCaptures(animsFirst, animsSecond))
        return audit::detail::failWith(report, std::format("{} anim compare", what), *mismatch);
      return std::nullopt;
    };

    if (auto fail = resplitStability(model.root, "body")) return *fail;

    if constexpr (requires { model.skel; })
      if (hasSkel) {
        if (auto fail = resplitStability(model.skel.boneBlock, "SKB1")) return *fail;
        if (auto fail = resplitStability(model.skel.attachmentBlock, "SKA1")) return *fail;
        if (auto fail = resplitStability(model.skel.sequenceBlock, "SKS1")) return *fail;
      }

    // every external skin write-stabilizes too (pre-WotLK skins are embedded
    // in the root and already covered by the body round-trip above)
    if constexpr (requires { model.skins; })
      for (std::size_t i = 0; i < model.skins.size(); ++i) {
        using Skin = std::remove_cvref_t<decltype(model.skins[i])>;
        const auto first = model.skins[i].write();
        if (!first)
          return audit::detail::failWith(report, std::format("skin {} write", i), first.error().message);
        Skin reparsed;
        if (auto r = reparsed.read(std::span<const std::byte>{*first}); !r)
          return audit::detail::failWith(report, std::format("skin {} reparse", i), r.error().message);
        const auto second = reparsed.write();
        if (!second)
          return audit::detail::failWith(report, std::format("skin {} rewrite", i), second.error().message);
        if (auto divergence = audit::detail::firstDivergence(*first, *second))
          return audit::detail::failWith(report, std::format("skin {} compare", i), *divergence);
      }

    return report;
  }
}

namespace wowlib::audit::detail {
  RoundtripReport FormatDrivers::m2(fs::FileSystem& fs, const std::string& path, ClientVersion version) {
    RoundtripReport report = skipped("unsupported-version");
    const bool matched = withVersion(version, [&]<ClientVersion V>() {
      if constexpr (versionSupported(formats::m2::M2Versions, V)) report = guarded([&] {
        return roundtripM2<V>(fs, path);
      });
    });
    if (!matched) return unrecognizedVersion(version);
    return report;
  }
}
