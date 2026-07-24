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

#include <wowlib/formats/common/offset_serializer.hpp>
#include <wowlib/formats/m2/m2.hpp>
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
  std::optional<std::string> diff_value(const T& a, const T& b)
  {
    if constexpr (formats::detail::is_vector_v<T>)
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
          if (auto d = diff_value(a[i], b[i]))
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
    else if constexpr (formats::detail::is_offset_record_v<T>)
    {
      static constexpr auto members = formats::detail::members_of<T>();
      std::optional<std::string> out;
      template for (constexpr auto m : members)
      {
        if (!out)
          if (auto d = diff_value(a.[:m:], b.[:m:]))
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
  void roundtrip_m2(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    INFO(label);

    M2<V> model;
    {
      const auto r = model.read(fs, key);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    CHECK(model.data.magic == md20_magic);
    constexpr auto era = m2_wire_version_range(V);
    CHECK(model.data.format_version >= era.first);
    CHECK(model.data.format_version <= era.second);
    CHECK(model.data.num_skin_profiles == model.skins.size());

    // skel-based models keep the external-data gating sequences in the
    // skeleton, and their bone/attachment blocks round-trip separately
    const auto* gate_sequences = &model.data.sequences;
    bool has_skel = false;
    if constexpr (requires { model.shell; })
    {
      has_skel = !model.shell.skeleton_fdid.empty() && model.shell.skeleton_fdid.front() != 0;
      if (has_skel)
        gate_sequences = &model.skel.sequence_block.sequences;
    }

    // rewrite an offset entity, splitting external sequences out per index,
    // then parse it back resolving those buffers
    const auto resplit_roundtrip = [&](const auto& entity, auto& reparsed,
                                       const char* what) -> void {
      std::map<std::size_t, FileBuffer> anim_out;
      OffsetWriteContext wctx;
      wctx.sequence_sink = [&](std::size_t i) -> FileBuffer* {
        if (i >= gate_sequences->size())
          return nullptr;
        const auto& s = (*gate_sequences)[i];
        if (!body::records::sequence_data_external(s.flags) || (s.flags & 0x40u) != 0)
          return nullptr;
        return &anim_out[i];
      };
      const auto rewritten = entity.write(wctx);
      REQUIRE(rewritten.has_value());
      const std::span<const std::byte> rewritten_span{*rewritten};
      OffsetReadContext rctx;
      rctx.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
        const auto it = anim_out.find(i);
        if (it == anim_out.end())
          return rewritten_span;
        return std::span<const std::byte>{it->second};
      };
      {
        const auto r = reparsed.read(rewritten_span, rctx);
        INFO(label << " (" << what << "): " << (r ? std::string{} : r.error().message));
        REQUIRE(r.has_value());
      }
      if (auto d = diff_value(reparsed, entity))
        FAIL(std::format("{} ({}): reparse diverges at {}", label, what, *d));
    };

    M2Data<V> reparsed;
    resplit_roundtrip(model.data, reparsed, "body");

    if constexpr (requires { model.skel; })
      if (has_skel)
      {
        SkelBones<V> bones_back;
        resplit_roundtrip(model.skel.bone_block, bones_back, "SKB1");
        SkelAttachments<V> attachments_back;
        resplit_roundtrip(model.skel.attachment_block, attachments_back, "SKA1");
        SkelSequences<V> sequences_back;
        resplit_roundtrip(model.skel.sequence_block, sequences_back, "SKS1");
      }

    // every skin re-reads equal, too
    for (std::size_t i = 0; i < model.skins.size(); ++i)
    {
      const auto skin_bytes = model.skins[i].write();
      REQUIRE(skin_bytes.has_value());
      Skin<V> skin_back;
      REQUIRE(skin_back.read(std::span<const std::byte>{*skin_bytes}).has_value());
      if (auto d = diff_value(skin_back, model.skins[i]))
        FAIL(std::format("{} (skin {}): reparse diverges at {}", label, i, *d));
    }
  }
}

TEST_CASE("3.3.5a M2s re-read equal after a canonical rewrite",
          "[integration][formats][m2]")
{
  const auto clients = tests::require_clients_dir();
  auto fs = fs::FileSystem::open({.client_path = clients / tests::mpq_client_name,
                                  .version = versions::wotlk});
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
    roundtrip_m2<versions::wotlk>(*fs, FileKey{path}, path);
    ++verified;
  }
  CHECK(verified >= 6);
}

TEST_CASE("9.2.7 M2s re-read equal after a canonical rewrite",
          "[integration][formats][m2]")
{
  const auto clients = tests::require_clients_dir();
  const auto listfile = tests::require_listfile();

  auto fs = fs::FileSystem::open({.client_path = clients / tests::casc_client_name,
                                  .version = versions::shadowlands,
                                  .listfile_csv = listfile});
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
  const std::vector<std::string> skel_candidates{
    "character/human/male/humanmale.m2",
    "character/bloodelf/female/bloodelffemale.m2",
    "character/lightforgeddraenei/male/lightforgeddraeneimale.m2",
    "character/tauren/male/taurenmale.m2",
  };

  int verified = 0;
  std::vector<std::string> skel_verified;
  int unreadable = 0;
  const auto process = [&](const FileKey& key, const std::string& path) {
    const auto bytes = fs->read_file(key);
    if (!bytes)
    {
      ++unreadable;  // encrypted or absent from this install
      WARN("unreadable in this install: " + path);
      return;
    }

    // the chunked shell itself keeps the chunk-framework byte-perfect
    // guarantee (MD21 preserved verbatim on a plain shell round-trip)
    std::uint32_t lead = 0;
    bool has_skel = false;
    if (bytes->size() >= 4)
      std::memcpy(&lead, bytes->data(), 4);
    if (lead != md20_magic)
    {
      M2File<versions::shadowlands> shell;
      {
        const auto r = shell.read(std::span<const std::byte>{*bytes});
        INFO(path << ": " << (r ? std::string{} : r.error().message));
        REQUIRE(r.has_value());
      }
      const auto shell_bytes = shell.write();
      REQUIRE(shell_bytes.has_value());
      // The typed offset-entity chunks (EXP2/PABC/PSBC/PGD1) re-encode
      // canonically, so byte-identity only holds while they are absent;
      // with them present the guarantee is semantic, like the body's.
      const bool re_encoded = !shell.extended_particles2.empty()
                              || !shell.parent_sequence_blacklist.empty()
                              || !shell.parent_sequence_bounds.empty()
                              || !shell.particle_geoset_data.empty();
      if (!re_encoded)
      {
        if (*shell_bytes != *bytes)
          FAIL(std::format("{}: shell rewrite is not byte-identical ({} vs {} bytes)", path,
                           shell_bytes->size(), bytes->size()));
      }
      else
      {
        M2File<versions::shadowlands> shell_back;
        REQUIRE(shell_back.read(std::span<const std::byte>{*shell_bytes}).has_value());
        if (auto d = diff_value(shell_back, shell))
          FAIL(std::format("{}: shell reparse diverges at {}", path, *d));
      }
      has_skel = !shell.skeleton_fdid.empty() && shell.skeleton_fdid.front() != 0;
    }

    roundtrip_m2<versions::shadowlands>(*fs, key, path);
    ++verified;
    if (has_skel)
      skel_verified.push_back(path);
  };

  for (const auto& path : skel_candidates)
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
  std::string skel_list;
  for (const auto& p : skel_verified)
    skel_list += "\n  " + p;
  WARN(std::format("9.2.7 sample: {} verified ({} skel-based:{}), {} unreadable", verified,
                   skel_verified.size(), skel_list, unreadable));
  CHECK(verified >= 15);
  CHECK(!skel_verified.empty());
}
