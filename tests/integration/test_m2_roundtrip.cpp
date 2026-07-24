#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <format>
#include <map>
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
    CHECK(model.data.format_version == 264);
    CHECK(model.data.num_skin_profiles == model.skins.size());

    // rewrite the body, splitting external sequences back out per index
    std::map<std::size_t, FileBuffer> anim_out;
    OffsetWriteContext wctx;
    wctx.sequence_sink = [&](std::size_t i) -> FileBuffer* {
      if (i >= model.data.sequences.size())
        return nullptr;
      const auto& s = model.data.sequences[i];
      if (!records::sequence_data_external(s.flags) || (s.flags & 0x40u) != 0)
        return nullptr;
      return &anim_out[i];
    };
    const auto rewritten = model.data.write(wctx);
    REQUIRE(rewritten.has_value());

    // parse the rewrite back, resolving the split-out buffers
    const std::span<const std::byte> rewritten_span{*rewritten};
    OffsetReadContext rctx;
    rctx.sequence_base = [&](std::size_t i) -> std::span<const std::byte> {
      const auto it = anim_out.find(i);
      if (it == anim_out.end())
        return rewritten_span;
      return std::span<const std::byte>{it->second};
    };
    M2Data<V> reparsed;
    {
      const auto r = reparsed.read(rewritten_span, rctx);
      INFO((r ? std::string{} : r.error().message));
      REQUIRE(r.has_value());
    }
    if (auto d = diff_value(reparsed, model.data))
      FAIL(std::format("{}: reparse diverges at {}", label, *d));

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
