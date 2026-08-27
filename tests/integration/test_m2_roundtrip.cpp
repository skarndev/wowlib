#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <random>
#include <span>
#include <string>
#include <vector>

#include <wowlib/formats/m2/m2.hpp>
#include <wowlib/formats/m2/offset_block.hpp>
#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::m2;

namespace
{
  /** The first bit-level divergence between two decoded values, as a member
      path ("bones[12].rotation.values[3][0]: bytes differ"), or nullopt when
      identical. Bitwise (memcmp) rather than operator== so NaN floats in
      client data — bit-preserved by the round-trip — do not read as
      divergence. */
  template <typename T>
  std::optional<std::string> diffValue(const T& a, const T& b)
  {
    if constexpr (formats::detail::IsVectorV<T>)
    {
      using U = typename T::value_type;
      if (a.size() != b.size())
        return std::format(": size {} vs {}", a.size(), b.size());
      if constexpr (std::is_trivially_copyable_v<U>)
      {
        for (std::size_t i = 0; i < a.size(); ++i)
          if (std::memcmp(&a[i], &b[i], sizeof(U)) != 0)
            return std::format("[{}]: bytes differ", i);
        return std::nullopt;
      }
      else
      {
        for (std::size_t i = 0; i < a.size(); ++i)
          if (auto d = diffValue(a[i], b[i]))
            return std::format("[{}]{}", i, *d);
        return std::nullopt;
      }
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
      if (a != b)
        return std::format(": \"{}\" vs \"{}\"", a, b);
      return std::nullopt;
    }
    else if constexpr (InlineRecordMember<T>)
    {
      static constexpr auto Members = formats::detail::membersOf<T>();
      std::optional<std::string> out;
      template for (constexpr auto m : Members)
      {
        if (!out)
          if (auto d = diffValue(a.[:m:], b.[:m:]))
            out = std::format(".{}{}", std::meta::identifier_of(m), *d);
      }
      return out;
    }
    else
    {
      static_assert(std::is_trivially_copyable_v<T>);
      if (std::memcmp(&a, &b, sizeof(T)) != 0)
        return std::optional<std::string>{": bytes differ"};
      return std::nullopt;
    }
  }

  /** Semantic round-trip of one model: assembly-read from the client (bakes
      .anim + .skin in), canonically rewrite body and skins to buffers, parse
      those back, and require decoded equality — the offset-format guarantee
      (no byte-perfect promise; see m2-architecture notes). */
  template <ClientVersion V>
  void roundtripM2(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    INFO(label);

    M2<V> model;
    {
      const auto r = model.read(fs, key);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    CHECK(model.root.magic == Md20Magic);
    // a freshly read, unmodified client model passes validation with zero
    // errors (warnings are allowed - they mark states real files ship)
    if (const auto valid = model.ensureValid(); !valid)
      FAIL(std::format("{}: {}", label, valid.error().message));
    constexpr auto era = m2FormatVersionRange(V);
    CHECK(model.root.formatVersion >= era.first);
    CHECK(model.root.formatVersion <= era.second);
    // pre-WotLK models embed their skin profiles in the MD20 body; only the
    // external-.skin era carries a separate `skins` assembly to reconcile.
    if constexpr (requires { model.skins; })
      CHECK(model.root.numSkinProfiles == model.skins.size());

    // skel-based models keep the external-data gating sequences in the
    // skeleton, and their bone/attachment blocks round-trip separately
    const auto* gateSequences = &model.root.sequences;
    bool hasSkel = false;
    if constexpr (requires { model.chunks; })
    {
      hasSkel = !model.chunks.skeletonFdid.empty() && model.chunks.skeletonFdid.front() != 0;
      if (hasSkel)
        gateSequences = &model.skel.sequenceBlock.sequences;
    }

    // rewrite an offset entity, splitting external sequences out per index,
    // then parse it back resolving those buffers
    const auto resplitRoundtrip = [&](const auto& entity, auto& reparsed,
                                       const char* what) -> void {
      std::map<std::size_t, FileBuffer> animOut;
      OffsetWriteContext wctx;
      wctx.sequenceSink = [&](std::size_t i) -> FileBuffer* {
        if (i >= gateSequences->size())
          return nullptr;
        const auto& s = (*gateSequences)[i];
        if (!s.ownsAnimFile())
          return nullptr;
        return &animOut[i];
      };
      const auto rewritten = entity.write(wctx);
      REQUIRE(rewritten.has_value());
      const std::span<const std::byte> rewrittenSpan{*rewritten};
      OffsetReadContext rctx;
      rctx.sequenceBase = [&](std::size_t i) -> std::span<const std::byte> {
        const auto it = animOut.find(i);
        if (it == animOut.end())
          return rewrittenSpan;
        return std::span<const std::byte>{it->second};
      };
      {
        const auto r = reparsed.read(rewrittenSpan, rctx);
        INFO(label << " (" << what << "): " << (r ? std::string{} : r.error().message));
        REQUIRE(r.has_value());
      }
      if (auto d = diffValue(reparsed, entity))
        FAIL(std::format("{} ({}): reparse diverges at {}", label, what, *d));
    };

    M2Root<V> reparsed;
    resplitRoundtrip(model.root, reparsed, "body");

    if constexpr (requires { model.skel; })
      if (hasSkel)
      {
        SkelBones<V> bonesBack;
        resplitRoundtrip(model.skel.boneBlock, bonesBack, "SKB1");
        SkelAttachments<V> attachmentsBack;
        resplitRoundtrip(model.skel.attachmentBlock, attachmentsBack, "SKA1");
        SkelSequences<V> sequencesBack;
        resplitRoundtrip(model.skel.sequenceBlock, sequencesBack, "SKS1");
      }

    // every external skin re-reads equal, too (pre-WotLK skins are embedded in
    // the root and already covered by the body round-trip above)
    if constexpr (requires { model.skins; })
      for (std::size_t i = 0; i < model.skins.size(); ++i)
      {
        const auto skinBytes = model.skins[i].write();
        REQUIRE(skinBytes.has_value());
        Skin<V> skinBack;
        REQUIRE(skinBack.read(std::span<const std::byte>{*skinBytes}).has_value());
        if (auto d = diffValue(skinBack, model.skins[i]))
          FAIL(std::format("{} (skin {}): reparse diverges at {}", label, i, *d));
      }
  }
}

TEST_CASE("3.3.5a M2s re-read equal after a canonical rewrite",
          "[integration][formats][m2]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::mpqClient(),
                                  .version = versions::Wotlk});
  REQUIRE(fs.has_value());

  // curated spread: static doodads, creatures with external .anim sets,
  // characters, particle/ribbon-heavy models and a glue screen; entries
  // missing from the client are skipped so path spelling never breaks the
  // suite
  const std::vector<std::string> candidates{
    "Creature/Chicken/Chicken.m2",
    "Creature/Rabbit/Rabbit.m2",
    "Creature/Murloc/Murloc.m2",
    "Creature/KelThuzad/KelThuzad.m2",
    "Creature/Ragnaros/Ragnaros.m2",
    "Creature/Illidan/Illidan.m2",
    "Character/Human/Male/HumanMale.m2",
    "Character/Orc/Male/OrcMale.m2",
    "Character/BloodElf/Female/BloodElfFemale.m2",
    "Character/Scourge/Female/ScourgeFemale.m2",
    "Interface/Glues/Models/UI_MainMenu_Northrend/UI_MainMenu_Northrend.m2",
    "World/Kalimdor/Ogrimmar/PassiveDoodads/OrgrimmarBonfire/OrgrimmarSmokeEmitter.m2",
    "World/Azeroth/Elwynn/PassiveDoodads/ElwynnTrees/ElwynnTree02.m2",
  };

  int verified = 0;
  for (const auto& path : candidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client, skipped: " + path);
      continue;
    }
    roundtripM2<versions::Wotlk>(*fs, FileKey{path}, path);
    ++verified;
  }
  CHECK(verified >= 6);
}

TEST_CASE("1.12.2 M2s re-read equal after a canonical rewrite",
          "[integration][formats][m2]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::vanillaClient(),
                                  .version = versions::Vanilla,
                                  .locale = tests::vanillaLocale()});
  REQUIRE(fs.has_value());

  // vanilla creatures, characters and world doodads (skins embedded in the MD20,
  // no external .anim/.skin files); entries missing from the client are skipped
  // so path spelling never breaks the suite
  const std::vector<std::string> candidates{
    "Creature/Chicken/Chicken.m2",
    "Creature/Rabbit/Rabbit.m2",
    "Creature/Murloc/Murloc.m2",
    "Creature/Ragnaros/Ragnaros.m2",
    "Character/Human/Male/HumanMale.m2",
    "Character/Orc/Male/OrcMale.m2",
    "World/Kalimdor/Ogrimmar/PassiveDoodads/OrgrimmarBonfire/OrgrimmarSmokeEmitter.m2",
    "World/Azeroth/Elwynn/PassiveDoodads/ElwynnTrees/ElwynnTree02.m2",
  };

  int verified = 0;
  for (const auto& path : candidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client, skipped: " + path);
      continue;
    }
    roundtripM2<versions::Vanilla>(*fs, FileKey{path}, path);
    ++verified;
  }
  CHECK(verified >= 4);
}

TEST_CASE("2.4.3 M2s re-read equal after a canonical rewrite",
          "[integration][formats][m2]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::tbcClient(),
                                  .version = versions::Tbc,
                                  .locale = tests::tbcLocale()});
  REQUIRE(fs.has_value());

  // vanilla creatures/characters plus TBC's new playable races (Blood Elf,
  // Draenei); TBC M2s still embed their skins in the MD20 (no external .skin).
  // Missing paths are skipped so spelling never breaks the suite.
  const std::vector<std::string> candidates{
    "Creature/Chicken/Chicken.m2",
    "Creature/Rabbit/Rabbit.m2",
    "Creature/Murloc/Murloc.m2",
    "Creature/Illidan/Illidan.m2",
    "Character/Human/Male/HumanMale.m2",
    "Character/Orc/Male/OrcMale.m2",
    "Character/BloodElf/Female/BloodElfFemale.m2",
    "Character/Draenei/Male/DraeneiMale.m2",
  };

  int verified = 0;
  for (const auto& path : candidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client, skipped: " + path);
      continue;
    }
    roundtripM2<versions::Tbc>(*fs, FileKey{path}, path);
    ++verified;
  }
  CHECK(verified >= 4);
}

TEST_CASE("9.2.7 M2s re-read equal after a canonical rewrite",
          "[integration][formats][m2]")
{
  const auto listfile = tests::requireListfile();

  auto fs = fs::FileSystem::open({.clientPath = tests::cascClient(),
                                  .version = versions::Shadowlands,
                                  .listfileCsv = listfile});
  REQUIRE(fs.has_value());

  // sample models from the community listfile
  std::vector<std::pair<std::uint32_t, std::string>> models;
  {
    std::ifstream in{listfile};
    REQUIRE(in.good());
    std::string line;
    while (std::getline(in, line))
    {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      const auto sep = line.find(';');
      if (sep == std::string::npos)
        continue;
      std::string path = line.substr(sep + 1);
      if (!path.ends_with(".m2"))
        continue;
      models.emplace_back(static_cast<std::uint32_t>(std::stoul(line.substr(0, sep))),
                          std::move(path));
    }
  }
  REQUIRE(models.size() > 100);

  std::mt19937 rng{20260724};  // fixed seed: reproducible sample
  std::shuffle(models.begin(), models.end(), rng);

  // curated skel-based candidates go first (characters, and a parent-skel
  // child: lightforgeddraeneimale shares draeneimale_hd's satellites)
  const std::vector<std::string> skelCandidates{
    "character/human/male/humanmale.m2",
    "character/bloodelf/female/bloodelffemale.m2",
    "character/lightforgeddraenei/male/lightforgeddraeneimale.m2",
    "character/tauren/male/taurenmale.m2",
  };

  int verified = 0;
  std::vector<std::string> skelVerified;
  int unreadable = 0;
  const auto process = [&](const FileKey& key, const std::string& path) {
    const auto bytes = fs->readFile(key);
    if (!bytes)
    {
      ++unreadable;  // encrypted or absent from this install
      WARN("unreadable in this install: " + path);
      return;
    }

    // the chunked shell itself keeps the chunk-framework byte-perfect
    // guarantee (MD21 preserved verbatim on a plain shell round-trip)
    std::uint32_t lead = 0;
    bool hasSkel = false;
    if (bytes->size() >= 4)
      std::memcpy(&lead, bytes->data(), 4);
    if (lead != Md20Magic)
    {
      M2ChunkedFile<versions::Shadowlands> shell;
      {
        const auto r = shell.read(std::span<const std::byte>{*bytes});
        INFO(path << ": " << (r ? std::string{} : r.error().message));
        REQUIRE(r.has_value());
      }
      const auto shellBytes = shell.write();
      REQUIRE(shellBytes.has_value());
      // The typed offset-entity chunks (EXP2/PABC/PSBC/PGD1) re-encode
      // canonically, so byte-identity only holds while they are absent;
      // with them present the guarantee is semantic, like the body's.
      const bool reEncoded = !shell.extendedParticles2.empty()
                              || !shell.parentSequenceBlacklist.empty()
                              || !shell.parentSequenceBounds.empty()
                              || !shell.particleGeosetData.empty();
      if (!reEncoded)
      {
        if (*shellBytes != *bytes)
          FAIL(std::format("{}: shell rewrite is not byte-identical ({} vs {} bytes)", path,
                           shellBytes->size(), bytes->size()));
      }
      else
      {
        M2ChunkedFile<versions::Shadowlands> shellBack;
        REQUIRE(shellBack.read(std::span<const std::byte>{*shellBytes}).has_value());
        if (auto d = diffValue(shellBack, shell))
          FAIL(std::format("{}: shell reparse diverges at {}", path, *d));
      }
      hasSkel = !shell.skeletonFdid.empty() && shell.skeletonFdid.front() != 0;
    }

    roundtripM2<versions::Shadowlands>(*fs, key, path);
    ++verified;
    if (hasSkel)
      skelVerified.push_back(path);
  };

  for (const auto& path : skelCandidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client/listfile, skipped: " + path);
      continue;
    }
    process(FileKey{path}, path);
  }
  for (const auto& [fdid, path] : models)
  {
    if (verified >= 30)
      break;
    process(FileKey{path, FileDataID{fdid}}, path);
  }
  std::string skelList;
  for (const auto& p : skelVerified)
    skelList += "\n  " + p;
  WARN(std::format("9.2.7 sample: {} verified ({} skel-based:{}), {} unreadable", verified,
                   skelVerified.size(), skelList, unreadable));
  CHECK(verified >= 15);
  CHECK(!skelVerified.empty());
}
