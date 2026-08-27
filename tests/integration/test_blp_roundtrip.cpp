#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <wowlib/formats/blp/blp.hpp>
#include <wowlib/fs/filesystem.hpp>

#include "integration_env.hpp"

using namespace wowlib;
using namespace wowlib::formats;
using namespace wowlib::formats::blp;

namespace
{
  /** Fail with the first divergence offset and the file region it falls into —
      the byte-perfect guarantee's debugging lens. */
  void requireIdentical(const FileBuffer& original, const FileBuffer& rewritten, const BLP& blp,
                         const std::string& label)
  {
    if (original == rewritten)
      return;

    const std::size_t common = std::min(original.size(), rewritten.size());
    std::size_t at = 0;
    while (at < common && original[at] == rewritten[at])
      ++at;

    std::string inside = "gap/tail";
    if (at < 0x94)
      inside = "header";
    else if (at < BlpHeaderBytes)
      inside = "palette";
    else
      for (std::size_t level = 0; level < BlpMaxMips; ++level)
      {
        const std::uint32_t offset = blp.storedLayout.offsets[level];
        const std::uint32_t size = blp.storedLayout.sizes[level];
        if (offset != 0 && size != 0 && at >= offset && at < std::uint64_t{offset} + size)
        {
          inside = std::format("mip {}", level);
          break;
        }
      }
    FAIL(std::format("{}: first divergence at {:#x} inside {} (sizes {} vs {})", label, at,
                     inside, original.size(), rewritten.size()));
  }

  std::map<std::string, int> gEncodingHistogram;

  /** Round-trip one BLP byte-for-byte and decode every stored level. */
  void roundtripBlp(fs::FileSystem& fs, const FileKey& key, const std::string& label)
  {
    const auto data = fs.readFile(key);
    REQUIRE(data.has_value());

    BLP blp;
    const auto parsed = blp.read(*data);
    if (!parsed)
      FAIL(std::format("{}: read failed: {}", label, parsed.error().message));
    // a freshly read, unmodified client texture passes validation with zero
    // errors (warnings are allowed - they mark states real files ship)
    if (const auto valid = blp.ensureValid(); !valid)
      FAIL(std::format("{}: {}", label, valid.error().message));

    const auto rewritten = blp.write();
    REQUIRE(rewritten.has_value());
    requireIdentical(*data, *rewritten, blp, label);

    ++gEncodingHistogram[std::format(
      "{}/pf{}/a{}", [&] {
        switch (blp.colorEncoding)
        {
          case ColorEncoding::Jpeg: return "Jpeg";
          case ColorEncoding::Palettized: return "Pal";
          case ColorEncoding::Dxt: return "Dxt";
          case ColorEncoding::Bgra: return "Bgra";
          case ColorEncoding::BgraAlt: return "BgraAlt";
        }
        return "?";
      }(),
      std::to_underlying(blp.preferredFormat), blp.alphaDepth)];

    for (std::uint32_t level = 0; level < blp.mipCount(); ++level)
    {
      if (blp.mip(level).has_value() == false)
        continue;  // absent middle level
      const auto image = blp.decode(level);
      if (!image)
        FAIL(std::format("{}: decode of level {} failed: {}", label, level,
                         image.error().message));
      CHECK(image->pixels.size()
            == std::size_t{blp.mipWidth(level)} * blp.mipHeight(level) * 4);
    }
  }

  void dumpHistogram(const char* which)
  {
    if (gEncodingHistogram.empty())
      return;
    std::string lines;
    for (const auto& [combo, count] : gEncodingHistogram)
      lines += std::format("  {} x{}\n", combo, count);
    WARN(std::format("{}: encoding/preferredFormat/alphaDepth spread:\n{}", which, lines));
    gEncodingHistogram.clear();
  }
}

TEST_CASE("3.3.5a BLPs rewrite byte-for-byte and decode", "[integration][formats][blp]")
{
  auto fs = fs::FileSystem::open({.clientPath = tests::mpqClient(),
                                  .version = versions::Wotlk});
  REQUIRE(fs.has_value());

  // curated spread across categories: icons and UI (DXT + palettized),
  // tilesets, character/creature skins, glue screens (the 4-bit alpha
  // credits). Entries missing from the client are skipped so path spelling
  // never breaks the suite.
  const std::vector<std::string> candidates{
    "Interface/Icons/INV_Misc_QuestionMark.blp",
    "Interface/Icons/Temp.blp",
    "Interface/Icons/Spell_Nature_HealingTouch.blp",
    "Interface/Tooltips/UI-Tooltip-Background.blp",
    "Interface/DialogFrame/UI-DialogBox-Background.blp",
    "Interface/TargetingFrame/UI-RaidTargetingIcons.blp",
    "Interface/Cursor/Point.blp",
    "Interface/Glues/Credits/wow1.blp",
    "Interface/Glues/Credits/wow2.blp",
    "Interface/Glues/MainMenu/Glues-BlizzardLogo.blp",
    "Tileset/Elwynn/ElwynnGrassBase.blp",
    "Tileset/Durotar/DurotarDirtBase.blp",
    "Tileset/Northrend/NorthrendIcePlain01.blp",
    "Character/Human/Male/HumanMaleSkin00_00.blp",
    "Character/Orc/Male/OrcMaleSkin00_00.blp",
    "Character/Scourge/Female/ScourgeFemaleSkin00_00.blp",
    "Creature/Illidan/Illidan.blp",
    "Textures/Moon02Glare.blp",
    "Textures/SunGlare.blp",
    "World/Environment/Doodad/GlobalDoodads/SnowFlakes/Snowflake04.blp",
  };

  int verified = 0;
  for (const auto& path : candidates)
  {
    if (!fs->exists(path))
    {
      WARN("not in client, skipped: " + path);
      continue;
    }
    roundtripBlp(*fs, FileKey{path}, path);
    ++verified;
  }
  dumpHistogram("3.3.5a");
  CHECK(verified >= 8);  // enough coverage even if some curated paths drift
}

TEST_CASE("9.2.7 BLPs rewrite byte-for-byte and decode", "[integration][formats][blp]")
{
  const auto listfile = tests::requireListfile();

  auto fs = fs::FileSystem::open({.clientPath = tests::cascClient(),
                                  .version = versions::Shadowlands,
                                  .listfileCsv = listfile});
  REQUIRE(fs.has_value());

  // sample textures from the community listfile: every .blp path
  std::vector<std::pair<std::uint32_t, std::string>> textures;
  {
    std::ifstream in{listfile};
    REQUIRE(in.good());
    std::string line;
    while (std::getline(in, line))
    {
      if (!line.empty() && line.back() == '\r')  // community listfile is CRLF
        line.pop_back();
      const auto sep = line.find(';');
      if (sep == std::string::npos)
        continue;
      std::string path = line.substr(sep + 1);
      if (!path.ends_with(".blp"))
        continue;
      textures.emplace_back(static_cast<std::uint32_t>(std::stoul(line.substr(0, sep))),
                            std::move(path));
    }
  }
  REQUIRE(textures.size() > 1000);

  std::mt19937 rng{20260730};  // fixed seed: reproducible sample
  std::shuffle(textures.begin(), textures.end(), rng);

  int verified = 0;
  int skipped = 0;
  for (const auto& [fdid, path] : textures)
  {
    if (verified >= 60)
      break;
    const FileKey key{path, FileDataID{fdid}};
    const auto probe = fs->readFile(key);
    if (!probe)
    {
      ++skipped;  // encrypted or absent from this install
      continue;
    }
    roundtripBlp(*fs, key, path);
    ++verified;
  }
  dumpHistogram("9.2.7");
  WARN(std::format("9.2.7 sample: {} verified, {} unreadable/skipped", verified, skipped));
  CHECK(verified >= 40);
}
